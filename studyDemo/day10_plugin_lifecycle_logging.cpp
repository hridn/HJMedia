/*
 * Day 10: Plugin lifecycle and logging practice.
 *
 * Study plan: study/week2-thread-plugin-player-practice.md
 * Study note:
 * - studyNote/10-plugin-lifecycle-log-design.md
 * - studyNote/week2-thread-plugin-player-notes.md Day 10
 *
 * HJMedia reference source:
 * - src/plugins/doc/HJPlugin.md
 * - src/plugins/doc/HJPluginCodec.md
 * - src/plugins/doc/HJMediaFrameDeque.md
 */

#include "study_demo_common.h"

#include <memory>

namespace {

// 这个状态枚举只保留学习 demo 需要的核心状态，用来模拟 HJPlugin/HJSyncObject 的生命周期。
enum class PluginStatus {
    None,
    Inited,
    Ready,
    Running,
    Paused,
    EOFReached,
    Done,
};

// 控制帧用于模拟真实媒体链路里的 flush/eof 语义。
// 普通音视频帧走处理路径，控制帧走清队列或结束传播路径。
enum class ControlFrame {
    None,
    Flush,
    EOFFrame,
};

// demo 中的简化帧结构。
// route 字段用于观察一帧经过了哪些插件，方便定位链路在哪个节点中断。
struct ProbeFrame {
    int64_t ptsMs{-1};
    hjstudy::MediaType mediaType{hjstudy::MediaType::Audio};
    ControlFrame control{ControlFrame::None};
    std::string route;
};

std::string toString(PluginStatus status)
{
    switch (status) {
    case PluginStatus::None:
        return "None";
    case PluginStatus::Inited:
        return "Inited";
    case PluginStatus::Ready:
        return "Ready";
    case PluginStatus::Running:
        return "Running";
    case PluginStatus::Paused:
        return "Paused";
    case PluginStatus::EOFReached:
        return "EOF";
    case PluginStatus::Done:
        return "Done";
    }
    return "Unknown";
}

std::string toString(ControlFrame control)
{
    switch (control) {
    case ControlFrame::None:
        return "media";
    case ControlFrame::Flush:
        return "flush";
    case ControlFrame::EOFFrame:
        return "eof";
    }
    return "unknown";
}

std::string threadId()
{
    std::ostringstream out;
    out << std::this_thread::get_id();
    return out.str();
}

// PluginProbe 是 HJPlugin 的最小模型：
// - inputQueue_ 对应消费者插件持有的 HJMediaFrameDeque；
// - outputs_ 对应 HJPlugin::m_outputs；
// - deliver/receive/runTask/deliverToOutputs 对应真实插件链路的关键日志点。
class PluginProbe {
public:
    explicit PluginProbe(std::string name)
        : name_(std::move(name))
    {
        log("create");
    }

    void init()
    {
        status_ = PluginStatus::Inited;
        log("init");
    }

    void addOutput(PluginProbe& output)
    {
        // 真实 HJPlugin::addOutputPlugin 会记录下游插件的 weak_ptr、媒体类型和 track id。
        // demo 只保留下游对象地址，重点观察转发行为。
        outputs_.push_back(&output);
        log("addOutput", {{"output", output.name_}});
    }

    void deliver(std::string_view srcName, const ProbeFrame& frame)
    {
        // deliver 是上游调用下游的入口：上游不直接处理下游逻辑，只把帧放入下游输入队列。
        const auto before = inputQueue_.size();
        inputQueue_.push_back(frame);
        log(
            "deliver",
            {
                {"src", std::string(srcName)},
                {"frame", toString(frame.control)},
                {"ptsMs", std::to_string(frame.ptsMs)},
                {"queueBefore", std::to_string(before)},
                {"queueAfter", std::to_string(inputQueue_.size())},
            });
    }

    void setPause(bool pause)
    {
        paused_ = pause;
        status_ = pause ? PluginStatus::Paused : PluginStatus::Ready;
        log(pause ? "pause" : "resume");
    }

    void flush(std::string_view srcName)
    {
        // 真实 HJMediaFrameDeque::flush(true) 会清空队列并保留 clear frame 语义。
        // 这里用 Flush 控制帧模拟“旧帧已清理，需要继续通知下游”。
        const auto before = inputQueue_.size();
        inputQueue_.clear();
        inputQueue_.push_back(ProbeFrame{-1, hjstudy::MediaType::Control, ControlFrame::Flush, "flush"});
        status_ = PluginStatus::Ready;
        log(
            "flush",
            {
                {"src", std::string(srcName)},
                {"queueBefore", std::to_string(before)},
                {"queueAfter", std::to_string(inputQueue_.size())},
            });
    }

    void runTask()
    {
        // runTask 在真实工程中通常由 handler 线程调度。
        // demo 里手动调用它，重点观察状态、队列和帧流动。
        if (paused_) {
            log("runTask.skip", {{"reason", "paused"}});
            return;
        }

        if (inputQueue_.empty()) {
            log("runTask.idle", {{"delayMs", "20"}});
            return;
        }

        status_ = PluginStatus::Running;
        auto frame = receive();

        if (frame.control == ControlFrame::Flush) {
            // flush 不产生普通输出帧，而是继续向下游传播清理动作。
            log("runFlush");
            for (auto* output : outputs_) {
                output->flush(name_);
                output->runTask();
            }
            status_ = PluginStatus::Ready;
            return;
        }

        if (frame.control == ControlFrame::EOFFrame) {
            // EOF 需要沿插件链路继续向下游传播，否则渲染端/上层可能永远等不到结束。
            status_ = PluginStatus::EOFReached;
            log("runTask.eof", {{"route", frame.route}});
            deliverToOutputs(frame);
            return;
        }

        // 普通媒体帧：处理完成后记录 route，再转发给下游。
        frame.route += "->" + name_;
        log(
            "runTask.process",
            {
                {"mediaType", hjstudy::toString(frame.mediaType)},
                {"ptsMs", std::to_string(frame.ptsMs)},
                {"route", frame.route},
            });
        deliverToOutputs(frame);
        status_ = PluginStatus::Ready;
    }

    void done()
    {
        inputQueue_.clear();
        status_ = PluginStatus::Done;
        log("done");
    }

private:
    ProbeFrame receive()
    {
        // receive 是消费者插件在自己的 runTask 里主动从输入队列取帧。
        // 这体现了 HJPlugin 的关键语义：输入队列归消费者管理。
        ProbeFrame frame = inputQueue_.front();
        inputQueue_.pop_front();
        log(
            "receive",
            {
                {"frame", toString(frame.control)},
                {"ptsMs", std::to_string(frame.ptsMs)},
                {"queueAfter", std::to_string(inputQueue_.size())},
            });
        return frame;
    }

    void deliverToOutputs(const ProbeFrame& frame)
    {
        // deliverToOutputs 模拟 HJPlugin::deliverToOutputs：
        // 当前插件只负责把处理后的帧推给所有下游，下游再进入自己的 deliver/runTask。
        log(
            "deliverToOutputs",
            {
                {"frame", toString(frame.control)},
                {"outputCount", std::to_string(outputs_.size())},
                {"ptsMs", std::to_string(frame.ptsMs)},
            });
        for (auto* output : outputs_) {
            output->deliver(name_, frame);
            output->runTask();
        }
    }

    void log(std::string_view action, std::map<std::string, std::string> fields = {}) const
    {
        // 每条日志都补齐定位插件链路问题时最常用的公共字段。
        fields["plugin"] = name_;
        fields["queueSize"] = std::to_string(inputQueue_.size());
        fields["status"] = toString(status_);
        fields["threadId"] = threadId();
        hjstudy::logFields(name_, action, fields);
    }

    std::string name_;
    PluginStatus status_{PluginStatus::None};
    bool paused_{false};
    std::deque<ProbeFrame> inputQueue_;
    // demo 不管理对象生命周期，所以这里使用裸指针；真实 HJPlugin 输出关系使用 weak_ptr。
    std::vector<PluginProbe*> outputs_;
};

ProbeFrame makeAudioFrame(int64_t ptsMs)
{
    return ProbeFrame{ptsMs, hjstudy::MediaType::Audio, ControlFrame::None, "demuxer"};
}

ProbeFrame makeEofFrame()
{
    return ProbeFrame{-1, hjstudy::MediaType::Control, ControlFrame::EOFFrame, "demuxer"};
}

} // namespace

int main()
{
    hjstudy::printReferences(
        "study/week2-thread-plugin-player-practice.md Day 10",
        "studyNote/10-plugin-lifecycle-log-design.md",
        {
            "src/plugins/doc/HJPlugin.md",
            "src/plugins/doc/HJPluginCodec.md",
            "src/plugins/doc/HJMediaFrameDeque.md",
        });

    PluginProbe decoder("audio-decoder");
    PluginProbe render("audio-render");

    // 建立简化链路：demuxer 作为外部上游，audio-decoder 输出到 audio-render。
    decoder.init();
    render.init();
    decoder.addOutput(render);

    // 场景 1：普通媒体帧完整经过 deliver -> receive -> process -> deliverToOutputs。
    hjstudy::printTitle("normal media flow");
    decoder.deliver("demuxer", makeAudioFrame(0));
    decoder.runTask();

    // 场景 2：暂停时允许入队，但 runTask 不消费；恢复后再继续处理旧帧。
    hjstudy::printTitle("pause keeps queued frame from being consumed");
    decoder.setPause(true);
    decoder.deliver("demuxer", makeAudioFrame(23));
    decoder.runTask();
    decoder.setPause(false);
    decoder.runTask();

    // 场景 3：flush 清掉 decoder 中的旧帧，并把清理动作继续传播到 render。
    hjstudy::printTitle("flush clears stale frames and propagates downstream");
    decoder.deliver("demuxer", makeAudioFrame(46));
    decoder.flush("demuxer");
    decoder.runTask();

    // 场景 4：EOF 从 decoder 继续传播到 render，验证结束信号不会在中间丢失。
    hjstudy::printTitle("eof propagation");
    decoder.deliver("demuxer", makeEofFrame());
    decoder.runTask();

    // 结束阶段只模拟状态和队列清理，不模拟真实 internalRelease 的线程释放。
    decoder.done();
    render.done();
}
