/*
 * Day 03: MusicPlayer pipeline practice.
 *
 * Study plan: study/week1-music-player-practice.md
 * Study note: studyNote/week1-music-player-notes.md
 *
 * HJMedia reference source:
 * - docs/Readme_MusicPlayer.md
 * - docs/architecture/HJGraphMusicPlayer_AudioContextGuide.md
 * - docs/architecture/HJGraphMusicPlayer.md
 */

#include "study_demo_common.h"

#include <shared_mutex>
#include <type_traits>

namespace {

// DemoSync 是一个学习版的 HJSync。
//
// HJMedia 源码里的 SYNC_CONS_LOCK / SYNC_PROD_LOCK 来自 HJSyncObject::m_sync。
// 这里不直接依赖项目源码，而是用标准库复刻它的核心语义：
// - consLock：consumer/read side，使用 shared_lock，允许多个普通访问同时进入。
// - prodLock：producer/write side，使用 unique_lock，独占执行强状态修改。
//
// 这个 demo 的重点不是做一个完整通用同步库，而是帮助理解：
// HJGraphMusicPlayer 为什么能让 getCurrentTimestamp/openURL 这类普通接口并发，
// 但 pause/resume/close 这类强状态修改必须互斥。
class DemoSync {
public:
    template <typename Fn>
    auto consLock(Fn&& fn) -> decltype(fn())
    {
        // shared_lock 对应“读锁”。多个 consLock 可以同时持有这把锁。
        // 这模拟 HJGraphMusicPlayer 中多个查询/普通控制接口并发进入的情况。
        std::shared_lock<std::shared_mutex> lock(mutex_);

        // HJSync 里有 producer 优先语义：只要有 prodLock 已经声明要进入，
        // 新的 consLock 就先等待，避免写侧长期饥饿。
        //
        // 注意：这里等待的是 prodCount_ == 0，而不是只看 unique_lock 是否已持有。
        // 因为 writer 可能还在等已有 reader 退出，但它已经表达了“我要写”的意图。
        condition_.wait(lock, [this] {
            return prodCount_.load() == 0;
        });

        // 真正的业务逻辑被包在 lambda 里执行。
        // 这样调用方可以写出和 HJMedia 类似的：
        // SYNC_CONS_LOCK([this] { ... });
        return fn();
    }

    template <typename Fn>
    auto prodLock(Fn&& fn) -> decltype(fn())
    {
        // 先增加 prodCount_，再去拿 unique_lock。
        // 这样即使当前有 reader 持有 shared_lock，后续新 reader 也会因为
        // prodCount_ > 0 在 consLock 的 condition_ 上等待。
        ++prodCount_;
        try {
            // unique_lock 对应“写锁”。它必须等待所有 shared_lock 释放后才能进入。
            // 这模拟 pause/resume/close 这类需要看到一致状态的强修改操作。
            std::unique_lock<std::shared_mutex> lock(mutex_);

            // lambda 可能返回 void，也可能返回一个值。
            // HJSync 原实现也需要兼容这两类调用方式。
            if constexpr (std::is_void_v<decltype(fn())>) {
                fn();
                if (--prodCount_ == 0) {
                    condition_.notify_all();
                }
            } else {
                decltype(auto) result = fn();
                if (--prodCount_ == 0) {
                    condition_.notify_all();
                }
                return result;
            }
        } catch (...) {
            // 如果业务 lambda 抛异常，也必须回退 prodCount_ 并唤醒等待者。
            // 否则后续 consLock 会一直等待 prodCount_ 归零，形成逻辑死锁。
            if (--prodCount_ == 0) {
                condition_.notify_all();
            }
            throw;
        }
    }

private:
    std::shared_mutex mutex_;
    std::condition_variable_any condition_;
    std::atomic<int> prodCount_{0};
};

// SyncLockProbe 专门用于验证锁语义，不属于 MusicPipeline 的播放链路。
//
// 自检目标：
// 1. 两个 reader 可以同时进入 consLock，证明 shared_lock 是共享读锁。
// 2. writer 不能在 reader 未退出时进入 prodLock，证明 unique_lock 会等待读锁释放。
// 3. writer 进入时 activeReaders 必须为 0，证明读写互斥成立。
class SyncLockProbe {
public:
    std::size_t holdRead(
        std::condition_variable_any& releaseCv,
        std::mutex& releaseMutex,
        bool& releaseReaders,
        std::atomic<int>& activeReaders,
        std::atomic<int>& maxActiveReaders,
        std::atomic<int>& readersEntered)
    {
        return sync_.consLock([&] {
            // activeReaders 表示当前正在 consLock 内部执行的 reader 数量。
            // 如果两个 reader 同时进入，maxActiveReaders 最终应当至少为 2。
            const int current = activeReaders.fetch_add(1) + 1;
            int observed = maxActiveReaders.load();
            while (observed < current && !maxActiveReaders.compare_exchange_weak(observed, current)) {
            }
            readersEntered.fetch_add(1);
            logFields("reader-enter", {{"activeReaders", std::to_string(current)}});

            // 故意把 reader 卡在锁内，给 writer 一个竞争机会。
            // 如果 prodLock 正确，它会等这两个 reader 都退出后才进入。
            std::unique_lock<std::mutex> lock(releaseMutex);
            releaseCv.wait(lock, [&] {
                return releaseReaders;
            });

            const int remaining = activeReaders.fetch_sub(1) - 1;
            logFields("reader-exit", {{"activeReaders", std::to_string(remaining)}});
            return static_cast<std::size_t>(current);
        });
    }

    int holdWrite(std::atomic<int>& activeReaders, std::atomic<int>& writerObservedReaders)
    {
        return sync_.prodLock([&] {
            // writer 进入 prodLock 的瞬间记录 reader 数量。
            // 正确结果必须是 0；如果不是 0，说明写锁和读锁没有互斥。
            const int readersAtEntry = activeReaders.load();
            writerObservedReaders.store(readersAtEntry);
            logFields("writer-enter", {{"activeReaders", std::to_string(readersAtEntry)}});
            return readersAtEntry;
        });
    }

private:
    void logFields(const std::string& action, const std::map<std::string, std::string>& fields)
    {
        std::lock_guard<std::mutex> lock(logMutex_);
        hjstudy::logFields("sync-lock", action, fields);
    }

    DemoSync sync_{};
    std::mutex logMutex_{};
};

#define SYNC_CONS_LOCK(...) sync_.consLock(__VA_ARGS__)
#define SYNC_PROD_LOCK(...) sync_.prodLock(__VA_ARGS__)

// MusicPipeline 是一个压缩版的 MusicPlayer 主链路模拟。
//
// 它只保留 Day 3 需要理解的核心流程：
// openURL -> demux -> decode -> resample -> render -> timeline/eof
//
// 和 HJGraphMusicPlayer 的对应关系：
// - openUrl() / getCurrentTimestamp()：普通入口，用 SYNC_CONS_LOCK 保护成员访问。
// - pause() / resume() / close()：强状态修改，用 SYNC_PROD_LOCK 独占保护。
// - demux/decode/resample/render：模拟插件链路中的数据流。
class MusicPipeline {
public:
    void openUrl(std::string url)
    {
        // 在真实 HJGraphMusicPlayer 中，openURL() 用 SYNC_CONS_LOCK：
        // 它主要保护“graph 还活着、m_demuxer 指针稳定”这个访问窗口。
        // 真正打开 URL 的复杂逻辑由 demuxer 自己负责。
        SYNC_CONS_LOCK([this, url = std::move(url)] {
            url_ = url;
            hjstudy::logFields("graph", "openURL", {{"url", url_}});
        });
    }

    void pause()
    {
        // pause 会同时修改 paused_、timeline、render 状态。
        // 这类操作不能让其他线程看到“只改了一半”的中间状态，所以用 prodLock。
        SYNC_PROD_LOCK([this] {
            if (!paused_) {
                paused_ = true;
                hjstudy::logLine("graph", "pause under SYNC_PROD_LOCK");
            }
        });
    }

    void resume()
    {
        // resume 和 pause 对称，也属于强状态修改。
        // 在真实播放器中，它通常要恢复 render，再恢复 timeline 推进。
        SYNC_PROD_LOCK([this] {
            if (paused_) {
                paused_ = false;
                hjstudy::logLine("graph", "resume under SYNC_PROD_LOCK");
            }
        });
    }

    void close()
    {
        // close/done/release 一类生命周期操作会改变对象是否可用。
        // 用 prodLock 可以阻止普通查询/控制接口在关键状态切换期间并发进入。
        SYNC_PROD_LOCK([this] {
            closed_ = true;
            hjstudy::logLine("graph", "close under SYNC_PROD_LOCK");
        });
    }

    int64_t getCurrentTimestamp()
    {
        // 进度查询是典型 consumer/read side 操作：
        // 它只需要安全读取当前 timeline 值，不需要独占整个 graph。
        return SYNC_CONS_LOCK([this] {
            return visiblePlaybackHeadMs_;
        });
    }

    void playUntilEof()
    {
        // 这个 demo 的播放流程仍然是同步顺序执行。
        // 真正的 HJMedia 会通过插件线程、队列和调度器异步推进。
        if (closed_) {
            hjstudy::logLine("graph", "skip playback because graph is closed");
            return;
        }

        demux();
        decode();
        resample();
        render();
        reportEof();
    }

private:
    void demux()
    {
        // Demuxer：从媒体源读取压缩 packet。
        // 这里用三帧 AAC packet 模拟真实 demuxer 从文件/网络读出的压缩数据。
        compressedPackets_ = {
            {0, hjstudy::MediaType::Audio, true, 0, "aac-packet-0"},
            {23, hjstudy::MediaType::Audio, false, 0, "aac-packet-1"},
            {46, hjstudy::MediaType::Audio, false, 0, "aac-packet-2"},
        };

        // demuxer EOF 只表示“源数据读完了”。
        // 它不等于最终播放完成，因为 decoder/resampler/render 里可能还有缓存帧。
        demuxerEof_ = true;
        hjstudy::logLine("demuxer", "source has no more packets, demuxer EOF is true");
    }

    void decode()
    {
        // Decoder：把压缩 packet 转成 PCM 原始音频帧。
        // demo 中只改 payload 文本，真实项目里会调用 FFmpeg/平台解码器。
        for (const auto& packet : compressedPackets_) {
            decodedFrames_.push_back({packet.ptsMs, packet.type, packet.keyFrame, packet.generation, "pcm(" + packet.payload + ")"});
        }
        hjstudy::logLine("decoder", "compressed packets converted to PCM frames");
    }

    void resample()
    {
        // Resampler：把 PCM 统一到 render 需要的目标格式。
        // 典型转换包括采样率、声道数、sample format、帧打包大小。
        for (auto& frame : decodedFrames_) {
            frame.payload = "s16-stereo-48k(" + frame.payload + ")";
        }
        hjstudy::logLine("resampler", "PCM format normalized for renderer");
    }

    void render()
    {
        // Render：消费 PCM，推进“用户实际听到的位置”。
        // 这就是为什么 MusicPlayer 的 timeline 更接近 render 侧，而不是 demux/decode 侧。
        for (const auto& frame : decodedFrames_) {
            visiblePlaybackHeadMs_ = frame.ptsMs;
            hjstudy::logFields(
                "render",
                "consume",
                {{"ptsMs", std::to_string(frame.ptsMs)}, {"payload", frame.payload}});
        }

        // finalPlaybackEof 表示 render 已经消费完最后一帧。
        // 它比 demuxerEof 更接近“用户听完了”这个业务语义。
        finalPlaybackEof_ = true;
    }

    void reportEof() const
    {
        // 同时打印两个 EOF，强调 Day 3 的核心区别：
        // - demuxerEof：源不再产出压缩 packet。
        // - finalPlaybackEof：render 已经消费到播放终点。
        hjstudy::logFields(
            "graph",
            "eof-state",
            {
                {"demuxerEof", hjstudy::yesNo(demuxerEof_)},
                {"finalPlaybackEof", hjstudy::yesNo(finalPlaybackEof_)},
                {"timelineMs", std::to_string(visiblePlaybackHeadMs_)},
            });
    }

    std::string url_;
    std::vector<hjstudy::Frame> compressedPackets_;
    std::vector<hjstudy::Frame> decodedFrames_;
    bool paused_{false};
    bool closed_{false};
    bool demuxerEof_{false};
    bool finalPlaybackEof_{false};
    int64_t visiblePlaybackHeadMs_{0};

    // 每个 MusicPipeline 实例持有自己的同步对象。
    // 真实 HJGraphMusicPlayer 则通过继承 HJSyncObject 获得 m_sync。
    DemoSync sync_{};
};

#undef SYNC_CONS_LOCK
#undef SYNC_PROD_LOCK

void runSyncLockSelfCheck()
{
    SyncLockProbe probe;

    // releaseReaders 用来控制两个 reader 什么时候从 consLock 内退出。
    // writerObservedReaders 初始为 -1；只要 writer 成功进入 prodLock，就会改成
    // 它进入时观察到的 activeReaders 数量。
    std::mutex releaseMutex;
    std::condition_variable_any releaseCv;
    bool releaseReaders = false;
    std::atomic<int> activeReaders{0};
    std::atomic<int> maxActiveReaders{0};
    std::atomic<int> readersEntered{0};
    std::atomic<int> writerObservedReaders{-1};

    auto readerTask = [&] {
        probe.holdRead(releaseCv, releaseMutex, releaseReaders, activeReaders, maxActiveReaders, readersEntered);
    };

    std::thread reader1(readerTask);
    std::thread reader2(readerTask);

    // 等两个 reader 都已经进入 consLock，再启动 writer。
    // 这样可以稳定复现“写锁等待读锁释放”的场景。
    while (readersEntered.load() < 2) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    std::thread writer([&] {
        probe.holdWrite(activeReaders, writerObservedReaders);
    });

    // 给 writer 一点时间尝试进入。
    // 如果 writerObservedReaders 仍是 -1，说明 writer 被读锁正确挡住了。
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    if (writerObservedReaders.load() != -1) {
        throw std::runtime_error("SYNC_PROD_LOCK should wait until all readers leave");
    }

    {
        std::lock_guard<std::mutex> lock(releaseMutex);
        releaseReaders = true;
    }
    releaseCv.notify_all();

    reader1.join();
    reader2.join();
    writer.join();

    // 验证点 1：两个 reader 同时进入过 consLock。
    if (maxActiveReaders.load() < 2) {
        throw std::runtime_error("SYNC_CONS_LOCK should allow multiple concurrent readers");
    }

    // 验证点 2：writer 进入 prodLock 时，reader 数量已经归零。
    if (writerObservedReaders.load() != 0) {
        throw std::runtime_error("SYNC_PROD_LOCK should enter only after reader count drops to zero");
    }

    hjstudy::logFields(
        "sync-lock",
        "self-check",
        {
            {"maxConcurrentReaders", std::to_string(maxActiveReaders.load())},
            {"writerReadersAtEntry", std::to_string(writerObservedReaders.load())},
        });
}

} // namespace

int main()
{
    hjstudy::printReferences(
        "study/week1-music-player-practice.md Day 3",
        "studyNote/week1-music-player-notes.md Day 3",
        {
            "docs/Readme_MusicPlayer.md",
            "docs/architecture/HJGraphMusicPlayer_AudioContextGuide.md",
            "docs/architecture/HJGraphMusicPlayer.md",
        });

    runSyncLockSelfCheck();

    // 主流程仍然按 Day 3 的 MusicPlayer 数据链路执行。
    // 前面的 self-check 只验证锁语义；这里演示音频数据如何从源走到 render。
    MusicPipeline pipeline;
    pipeline.openUrl("file:///demo/music.aac");
    pipeline.pause();
    pipeline.resume();
    pipeline.playUntilEof();
    hjstudy::logFields("graph", "current-timestamp", {{"ts", std::to_string(pipeline.getCurrentTimestamp())}});
    pipeline.close();
}
