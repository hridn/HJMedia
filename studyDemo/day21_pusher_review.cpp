/*
 * Day 21: 第三周 Pusher / Codec / RTMP 复盘 demo。
 *
 * 这个 demo 不依赖真实 HJMedia 库，而是把第 15-20 天读到的源码语义
 * 压缩成三个可运行练习：
 * 1. 打印推流主链路和真实源码入口，帮助复述 HJGraphPusher 的组装边界。
 * 2. 输出 20 个面试问答提示，覆盖 PCM/AAC、H.264/H.265、RTMP、弱网和 RTE。
 * 3. 模拟弱网时 RTMP packet 队列堆积、低优先级视频帧丢弃、码率下调。
 *
 * 对应 HJMedia 语义：
 * - HJGraphPusher::internalInit 负责 audio/video encoder -> AVInterleave -> RTMPMuxer 的拓扑。
 * - HJPluginAVInterleave::runTask 通过 DTS 选择下一路 audio/video packet。
 * - HJRTMPPacketManager::push/drop/keepLastGop 负责缓存时长、丢帧和保留最近 GOP。
 * - HJRTMPBitrateAdapter::evaluateBitrate 根据 dropRatio 和 queueDuration 推荐码率。
 */

#include "study_demo_common.h"

#include <iomanip>

namespace {

enum class PacketType {
    Audio,
    Video,
};

struct Packet {
    PacketType type{};
    int dtsMs{};
    int ptsMs{};
    bool keyFrame{};
    int priority{}; // 数值越小越容易被丢弃；模拟 HJFLV packet priority。
    int bytes{};
    std::string label;
};

std::string packetTypeName(PacketType type)
{
    return type == PacketType::Audio ? "audio" : "video";
}

struct ReviewQA {
    std::string question;
    std::string answer;
};

struct Stage {
    std::string name;
    std::string source;
    std::string role;
};

std::vector<Stage> buildPipeline()
{
    return {
        {"Harmony API / NAPI", "src/entry/pusher/hsys/bridge/HJPusherNapi.cpp", "解析 openPreview/openPusher 参数"},
        {"Pusher entry", "src/entry/pusher/hsys/verify/HJNAPILiveStream.cpp", "创建 HJVideoInfo/HJAudioInfo/HJMediaUrl 和 listener"},
        {"Graph assembly", "src/graphs/HJGraphPusher.cpp", "组装 audio/video encoder、AVInterleave、RTMPMuxer"},
        {"Audio capture + AAC", "src/plugins/hsys/HJPluginAudioOHCapturer.cpp; src/media/codec/HJAEncFDKAAC.cc", "采集 PCM，按 AAC-LC 1024 samples/channel 编码"},
        {"Video encoder", "src/plugins/hsys/HJPluginVideoOHEncoder.cpp; src/media/codec/hsys/HJVEncOHCodec.cc", "通过 Surface 输入硬编码 H.264/H.265 packet"},
        {"AV interleave", "src/plugins/HJPluginAVInterleave.cpp", "按 audio/video DTS 合并成单路 packet 流"},
        {"RTMP mux + packet queue", "src/plugins/HJPluginRTMPMuxer.cpp; src/media/muxer/HJRTMPPacketManager.cc", "生成 FLV tag、等待关键帧、弱网丢帧"},
        {"Render / AI side graph", "src/comp/rte; src/entry/render; src/entry/inference", "Faceu/Blur/SR/Detect 通过 RTE/entry 插入视频链路"},
    };
}

std::vector<ReviewQA> buildQuestions()
{
    return {
        {"Pusher 主链路从哪里开始？", "Harmony TS 调 NAPI，HJNAPILiveStream::openPusher 构造参数后初始化 HJGraphPusher。"},
        {"HJGraphPusher 固定出口是什么？", "先创建 HJPluginAVInterleave 和 HJPluginRTMPMuxer，并连接 avInterleave -> rtmpMuxer。"},
        {"音频推流链路是什么？", "AudioOHCapturer -> AudioResampler -> FDKAACEncoder -> AVInterleave。"},
        {"视频为什么走 Surface？", "Harmony 硬编码器通过 HJVEncOHCodec::OH_VideoEncoder_GetSurface 暴露 NativeWindow 给上游渲染写入。"},
        {"PCM 字节数怎么计算？", "samplesPerChannel * channels * bytesPerSample，例如 48k stereo S16 的 1024 samples 是 4096 bytes。"},
        {"AAC-LC 一帧常见输入节奏是什么？", "每声道 1024 samples，时长由 1024 / sampleRate 决定。"},
        {"SPS/PPS/VPS 有什么用？", "它们是解码参数集，关键帧前缺失会导致接收端无法从该点初始化解码。"},
        {"IDR 和普通 P 帧区别是什么？", "IDR 是可作为解码恢复点的关键帧；P 帧依赖之前参考帧。"},
        {"AVInterleave 为什么看 DTS？", "mux/网络发送需要保持压缩码流解码顺序，PTS 更多用于显示时间。"},
        {"RTMP/FLV 时间戳如何归零？", "HJRTMPMuxer 等待起始 DTS offset，FLV tag timestamp 使用 dts - offset。"},
        {"有 B 帧时 PTS/DTS 怎么表达？", "FLV 主 timestamp 仍看 DTS，composition offset 表达 PTS - DTS。"},
        {"为什么要等关键帧再发视频？", "避免接收端从非关键帧开始，拿不到 codec header 和参考帧。"},
        {"弱网为什么不能无限缓存？", "直播目标是实时性；无限缓存会把直播变成高延迟录播，并拉高内存。"},
        {"弱网丢帧优先丢什么？", "优先丢低优先级视频帧，尽量保留最近 GOP 和关键帧恢复点。"},
        {"自动降码率在哪里触发？", "HJRTMPPacketManager 调 HJRTMPBitrateAdapter，变化后发 HJRTMP_EVENT_AUTOADJUST_BITRATE。"},
        {"降码率如何落到编码器？", "HJNAPILiveStream 收到 AUTOADJUST 后调用 HJGraphPusher::adjustBitrate。"},
        {"推流录制分支怎么加？", "openRecorder 动态创建 HJPluginFFMuxer，并连接 avInterleave -> ffMuxer。"},
        {"AI/美颜插在 Pusher 哪一段？", "通常在视频进入 encoder surface 前由 RTE/Render entry 做 GPU 后处理和 faceInfo 回灌。"},
        {"RTE 是普通 plugin 吗？", "不是，它是 texture/FBO/render graph 子图，通过 TargetEncoder、bridge、PBO 等边界与媒体图交互。"},
        {"面试时如何避免夸大？", "说清这是源码分析、架构梳理和 standalone C++ 模拟验证，不声称独立实现完整 HJMedia。"},
    };
}

std::vector<Packet> makeInterleavedPackets()
{
    std::deque<Packet> audio = {
        {PacketType::Audio, 0, 0, false, 3, 260, "A0"},
        {PacketType::Audio, 20, 20, false, 3, 250, "A20"},
        {PacketType::Audio, 40, 40, false, 3, 250, "A40"},
        {PacketType::Audio, 60, 60, false, 3, 250, "A60"},
        {PacketType::Audio, 80, 80, false, 3, 250, "A80"},
    };
    std::deque<Packet> video = {
        {PacketType::Video, 0, 0, true, 5, 1800, "IDR0"},
        {PacketType::Video, 33, 33, false, 1, 1200, "P33"},
        {PacketType::Video, 66, 66, false, 1, 1100, "P66"},
        {PacketType::Video, 99, 99, true, 5, 1700, "IDR99"},
    };

    std::vector<Packet> out;
    while (!audio.empty() || !video.empty()) {
        const bool chooseAudio = !audio.empty() && (video.empty() || audio.front().dtsMs <= video.front().dtsMs);
        if (chooseAudio) {
            out.push_back(audio.front());
            audio.pop_front();
        } else {
            out.push_back(video.front());
            video.pop_front();
        }
    }
    return out;
}

class RtmpQueueSimulator {
public:
    void push(Packet packet)
    {
        packets_.push_back(std::move(packet));
        dropIfNeeded();
        adjustBitrateIfNeeded();
    }

    void sendBudget(int budgetBytes)
    {
        int sent = 0;
        while (!packets_.empty() && sent + packets_.front().bytes <= budgetBytes) {
            sent += packets_.front().bytes;
            sentPackets_++;
            packets_.pop_front();
        }
    }

    void printSummary() const
    {
        hjstudy::logFields("weak-network-summary", "final", {
            {"queuePackets", std::to_string(packets_.size())},
            {"queueDurationMs", std::to_string(queueDuration())},
            {"dropped", std::to_string(droppedPackets_)},
            {"sent", std::to_string(sentPackets_)},
            {"recommendedKbps", std::to_string(recommendedKbps_)},
        });
    }

private:
    int queueDuration() const
    {
        if (packets_.size() < 2) {
            return 0;
        }
        return packets_.back().dtsMs - packets_.front().dtsMs;
    }

    void dropIfNeeded()
    {
        const int duration = queueDuration();
        if (duration <= dropThresholdMs_) {
            return;
        }

        // 对应 HJRTMPPacketManager::dropFrames：弱网先从最近 GOP 之前的低优先级视频帧下手，
        // 不直接丢音频，也不轻易丢关键帧，保证恢复时仍有可解码起点。
        for (auto it = packets_.begin(); it != packets_.end();) {
            if (it->type == PacketType::Video && !it->keyFrame && it->priority <= 1) {
                hjstudy::logFields("HJRTMPPacketManager::drop", "drop-low-priority-video", {
                    {"packet", it->label},
                    {"dts", std::to_string(it->dtsMs)},
                    {"queueDuration", std::to_string(duration)},
                });
                it = packets_.erase(it);
                droppedPackets_++;
                if (queueDuration() <= dropThresholdMs_) {
                    break;
                }
            } else {
                ++it;
            }
        }
    }

    void adjustBitrateIfNeeded()
    {
        const bool shouldDecrease = droppedPackets_ > lastDropCount_ || queueDuration() > dropThresholdMs_;
        if (shouldDecrease && recommendedKbps_ > minKbps_) {
            recommendedKbps_ = std::max(minKbps_, recommendedKbps_ - 200);
            lastDropCount_ = droppedPackets_;
            hjstudy::logFields("HJRTMPBitrateAdapter::evaluateBitrate", "auto-adjust-down", {
                {"recommendedKbps", std::to_string(recommendedKbps_)},
                {"reason", "drop-or-queue-duration"},
            });
        }
    }

    std::deque<Packet> packets_;
    int droppedPackets_{0};
    int sentPackets_{0};
    int lastDropCount_{0};
    int recommendedKbps_{1200};
    const int minKbps_{500};
    const int dropThresholdMs_{50};
};

void printFiveMinuteTalk()
{
    hjstudy::printTitle("5-minute pusher talk outline");
    const std::vector<std::string> outline = {
        "1. 产品入口：openPreview 先建立 RTE 预览/TargetEncoder，openPusher 再创建 HJGraphPusher。",
        "2. 音频：OH 采集 PCM，重采样/FIFO 对齐后按 AAC-LC 1024 samples/channel 编码。",
        "3. 视频：RTE/GPU 后处理把画面写入硬编码 Surface，编码器输出 H.264/H.265 packet。",
        "4. 交织与封装：AVInterleave 按 DTS 合并音视频，RTMPMuxer 生成 FLV tag 并等待关键帧。",
        "5. 弱网：PacketManager 看 queue duration/drop ratio，丢低优先级视频帧并触发自动降码率。",
    };
    for (const auto& line : outline) {
        hjstudy::logLine("talk", line);
    }
}

} // namespace

int main()
{
    hjstudy::printReferences(
        "study/week3-pusher-codec-rtmp-practice.md Day 21",
        "studyNote/week3-review.md",
        {
            ".agents/skills/hjmedia-daily-study/references/28-day-plan.md",
            "src/graphs/HJGraphPusher.cpp",
            "src/plugins/HJPluginAVInterleave.cpp",
            "src/media/muxer/HJRTMPPacketManager.cc",
            "src/media/muxer/HJRTMPBitrateAdapter.cc",
            "src/entry/pusher/hsys/verify/HJNAPILiveStream.cpp",
            "src/comp/rte",
            "src/entry/inference",
        });

    hjstudy::printTitle("Week 3 pipeline map");
    for (const auto& stage : buildPipeline()) {
        hjstudy::logFields("pipeline", stage.name, {
            {"source", stage.source},
            {"role", stage.role},
        });
    }

    hjstudy::printTitle("20 review questions");
    int index = 1;
    for (const auto& qa : buildQuestions()) {
        hjstudy::logFields("qa", std::to_string(index++), {
            {"Q", qa.question},
            {"A", qa.answer},
        });
    }

    printFiveMinuteTalk();

    hjstudy::printTitle("DTS interleave simulation");
    const auto packets = makeInterleavedPackets();
    for (const auto& packet : packets) {
        hjstudy::logFields("HJPluginAVInterleave::runTask", "pop-by-dts", {
            {"packet", packet.label},
            {"type", packetTypeName(packet.type)},
            {"dts", std::to_string(packet.dtsMs)},
            {"pts", std::to_string(packet.ptsMs)},
            {"key", hjstudy::yesNo(packet.keyFrame)},
        });
    }

    hjstudy::printTitle("Weak-network queue simulation");
    RtmpQueueSimulator queue;
    int tick = 0;
    for (const auto& packet : packets) {
        queue.push(packet);
        const int networkBudget = tick < 2 ? 2000 : 200; // 后半段模拟 RTMP_Write 吞吐明显低于编码产出。
        queue.sendBudget(networkBudget);
        hjstudy::logFields("HJRTMPAsyncWrapper::run", "send-budget", {
            {"tick", std::to_string(tick++)},
            {"budgetBytes", std::to_string(networkBudget)},
        });
    }
    queue.printSummary();

    return 0;
}
