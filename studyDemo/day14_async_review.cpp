/*
 * Day 14: week 2 async model review.
 *
 * Study plan: study/week2-thread-plugin-player-practice.md
 * Study note: studyNote/week2-review.md
 *
 * HJMedia reference source:
 * - src/utils/HJThread/doc/README.md
 * - src/plugins/doc/HJPlugin.md
 * - src/plugins/doc/HJMediaFrameDeque.md
 * - src/graphs/HJGraphLivePlayer.cpp
 * - src/graphs/HJGraphVodPlayer.cpp
 * - src/graphs/HJGraphMusicPlayer.cpp
 * - src/core/HJMediaPlayer.cc
 * - src/core/HJMediaNode.cc
 */

#include "study_demo_common.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

namespace {

// 把当前线程 id 转成字符串，方便日志里直观看到
// deliver 和 runTask 是否真的跑在不同线程上。
std::string currentThreadId()
{
    std::ostringstream oss;
    oss << std::this_thread::get_id();
    return oss.str();
}

// 构造一个简化版音频帧，用于模拟 HJMedia 里带 pts/generation 的媒体数据。
hjstudy::Frame audioFrame(int64_t ptsMs, int generation, std::string payload)
{
    return hjstudy::Frame{ptsMs, hjstudy::MediaType::Audio, false, generation, std::move(payload)};
}

// 用 control 帧 + payload=eof 模拟链路里的 EOF 控制帧。
hjstudy::Frame eofFrame(int generation)
{
    return hjstudy::Frame{-1, hjstudy::MediaType::Control, false, generation, "eof"};
}

// ---------------------------------------------------------------------------
// Section 1: deliver / runTask decoupling.
// ---------------------------------------------------------------------------

class ReviewPlugin : public std::enable_shared_from_this<ReviewPlugin> {
public:
    explicit ReviewPlugin(std::string name)
        : name_(std::move(name))
        , worker_(std::make_unique<hjstudy::TaskRunner>())
    {
    }

    ~ReviewPlugin()
    {
        worker_->stop();
    }

    void deliver(hjstudy::Frame frame)
    {
        std::size_t queueSize = 0;
        {
            // 模拟上游线程只做“入队”动作，不在这里直接处理帧。
            std::lock_guard<std::mutex> lock(mutex_);
            input_.push_back(std::move(frame));
            queueSize = input_.size();
        }

        hjstudy::logFields(
            name_,
            "deliver",
            {{"queueSize", std::to_string(queueSize)}, {"threadId", currentThreadId()}});

        postTask();
    }

private:
    void postTask()
    {
        // scheduled_ 模拟 latest-only 的 runTask 调度信号：
        // 队列里已经有一个待执行 runTask 时，不再重复投递。
        if (scheduled_.exchange(true)) {
            hjstudy::logLine(name_, "postTask coalesced: runTask is already scheduled");
            return;
        }

        std::weak_ptr<ReviewPlugin> weakSelf = shared_from_this();
        worker_->post([weakSelf] {
            if (auto self = weakSelf.lock()) {
                self->runTask();
            }
        });
    }

    void runTask()
    {
        int consumed = 0;
        while (true) {
            hjstudy::Frame frame;
            std::size_t queueLeft = 0;
            {
                // runTask 在 worker 线程里串行取帧并处理。
                std::lock_guard<std::mutex> lock(mutex_);
                if (input_.empty()) {
                    break;
                }
                frame = std::move(input_.front());
                input_.pop_front();
                queueLeft = input_.size();
            }

            ++consumed;
            hjstudy::logFields(
                name_,
                "receive",
                {{"ptsMs", std::to_string(frame.ptsMs)},
                 {"queueLeft", std::to_string(queueLeft)},
                 {"threadId", currentThreadId()}});
        }

        scheduled_ = false;

        bool hasMore = false;
        {
            // 如果 runTask 结束后发现期间又来了新帧，再补投递一次。
            std::lock_guard<std::mutex> lock(mutex_);
            hasMore = !input_.empty();
        }
        if (hasMore) {
            postTask();
        }

        hjstudy::logFields(name_, "runTask done", {{"consumed", std::to_string(consumed)}});
    }

    std::string name_;
    std::mutex mutex_;
    std::deque<hjstudy::Frame> input_;
    std::unique_ptr<hjstudy::TaskRunner> worker_;
    std::atomic<bool> scheduled_{false};
};

void runDeliverRunTaskReview()
{
    hjstudy::printTitle("Section 1: deliver / runTask decoupling");
    hjstudy::logLine("point", "deliver only enqueues frames; runTask consumes them on the worker thread");

    // 连续 deliver 三帧，观察日志中：
    // 1. deliver 在线程 A
    // 2. receive 在线程 B
    // 3. runTask 调度只保留一份
    auto decoder = std::make_shared<ReviewPlugin>("decoder");
    decoder->deliver(audioFrame(1000, 0, "frame-A"));
    decoder->deliver(audioFrame(1020, 0, "frame-B"));
    decoder->deliver(audioFrame(1040, 0, "frame-C"));

    std::this_thread::sleep_for(80ms);
}

// ---------------------------------------------------------------------------
// Section 2: latest-only scheduling.
// ---------------------------------------------------------------------------

class LatestOnlyHandler {
public:
    void asyncAndClear(int id, std::string label, std::function<void()> task)
    {
        const auto before = tasks_.size();
        // 模拟 HJLooperThread::Handler::asyncAndClear：
        // 先删旧消息，再塞入新的同类请求。
        tasks_.erase(
            std::remove_if(tasks_.begin(), tasks_.end(), [id](const Task& item) {
                return item.id == id;
            }),
            tasks_.end());

        hjstudy::logFields(
            "handler",
            "asyncAndClear",
            {{"cleared", std::to_string(before - tasks_.size())}, {"id", std::to_string(id)}, {"label", label}});

        tasks_.push_back(Task{id, std::move(label), std::move(task)});
    }

    void runAll()
    {
        // 顺序执行队列中保留下来的请求。
        while (!tasks_.empty()) {
            auto task = std::move(tasks_.front());
            tasks_.erase(tasks_.begin());
            hjstudy::logFields("handler", "dispatch", {{"id", std::to_string(task.id)}, {"label", task.label}});
            task.callback();
        }
    }

private:
    struct Task {
        int id{};
        std::string label;
        std::function<void()> callback;
    };

    std::vector<Task> tasks_;
};

void runLatestOnlyReview()
{
    hjstudy::printTitle("Section 2: latest-only seek");

    constexpr int seekMessageId = 42;
    LatestOnlyHandler handler;

    // 连续 3 次 seek，只希望最后一次生效。
    for (const int64_t target : {1000, 2000, 5000}) {
        handler.asyncAndClear(seekMessageId, "seek(" + std::to_string(target) + ")", [target] {
            hjstudy::logFields("demuxer", "seek", {{"targetPtsMs", std::to_string(target)}});
        });
    }

    hjstudy::logLine("point", "three requests were posted, but only the last seek should execute");
    handler.runAll();
}

// ---------------------------------------------------------------------------
// Section 3: weak_ptr teardown.
// ---------------------------------------------------------------------------

class PlayerSession : public std::enable_shared_from_this<PlayerSession> {
public:
    explicit PlayerSession(std::string name)
        : name_(std::move(name))
    {
        hjstudy::logLine(name_, "created");
    }

    ~PlayerSession()
    {
        hjstudy::logLine(name_, "destroyed");
    }

    void seek(int64_t targetPtsMs)
    {
        hjstudy::logFields(name_, "seek", {{"targetPtsMs", std::to_string(targetPtsMs)}});
    }

private:
    std::string name_;
};

void runWeakPtrReview()
{
    hjstudy::printTitle("Section 3: weak_ptr teardown");

    hjstudy::TaskRunner handler;
    {
        auto session = std::make_shared<PlayerSession>("music-player");
        std::weak_ptr<PlayerSession> weakSession = session;

        // 延迟任务里只持有 weak_ptr。
        // 如果对象先析构，旧任务到期后会自动跳过。
        handler.postDelayed(30ms, [weakSession] {
            if (auto locked = weakSession.lock()) {
                locked->seek(5000);
            } else {
                hjstudy::logLine("handler", "old delayed seek skipped because session is gone");
            }
        });

        session.reset();
    }

    std::this_thread::sleep_for(70ms);
    handler.stop();
}

// ---------------------------------------------------------------------------
// Section 4: seek protections.
// ---------------------------------------------------------------------------

struct Stage {
    std::string name;
    // queue 模拟各处理节点持有的输入缓存/待处理帧。
    std::deque<hjstudy::Frame> queue;
    bool eof{false};
    // preFlush 只对 render 有意义：seek 期间先暂停旧帧消费。
    bool preFlush{false};
};

struct SeekProtection {
    // 这几个开关分别代表 seek 修复方案里的不同保护措施。
    bool preFlushRender{};
    bool flushQueues{};
    bool resetEof{};
    bool generationGate{};
};

void flushStage(Stage& stage)
{
    const auto before = stage.queue.size();
    // 模拟 flush：清队列、清 EOF。
    stage.queue.clear();
    stage.eof = false;
    hjstudy::logFields(
        stage.name,
        "flush",
        {{"queueBefore", std::to_string(before)}, {"queueAfter", "0"}, {"eof", "false"}});
}

void moveAll(Stage& from, Stage& to)
{
    // 简化版链路传递：把上一阶段的所有帧转交给下一阶段。
    while (!from.queue.empty()) {
        to.queue.push_back(std::move(from.queue.front()));
        from.queue.pop_front();
    }
}

void runSeekCase(const std::string& label, SeekProtection protection)
{
    hjstudy::printTitle(label);

    constexpr int64_t targetPtsMs = 5000;
    int generation = 0;
    int renderedOldFrames = 0;
    int renderedNewFrames = 0;
    int droppedStaleFrames = 0;

    Stage demuxer{"demuxer"};
    Stage decoder{"decoder"};
    Stage render{"render"};

    // 先造一个 seek 前的“脏现场”：
    // demuxer 已经 EOF，下游还残留旧帧和旧 EOF。
    demuxer.eof = true;
    decoder.queue.push_back(audioFrame(980, 0, "old-decoder-cache"));
    render.queue.push_back(audioFrame(1000, 0, "old-render-frame"));
    render.queue.push_back(eofFrame(0));

    hjstudy::logFields(
        label,
        "before-seek",
        {{"decoderQueue", std::to_string(decoder.queue.size())},
         {"demuxerEof", hjstudy::yesNo(demuxer.eof)},
         {"renderQueue", std::to_string(render.queue.size())}});

    if (protection.preFlushRender) {
        render.preFlush = true;
        hjstudy::logLine("render", "setPreFlush(true)");
    }

    if (protection.flushQueues) {
        // 正确 seek 需要把旧链路上的缓存都清掉。
        flushStage(demuxer);
        flushStage(decoder);
        flushStage(render);
    } else {
        hjstudy::logLine("flush", "missing flush: old queues remain visible");
    }

    if (protection.resetEof) {
        // 旧播放轮次的 EOF 不能污染新一轮播放。
        demuxer.eof = false;
        decoder.eof = false;
        render.eof = false;
        hjstudy::logLine("eof", "reset EOF flags for the new playback generation");
    } else if (demuxer.eof) {
        hjstudy::logLine("eof", "old demuxer EOF is still set");
    }

    ++generation;
    render.preFlush = false;

    // seek 成功后，从新位置重新吐出新帧。
    demuxer.queue.push_back(audioFrame(5000, generation, "new-frame"));
    demuxer.queue.push_back(audioFrame(5020, generation, "new-frame"));
    demuxer.queue.push_back(eofFrame(generation));
    moveAll(demuxer, decoder);
    moveAll(decoder, render);

    while (!render.queue.empty()) {
        auto frame = std::move(render.queue.front());
        render.queue.pop_front();

        if (protection.generationGate && frame.generation != generation) {
            // generation gate 用来隔离旧 seek 轮次晚到的残留帧。
            ++droppedStaleFrames;
            hjstudy::logFields(
                "render",
                "drop-stale",
                {{"frameGeneration", std::to_string(frame.generation)}, {"ptsMs", std::to_string(frame.ptsMs)}});
            continue;
        }

        if (frame.payload == "eof") {
            // broken 场景里，旧 EOF 可能在新帧前被消费，导致提前结束。
            render.eof = true;
            hjstudy::logFields(
                "render",
                "eof",
                {{"frameGeneration", std::to_string(frame.generation)},
                 {"remainingQueue", std::to_string(render.queue.size())}});
            break;
        }

        if (frame.ptsMs < targetPtsMs) {
            // seek 到 5000ms 后还渲染出 5000ms 前的帧，就说明旧状态泄漏了。
            ++renderedOldFrames;
            hjstudy::logFields(
                "render",
                "render-old-after-seek",
                {{"ptsMs", std::to_string(frame.ptsMs)}, {"targetPtsMs", std::to_string(targetPtsMs)}});
        } else {
            ++renderedNewFrames;
            hjstudy::logFields("render", "render-new", {{"ptsMs", std::to_string(frame.ptsMs)}});
        }
    }

    hjstudy::logFields(
        label,
        "summary",
        {{"droppedStaleFrames", std::to_string(droppedStaleFrames)},
         {"renderedNewFrames", std::to_string(renderedNewFrames)},
         {"renderedOldFrames", std::to_string(renderedOldFrames)},
         {"renderQueueLeft", std::to_string(render.queue.size())}});
}

void runSeekProtectionReview()
{
    hjstudy::printTitle("Section 4: seek protections");
    hjstudy::logLine("point", "fixed seek combines preFlush, downstream flush, EOF reset, and generation gate");

    // broken-seek：复现问题。
    runSeekCase("broken-seek", SeekProtection{false, false, false, false});
    // fixed-seek：打开所有保护后观察问题消失。
    runSeekCase("fixed-seek", SeekProtection{true, true, true, true});
}

// ---------------------------------------------------------------------------
// Section 5: player comparison.
// ---------------------------------------------------------------------------

struct PlayerTrait {
    std::string name;
    std::string scene;
    bool seekable{};
    bool lowLatency{};
    bool dropping{};
    std::string eofBehavior;
};

void runPlayerComparisonReview()
{
    hjstudy::printTitle("Section 5: player comparison");

    // 这一段不是模拟执行链路，而是复盘三类播放器的产品目标差异。
    const std::vector<PlayerTrait> players = {
        {"LivePlayer", "live audio/video stream", false, true, true,
         "network or demuxer EOF tends to reset/reconnect instead of ending the session"},
        {"VodPlayer", "seekable audio/video file", true, false, false,
         "demuxer EOF waits for audio and video render EOF before graph EOF"},
        {"MusicPlayer", "pure audio playback", true, false, false,
         "demuxer EOF depends on repeat; final audio render EOF means playback is done"},
    };

    for (const auto& player : players) {
        hjstudy::logFields(
            "player",
            player.name,
            {{"dropping", hjstudy::yesNo(player.dropping)},
             {"eof", player.eofBehavior},
             {"lowLatency", hjstudy::yesNo(player.lowLatency)},
             {"scene", player.scene},
             {"seekable", hjstudy::yesNo(player.seekable)}});
    }

    hjstudy::logLine("takeaway", "live favors latency; VOD favors completeness; music favors audio continuity and repeat/EOF semantics");
}

} // namespace

int main()
{
    // 运行顺序按“线程模型 -> 调度去重 -> 生命周期 -> seek 修复 -> 产品差异”组织，
    // 对应第 2 周复盘里的主线。
    hjstudy::printReferences(
        "study/week2-thread-plugin-player-practice.md Day 14",
        "studyNote/week2-review.md",
        {
            "src/utils/HJThread/doc/README.md",
            "src/plugins/doc/HJPlugin.md",
            "src/plugins/doc/HJMediaFrameDeque.md",
            "src/graphs/HJGraphLivePlayer.cpp",
            "src/graphs/HJGraphVodPlayer.cpp",
            "src/graphs/HJGraphMusicPlayer.cpp",
            "src/core/HJMediaPlayer.cc",
            "src/core/HJMediaNode.cc",
        });

    runDeliverRunTaskReview();
    runLatestOnlyReview();
    runWeakPtrReview();
    runSeekProtectionReview();
    runPlayerComparisonReview();

    hjstudy::printTitle("Week 2 review complete");
    hjstudy::logLine("note", "read studyNote/week2-review.md for the 15 Q&A and 5-minute talk script");
    return 0;
}
