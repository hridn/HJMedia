/*
 * Day 18: AV interleave, FLV timestamp and RTMP failure strategy practice.
 *
 * Study plan: .agents/skills/hjmedia-daily-study/references/28-day-plan.md Day 18
 * Study note: studyNote/18-rtmp-muxer-timestamp.md
 *
 * HJMedia reference source:
 * - src/graphs/HJGraphPusher.cpp
 * - src/plugins/HJPluginAVInterleave.cpp
 * - src/plugins/HJPluginMuxer.cpp
 * - src/plugins/HJPluginRTMPMuxer.cpp
 * - src/media/muxer/HJRTMPMuxer.cc
 * - src/media/muxer/HJRTMPAsyncWrapper.cc
 * - src/media/muxer/HJRTMPPacketManager.h/.cc
 * - src/media/muxer/flv/HJFLVUtils.h/.cc
 * - src/media/muxer/HJRTMPWrapper.cc
 * - third_party/librtmp/include/librtmp/rtmp.h
 */

#include "study_demo_common.h"

namespace {

enum class PacketPriority {
    Low,
    Medium,
    High,
};

std::string toString(PacketPriority priority)
{
    switch (priority) {
    case PacketPriority::Low:
        return "low";
    case PacketPriority::Medium:
        return "medium";
    case PacketPriority::High:
        return "high";
    }
    return "unknown";
}

struct Packet {
    int64_t ptsMs{};
    int64_t dtsMs{};
    hjstudy::MediaType type{};
    bool keyFrame{};
    PacketPriority priority{PacketPriority::High};
    std::string payload;
};

struct FlvTag {
    Packet packet;
    int64_t tagTimestampMs{};
    int64_t compositionOffsetMs{};
    std::string name;
};

class AvInterleaver {
public:
    void push(Packet packet)
    {
        if (packet.type == hjstudy::MediaType::Audio) {
            audio_.push_back(std::move(packet));
        } else if (packet.type == hjstudy::MediaType::Video) {
            video_.push_back(std::move(packet));
        }
    }

    std::optional<Packet> popNext()
    {
        // 对应 HJPluginAVInterleave::runTask()：
        // 先 preview 音频/视频队首，再比较 getDTS()，DTS 更小的输入被 receive 并 deliverToOutputs。
        // 源码里音频 DTS <= 视频 DTS 时优先出音频，这里保持相同 tie-break 行为。
        if (!audio_.empty() && (video_.empty() || audio_.front().dtsMs <= video_.front().dtsMs)) {
            return popFront(audio_);
        }
        if (!video_.empty()) {
            return popFront(video_);
        }
        return std::nullopt;
    }

private:
    static Packet popFront(std::deque<Packet>& packets)
    {
        Packet packet = packets.front();
        packets.pop_front();
        return packet;
    }

    std::deque<Packet> audio_;
    std::deque<Packet> video_;
};

class FlvTimestampBuilder {
public:
    void observe(const Packet& packet)
    {
        // 对应 HJRTMPMuxer::waitStartDTSOffset()：
        // 音视频都有时等待两路第一帧，取较小 DTS 作为零点，后续 FLV tag timestamp 写 dts - offset。
        if (packet.type == hjstudy::MediaType::Audio && !firstAudioDts_) {
            firstAudioDts_ = packet.dtsMs;
        }
        if (packet.type == hjstudy::MediaType::Video && !firstVideoDts_) {
            firstVideoDts_ = packet.dtsMs;
        }
        if (!startDtsOffset_ && firstAudioDts_ && firstVideoDts_) {
            startDtsOffset_ = std::min(*firstAudioDts_, *firstVideoDts_);
            hjstudy::logFields("rtmp-muxer", "start-dts-offset-ready", {{"offsetMs", std::to_string(*startDtsOffset_)}});
        }
    }

    std::optional<FlvTag> build(const Packet& packet) const
    {
        if (!startDtsOffset_) {
            hjstudy::logFields(
                "rtmp-muxer",
                "buffer-until-audio-video-ready",
                {{"type", hjstudy::toString(packet.type)}, {"dtsMs", std::to_string(packet.dtsMs)}});
            return std::nullopt;
        }

        // 对应 HJFLVPacket::init() 和 HJFLVUtils::buildVideoTag()/buildAudioTag()：
        // packet 内部先保存 pts/dts，写 tag 时 timestamp 用 DTS，视频额外写 composition time offset = PTS - DTS。
        return FlvTag{
            packet,
            packet.dtsMs - *startDtsOffset_,
            packet.ptsMs - packet.dtsMs,
            packet.type == hjstudy::MediaType::Audio ? "aac-flv-tag" : "video-flv-tag",
        };
    }

private:
    std::optional<int64_t> firstAudioDts_;
    std::optional<int64_t> firstVideoDts_;
    std::optional<int64_t> startDtsOffset_;
};

class RtmpSendStrategy {
public:
    void enqueue(FlvTag tag)
    {
        backlog_.push_back(std::move(tag));
        dropIfBacklogTooLong();
    }

    void drain()
    {
        while (!backlog_.empty()) {
            auto tag = backlog_.front();
            backlog_.pop_front();

            if (simulateSendFailure(tag)) {
                retryCount_++;
                hjstudy::logFields(
                    "rtmp-async-wrapper",
                    "send-failed-schedule-retry",
                    {
                        {"tagTsMs", std::to_string(tag.tagTimestampMs)},
                        {"retryCount", std::to_string(retryCount_)},
                    });

                if (retryCount_ >= 2) {
                    // 对应 HJRTMPAsyncWrapper::onRTMPWrapperNotify() 的失败后 destroyAVIO + retryAVIO 思路。
                    // demo 中不连真实网络，只表达策略：短失败先重试，连续失败触发重连，队列侧保留最近 GOP。
                    hjstudy::logFields("rtmp-async-wrapper", "reconnect-and-keep-last-gop", {{"retryCount", std::to_string(retryCount_)}});
                    keepLastGop();
                    retryCount_ = 0;
                }
                continue;
            }

            retryCount_ = 0;
            hjstudy::logFields(
                "librtmp",
                "RTMP_Write",
                {
                    {"name", tag.name},
                    {"type", hjstudy::toString(tag.packet.type)},
                    {"dtsMs", std::to_string(tag.packet.dtsMs)},
                    {"ptsMs", std::to_string(tag.packet.ptsMs)},
                    {"tagTsMs", std::to_string(tag.tagTimestampMs)},
                    {"ctsMs", std::to_string(tag.compositionOffsetMs)},
                    {"keyFrame", hjstudy::yesNo(tag.packet.keyFrame)},
                });
        }
    }

private:
    static bool simulateSendFailure(const FlvTag& tag)
    {
        return tag.tagTimestampMs >= 80;
    }

    void dropIfBacklogTooLong()
    {
        const auto duration = backlog_.empty() ? 0 : backlog_.back().packet.dtsMs - backlog_.front().packet.dtsMs;
        if (duration <= 70) {
            return;
        }

        // 对应 HJRTMPPacketManager::drop() / dropFrames()：
        // 队列时间跨度过大时，优先丢低优先级视频帧；不要无限缓存导致推流延迟持续上涨。
        const auto it = std::find_if(backlog_.begin(), backlog_.end(), [](const FlvTag& tag) {
            return tag.packet.type == hjstudy::MediaType::Video && tag.packet.priority == PacketPriority::Low;
        });
        if (it != backlog_.end()) {
            hjstudy::logFields(
                "rtmp-packet-manager",
                "drop-low-priority-video",
                {{"dtsMs", std::to_string(it->packet.dtsMs)}, {"priority", toString(it->packet.priority)}});
            backlog_.erase(it);
        }
    }

    void keepLastGop()
    {
        const auto lastKey = std::find_if(backlog_.rbegin(), backlog_.rend(), [](const FlvTag& tag) {
            return tag.packet.type == hjstudy::MediaType::Video && tag.packet.keyFrame;
        });
        if (lastKey == backlog_.rend()) {
            return;
        }

        const auto keepFrom = std::prev(lastKey.base());
        const auto dropped = std::distance(backlog_.begin(), keepFrom);
        backlog_.erase(backlog_.begin(), keepFrom);
        hjstudy::logFields("rtmp-packet-manager", "keep-last-gop", {{"droppedBeforeGop", std::to_string(dropped)}});
    }

    std::deque<FlvTag> backlog_;
    int retryCount_{0};
};

std::vector<Packet> makePackets()
{
    return {
        // 音频 AAC 常见 20ms 左右一个 packet，这里模拟 0/20/40/60/80ms。
        {0, 0, hjstudy::MediaType::Audio, false, PacketPriority::High, "aac-0"},
        {20, 20, hjstudy::MediaType::Audio, false, PacketPriority::High, "aac-20"},
        {40, 40, hjstudy::MediaType::Audio, false, PacketPriority::High, "aac-40"},
        {60, 60, hjstudy::MediaType::Audio, false, PacketPriority::High, "aac-60"},
        {80, 80, hjstudy::MediaType::Audio, false, PacketPriority::High, "aac-80"},

        // 视频按约 30fps 模拟；B 帧示例中 PTS 可大于 DTS，FLV tag 需要写 composition offset。
        {0, 0, hjstudy::MediaType::Video, true, PacketPriority::High, "idr+sps/pps"},
        {33, 33, hjstudy::MediaType::Video, false, PacketPriority::Low, "p-frame"},
        {99, 66, hjstudy::MediaType::Video, false, PacketPriority::Medium, "b-frame-cts"},
        {99, 99, hjstudy::MediaType::Video, true, PacketPriority::High, "next-idr"},
    };
}

} // namespace

int main()
{
    hjstudy::printReferences(
        ".agents/skills/hjmedia-daily-study/references/28-day-plan.md Day 18",
        "studyNote/18-rtmp-muxer-timestamp.md",
        {
            "src/graphs/HJGraphPusher.cpp",
            "src/plugins/HJPluginAVInterleave.cpp",
            "src/plugins/HJPluginMuxer.cpp",
            "src/plugins/HJPluginRTMPMuxer.cpp",
            "src/media/muxer/HJRTMPMuxer.cc",
            "src/media/muxer/HJRTMPAsyncWrapper.cc",
            "src/media/muxer/HJRTMPPacketManager.h",
            "src/media/muxer/HJRTMPPacketManager.cc",
            "src/media/muxer/flv/HJFLVUtils.h",
            "src/media/muxer/flv/HJFLVUtils.cc",
            "src/media/muxer/HJRTMPWrapper.cc",
            "third_party/librtmp/include/librtmp/rtmp.h",
        });

    AvInterleaver interleaver;
    for (auto packet : makePackets()) {
        interleaver.push(std::move(packet));
    }

    FlvTimestampBuilder flvBuilder;
    RtmpSendStrategy sender;

    hjstudy::printTitle("interleave -> flv timestamp -> rtmp send");
    while (auto packet = interleaver.popNext()) {
        hjstudy::logFields(
            "av-interleave",
            "pop-by-dts",
            {
                {"type", hjstudy::toString(packet->type)},
                {"dtsMs", std::to_string(packet->dtsMs)},
                {"ptsMs", std::to_string(packet->ptsMs)},
                {"payload", packet->payload},
            });

        flvBuilder.observe(*packet);
        auto tag = flvBuilder.build(*packet);
        if (!tag) {
            continue;
        }
        sender.enqueue(std::move(*tag));
    }

    sender.drain();
}
