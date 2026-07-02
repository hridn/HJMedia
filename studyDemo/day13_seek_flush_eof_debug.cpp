/*
 * Day 13: seek / flush / EOF debugging practice.
 *
 * Study plan: study/week2-thread-plugin-player-practice.md
 * Study note: studyNote/13-seek-flush-eof-debug.md
 *
 * HJMedia reference source:
 * - src/core/HJMediaPlayer.cc
 * - src/core/HJNodeDemuxer.cc
 * - src/core/HJNodeVRender.cc
 * - src/core/HJNodeARender.cc
 * - src/core/HJMediaNode.cc
 * - src/graphs/HJGraphMusicPlayer.cpp
 */

#include "study_demo_common.h"

namespace {

// 一个 StageState 对应真实播放器链路里的一个处理节点。
//
// 在 HJMedia 里，demuxer、decoder、resampler、render 都可能持有自己的
// 输入队列、内部缓存和 EOF 状态。seek 时如果只让 demuxer 定位到新时间点，
// 但没有清理下游队列，旧帧仍然可能继续流到 render。
struct StageState {
    std::string name;                 // 日志里显示的节点名，例如 demuxer / decoder / render。
    std::deque<hjstudy::Frame> queue; // 简化版输入队列，模拟 HJMediaFrameDeque / 节点侧缓存。
    bool eof{false};                  // 模拟节点是否已经进入 EOF 状态。
    bool preFlush{false};             // 只对 render 有意义：seek flush 到达前先暂停消费旧帧。
};

// 同一个 demo 通过不同 SeekOptions 同时模拟“错误实现”和“修复实现”。
// broken-seek 会把这些开关全部关掉，fixed-seek 会全部打开。
struct SeekOptions {
    bool preFlushRender{}; // 是否在 seek 开始时先让 render 停止消费旧帧。
    bool flushAllStages{}; // 是否把 flush 从 demuxer 一路传到 decoder/resampler/render。
    bool resetEofFlags{};  // 是否清掉旧一轮播放留下的 EOF 状态。
    bool resetTimeline{};  // 是否把播放时钟/timeline 切到 seek 后的新基准。
    bool generationGate{}; // 是否用 generation 过滤旧 seek 轮次残留的帧和 EOF。
};

hjstudy::Frame makeAudioFrame(int64_t ptsMs, int generation, std::string payload)
{
    // keyFrame 只对视频丢帧策略有意义，本 demo 聚焦 seek/flush/EOF，所以固定为 false。
    return hjstudy::Frame{ptsMs, hjstudy::MediaType::Audio, false, generation, std::move(payload)};
}

hjstudy::Frame makeEofFrame(int generation)
{
    // 用 Control + payload=eof 模拟 EOF 控制帧。
    // 真实 HJMedia 里 EOF 帧会沿插件/节点链路向下游传播，render 消费后才代表最终播放结束。
    return hjstudy::Frame{-1, hjstudy::MediaType::Control, false, generation, "eof"};
}

std::string frameKind(const hjstudy::Frame& frame)
{
    if (frame.payload == "eof") {
        return "eof";
    }
    return hjstudy::toString(frame.type);
}

class SeekDebugPipeline {
public:
    SeekDebugPipeline(std::string label, SeekOptions options)
        : label_(std::move(label))
        , options_(options)
        , demuxer_{"demuxer"}
        , decoder_{"decoder"}
        , resampler_{"resampler"}
        , render_{"render"}
    {
    }

    void run()
    {
        // 完整执行一次 seek 场景：
        // 1. 先制造 seek 前已经积压的旧帧和旧 EOF。
        // 2. 执行 seek。
        // 3. demuxer 产生新位置的新帧。
        // 4. render 消费队列，观察旧帧/旧 EOF 是否泄漏。
        hjstudy::printTitle(label_);
        loadBufferedStateBeforeSeek();
        seekTo(5000);
        deliverNewFrames();
        renderUntilBlockedOrEmpty();
        printResult();
    }

private:
    void loadBufferedStateBeforeSeek()
    {
        // 模拟用户 seek 前的状态：
        // - demuxer 已经读到文件尾，留下 EOF 标志。
        // - decoder/resampler/render 里还有旧时间点的缓存。
        // - render 队列里还放着一个旧 EOF 控制帧。
        //
        // 这正是 seek bug 常见的危险场景：上游切到了新位置，但下游还排着旧数据。
        demuxer_.eof = true;
        decoder_.queue.push_back(makeAudioFrame(960, 0, "old-decoder-cache"));
        resampler_.queue.push_back(makeAudioFrame(980, 0, "old-resampler-cache"));
        render_.queue.push_back(makeAudioFrame(1000, 0, "old-render-frame"));
        render_.queue.push_back(makeAudioFrame(1020, 0, "old-render-frame"));
        render_.queue.push_back(makeEofFrame(0));

        hjstudy::logFields(
            label_,
            "before-seek",
            {
                {"decoderQueue", std::to_string(decoder_.queue.size())},
                {"demuxerEof", hjstudy::yesNo(demuxer_.eof)},
                {"renderQueue", std::to_string(render_.queue.size())},
                {"resamplerQueue", std::to_string(resampler_.queue.size())},
            });
    }

    void seekTo(int64_t targetPtsMs)
    {
        // targetPtsMs 对应播放器 API 的 seek 目标时间。
        // HJMedia 中点播播放器会创建 HJSeekInfo，demuxer 根据目标时间调用 source->seek(pos)。
        targetPtsMs_ = targetPtsMs;
        hjstudy::logFields(label_, "seek-request", {{"targetPtsMs", std::to_string(targetPtsMs)}});

        if (options_.preFlushRender) {
            // 对应 HJMediaPlayer::seek() 里先调用 render->setPreFlush(true)。
            // 这样做的原因是：seek 请求发出后，真正的 flush 可能还没跑到 render，
            // 如果 render 此时继续消费队列，就会把 seek 前的旧帧播出去。
            render_.preFlush = true;
            hjstudy::logLine("render", "setPreFlush(true): stop consuming old queued frames before flush arrives");
        } else {
            // broken-seek 故意不设置 preFlush，用来复现“seek 后旧帧仍被播放”。
            hjstudy::logLine("render", "missing setPreFlush(true): render may keep consuming old frames during seek");
        }

        if (options_.flushAllStages) {
            // 对应 HJNodeDemuxer::seek() 成功后调用 HJMediaNode::flush(info)，
            // 再由 HJMediaNode::flush() 递归向后继节点传播。
            //
            // 注意：真实项目里 flush 不只清 queue，还可能重置 codec、audio fifo、
            // render device buffer、timeline 相关状态。本 demo 用 queue.clear() 简化表达。
            flushStage(demuxer_);
            flushStage(decoder_);
            flushStage(resampler_);
            flushStage(render_);
        } else {
            // broken-seek 故意不 flush，因此旧帧和旧 EOF 会继续留在下游队列里。
            hjstudy::logLine("flush", "missing flush: old decoder/resampler/render queues are still visible downstream");
        }

        if (options_.resetEofFlags) {
            // seek 是新一轮播放语义。旧 EOF 只能说明“上一轮读到了结尾”，
            // 不能继续影响 seek 后的新位置，否则会出现刚 seek 完就播放完成的假 EOF。
            demuxer_.eof = false;
            decoder_.eof = false;
            resampler_.eof = false;
            render_.eof = false;
            hjstudy::logLine("eof", "reset EOF flags for the new playback generation");
        } else if (demuxer_.eof) {
            // 这条日志说明旧 EOF 状态泄漏到了 seek 后。
            hjstudy::logLine("eof", "demuxer EOF flag is still set after seek");
        }

        if (options_.resetTimeline) {
            // 对应播放器 seek 后重置播放时钟。否则新帧 pts 虽然正确，
            // 但 UI 进度、A/V 同步、首帧判断、EOF 后 timestamp 都可能异常。
            timelineBasePtsMs_ = targetPtsMs;
            hjstudy::logFields("timeline", "reset", {{"basePtsMs", std::to_string(timelineBasePtsMs_)}});
        } else {
            hjstudy::logFields("timeline", "not-reset", {{"basePtsMs", std::to_string(timelineBasePtsMs_)}});
        }

        ++generation_;
        // generation 是 demo 里的“播放轮次号”。真实项目中可以用 seek id、
        // stream index、demuxer generation 或类似字段达到同样目的。
        //
        // 每次 seek 后 generation 增加，render 只接受当前 generation 的帧，
        // 就能避免旧异步任务或旧 EOF 晚到后误伤新播放轮次。
        render_.preFlush = false;
        hjstudy::logFields(
            label_,
            "seek-finished",
            {{"generation", std::to_string(generation_)}, {"targetPtsMs", std::to_string(targetPtsMs_)}});
    }

    void flushStage(StageState& stage)
    {
        // flush 的最小可观察行为：
        // - 队列从 before 变成 0。
        // - EOF 标志被清掉。
        //
        // 排查真实问题时，日志里一定要打印 queue size before/after 和 EOF before/after。
        // 如果某个阶段 flush 后队列仍不为空，旧帧大概率就会从这里继续泄漏。
        const auto before = stage.queue.size();
        stage.queue.clear();
        stage.eof = false;
        hjstudy::logFields(
            stage.name,
            "flush",
            {
                {"eof", hjstudy::yesNo(stage.eof)},
                {"queueAfter", std::to_string(stage.queue.size())},
                {"queueBefore", std::to_string(before)},
            });
    }

    void deliverNewFrames()
    {
        // 模拟 seek 成功后，demuxer 从 5000ms 附近开始重新输出数据。
        // fixed-seek 中，下游旧队列已经清空，所以 render 最终只会看到这些新帧。
        // broken-seek 中，新帧会排在旧 render 队列之后，甚至可能被旧 EOF 截断。
        demuxer_.queue.push_back(makeAudioFrame(5000, generation_, "new-demuxed-frame"));
        demuxer_.queue.push_back(makeAudioFrame(5020, generation_, "new-demuxed-frame"));
        demuxer_.queue.push_back(makeEofFrame(generation_));

        moveAll(demuxer_, decoder_, "demux->decode");
        moveAll(decoder_, resampler_, "decode->resample");
        moveAll(resampler_, render_, "resample->render");
    }

    void moveAll(StageState& from, StageState& to, std::string_view route)
    {
        // 简化版插件链路转发：
        // demuxer -> decoder -> resampler -> render。
        //
        // 真实 HJMedia 中对应 deliver()/receive()/runTask()/deliverToOutputs()
        // 这一整套异步链路。本 demo 不模拟线程，只保留队列顺序和控制帧传播。
        while (!from.queue.empty()) {
            auto frame = std::move(from.queue.front());
            from.queue.pop_front();
            const auto kind = frameKind(frame);
            to.queue.push_back(std::move(frame));
            hjstudy::logFields(
                route,
                "deliver",
                {
                    {"kind", kind},
                    {"toQueue", std::to_string(to.queue.size())},
                });
        }
    }

    void renderUntilBlockedOrEmpty()
    {
        if (render_.preFlush) {
            // 如果 seek 过程中 render 仍处于 preFlush，正确行为是不消费任何旧队列。
            // 真实项目里如果 flush 或 resume 丢失，也可能导致 render 永远卡在这里。
            hjstudy::logLine("render", "preFlush is still true; render waits instead of consuming queued frames");
            return;
        }

        while (!render_.queue.empty()) {
            auto frame = std::move(render_.queue.front());
            render_.queue.pop_front();

            if (options_.generationGate && frame.generation != generation_) {
                // generation gate 是最后一道保护：
                // 即使某个旧帧因为异步乱序绕过了 flush，只要它的 generation 不是当前值，
                // render / EOF 处理也会丢弃它。
                ++droppedStaleFrames_;
                hjstudy::logFields(
                    "render",
                    "drop-stale",
                    {
                        {"currentGeneration", std::to_string(generation_)},
                        {"frameGeneration", std::to_string(frame.generation)},
                        {"kind", frameKind(frame)},
                        {"ptsMs", std::to_string(frame.ptsMs)},
                    });
                continue;
            }

            if (frame.payload == "eof") {
                // EOF 必须和当前播放轮次匹配才应该被接受。
                // broken-seek 不做 generation gate，所以旧 EOF 会在新帧前被消费，
                // 导致 remainingQueue 里明明还有新帧，render 却已经认为播放结束。
                render_.eof = true;
                hjstudy::logFields(
                    "render",
                    "eof",
                    {
                        {"currentGeneration", std::to_string(generation_)},
                        {"frameGeneration", std::to_string(frame.generation)},
                        {"remainingQueue", std::to_string(render_.queue.size())},
                    });
                break;
            }

            // seek 后仍渲染 targetPtsMs 之前的帧，就是本 demo 要复现的核心问题。
            const bool oldFrameAfterSeek = frame.ptsMs < targetPtsMs_;
            if (oldFrameAfterSeek) {
                ++renderedOldFrames_;
            }
            ++renderedFrames_;
            hjstudy::logFields(
                "render",
                oldFrameAfterSeek ? "render-old-frame-after-seek" : "render",
                {
                    {"generation", std::to_string(frame.generation)},
                    {"payload", frame.payload},
                    {"ptsMs", std::to_string(frame.ptsMs)},
                    {"timelineBasePtsMs", std::to_string(timelineBasePtsMs_)},
                });
        }
    }

    void printResult() const
    {
        // 最终验收重点：
        // - broken-seek 的 renderedOldFrames 应该大于 0。
        // - fixed-seek 的 renderedOldFrames 应该为 0。
        // - broken-seek 会因为旧 EOF 导致 renderQueueLeft 仍有新帧未消费。
        hjstudy::logFields(
            label_,
            "summary",
            {
                {"droppedStaleFrames", std::to_string(droppedStaleFrames_)},
                {"renderEof", hjstudy::yesNo(render_.eof)},
                {"renderQueueLeft", std::to_string(render_.queue.size())},
                {"renderedFrames", std::to_string(renderedFrames_)},
                {"renderedOldFrames", std::to_string(renderedOldFrames_)},
            });
    }

    std::string label_;       // 当前场景名：broken-seek 或 fixed-seek。
    SeekOptions options_;     // 控制本次 seek 是否启用各类保护措施。
    StageState demuxer_;      // 模拟解复用节点。
    StageState decoder_;      // 模拟解码节点。
    StageState resampler_;    // 模拟音频重采样 / FIFO 节点。
    StageState render_;       // 模拟最终消费音频/视频帧的渲染节点。
    int generation_{0};       // 当前 seek / 播放轮次号。
    int64_t targetPtsMs_{0};  // 本次 seek 目标时间。
    int64_t timelineBasePtsMs_{0}; // 播放时钟基准，fixed 场景会重置为 targetPtsMs。
    int renderedFrames_{0};       // render 实际消费的普通媒体帧数量。
    int renderedOldFrames_{0};    // seek 后仍被渲染的旧帧数量，这是 bug 指标。
    int droppedStaleFrames_{0};   // 被 generation gate 丢弃的旧帧/旧控制帧数量。
};

void printDebugChecklist()
{
    // 这部分不是模拟逻辑，而是把排查真实 HJMedia 问题时要补的日志点打印出来。
    // 运行 demo 时先看这个 checklist，再看 broken/fixed 输出，会更容易建立排查顺序。
    hjstudy::printTitle("debug checklist");
    hjstudy::logLine("symptom", "after seek(5000), audio/video from around 1000ms is still rendered");
    hjstudy::logLine("suspects", "demuxer, decoder, resampler, render, timeline, queue, EOF flags, delayed tasks");
    hjstudy::logLine("log-point", "seek request: seek id, target pts, thread id, latest-only message id");
    hjstudy::logLine("log-point", "flush: stage name, queue size before/after, EOF before/after, generation");
    hjstudy::logLine("log-point", "render: preFlush flag, frame pts, frame generation, timeline base, queue size");
    hjstudy::logLine("log-point", "EOF: producer generation, stream index, pending final EOF, downstream ret");
}

} // namespace

int main()
{
    hjstudy::printReferences(
        "study/week2-thread-plugin-player-practice.md Day 13",
        "studyNote/13-seek-flush-eof-debug.md",
        {
            "src/core/HJMediaPlayer.cc",
            "src/core/HJNodeDemuxer.cc",
            "src/core/HJNodeVRender.cc",
            "src/core/HJNodeARender.cc",
            "src/core/HJMediaNode.cc",
            "src/graphs/HJGraphMusicPlayer.cpp",
        });

    printDebugChecklist();

    // 错误场景：
    // - render 不进入 preFlush，seek 期间仍可能消费旧帧。
    // - demuxer/decoder/resampler/render 的旧队列都不清。
    // - EOF 和 timeline 没有切换到新播放轮次。
    // - 不用 generation gate 过滤旧帧。
    //
    // 运行结果中会看到 render-old-frame-after-seek，并且旧 EOF 会提前截断新帧。
    SeekDebugPipeline broken(
        "broken-seek",
        SeekOptions{
            false,
            false,
            false,
            false,
            false,
        });
    broken.run();

    // 修复场景：
    // - seek 开始先 preFlush render，防止它继续播放旧队列。
    // - seek 成功后 flush 全链路，清空旧缓存。
    // - 重置 EOF 和 timeline。
    // - 用 generation gate 防止旧异步帧或旧 EOF 晚到。
    //
    // 运行结果中 renderedOldFrames 应为 0，render 只消费 5000ms 之后的新帧。
    SeekDebugPipeline fixed(
        "fixed-seek",
        SeekOptions{
            true,
            true,
            true,
            true,
            true,
        });
    fixed.run();
}
