/*
 * Day 19: Weak network backpressure practice.
 *
 * Study plan: .agents/skills/hjmedia-daily-study/references/28-day-plan.md Day 19
 * Study note: studyNote/19-weak-network-queue-practice.md
 *
 * HJMedia reference source:
 * - src/media/muxer/HJRTMPPacketManager.h/.cc
 * - src/media/muxer/HJRTMPBitrateAdapter.h/.cc
 * - src/media/muxer/HJRTMPAsyncWrapper.cc
 * - src/media/muxer/HJRTMPMuxer.cc
 * - src/media/muxer/HJRTMPUtils.h/.cc
 * - src/media/muxer/HJRTMPWrapper.cc
 * - src/plugins/HJPluginMuxer.cpp
 */

#include "study_demo_common.h"

namespace {

enum class Strategy {
    BlockEncoder,
    DropLowPriority,
    AdaptiveBitrate,
};

std::string toString(Strategy strategy)
{
    switch (strategy) {
    case Strategy::BlockEncoder:
        return "block-encoder";
    case Strategy::DropLowPriority:
        return "drop-low-priority";
    case Strategy::AdaptiveBitrate:
        return "adaptive-bitrate";
    }
    return "unknown";
}

struct Packet {
    int64_t dtsMs{};
    hjstudy::MediaType type{hjstudy::MediaType::Audio};
    bool keyFrame{};
    int priority{}; // 1=B/P 低优先级，2=P 中优先级，3=I/Audio 高优先级，映射 HJFLV_PKT_PRIORITY_*。
    int sizeBytes{};
    std::string payload;
};

struct Metrics {
    int produced{};
    int sent{};
    int dropped{};
    int blockedTicks{};
    int bitrateChanges{};
    int recommendedBitrateKbps{2200};
    std::size_t peakQueuePackets{};
    int64_t peakQueueDurationMs{};
};

class WeakNetworkQueue {
public:
    explicit WeakNetworkQueue(Strategy strategy)
        : strategy_(strategy)
    {
    }

    bool shouldBlockEncoder() const
    {
        // 对应“阻塞编码”策略：当队列时长过大时让上游停产。
        // HJMedia 推流更偏向保持实时性，不会把这个作为唯一策略，否则编码和采集会被网络反压拖住。
        return strategy_ == Strategy::BlockEncoder && queueDurationMs() >= highWatermarkMs_;
    }

    void enqueue(Packet packet, Metrics& metrics)
    {
        packets_.push_back(std::move(packet));
        metrics.produced++;

        if (strategy_ == Strategy::DropLowPriority || strategy_ == Strategy::AdaptiveBitrate) {
            dropByDuration(metrics);
        }

        if (strategy_ == Strategy::AdaptiveBitrate) {
            adaptBitrate(metrics);
        }

        metrics.peakQueuePackets = std::max(metrics.peakQueuePackets, packets_.size());
        metrics.peakQueueDurationMs = std::max(metrics.peakQueueDurationMs, queueDurationMs());
    }

    void sendBudget(int budgetBytes, Metrics& metrics)
    {
        int left = budgetBytes;
        while (!packets_.empty() && packets_.front().sizeBytes <= left) {
            Packet packet = packets_.front();
            packets_.pop_front();
            left -= packet.sizeBytes;
            metrics.sent++;

            hjstudy::logFields(
                toString(strategy_),
                "send",
                {
                    {"type", hjstudy::toString(packet.type)},
                    {"dtsMs", std::to_string(packet.dtsMs)},
                    {"sizeBytes", std::to_string(packet.sizeBytes)},
                    {"queuePackets", std::to_string(packets_.size())},
                    {"queueDurationMs", std::to_string(queueDurationMs())},
                });
        }
    }

    int64_t queueDurationMs() const
    {
        if (packets_.size() < 2) {
            return 0;
        }

        int64_t minDts = packets_.front().dtsMs;
        int64_t maxDts = packets_.front().dtsMs;
        for (const auto& packet : packets_) {
            minDts = std::min(minDts, packet.dtsMs);
            maxDts = std::max(maxDts, packet.dtsMs);
        }
        return maxDts - minDts;
    }

private:
    void dropByDuration(Metrics& metrics)
    {
        // 对应 HJRTMPPacketManager::drop()：
        // cache duration 超过阈值后，先丢低优先级视频；更严重时再提高丢帧优先级。
        const auto duration = queueDurationMs();
        if (duration <= lowDropThresholdMs_) {
            return;
        }

        int maxDroppablePriority = 1;
        if (duration > middleDropThresholdMs_) {
            maxDroppablePriority = 2;
        }
        if (duration > highWatermarkMs_) {
            maxDroppablePriority = 3;
        }

        const auto it = std::find_if(packets_.begin(), packets_.end(), [maxDroppablePriority](const Packet& packet) {
            return packet.type == hjstudy::MediaType::Video && !packet.keyFrame && packet.priority <= maxDroppablePriority;
        });
        if (it == packets_.end()) {
            return;
        }

        hjstudy::logFields(
            toString(strategy_),
            "drop-video",
            {
                {"dtsMs", std::to_string(it->dtsMs)},
                {"priority", std::to_string(it->priority)},
                {"queueDurationMs", std::to_string(duration)},
            });
        packets_.erase(it);
        metrics.dropped++;
    }

    void adaptBitrate(Metrics& metrics)
    {
        // 对应 HJRTMPBitrateAdapter::evaluateBitrate() 的核心思想：
        // 出现丢帧或队列超过阈值时降低推荐码率；队列长期很短且网络恢复时再逐步上调。
        const auto duration = queueDurationMs();
        if ((metrics.dropped > lastDropCount_ || duration > middleDropThresholdMs_) && metrics.recommendedBitrateKbps > minBitrateKbps_) {
            metrics.recommendedBitrateKbps = std::max(minBitrateKbps_, metrics.recommendedBitrateKbps - 300);
            metrics.bitrateChanges++;
            lastDropCount_ = metrics.dropped;
            hjstudy::logFields(
                toString(strategy_),
                "auto-adjust-bitrate-down",
                {
                    {"recommendedKbps", std::to_string(metrics.recommendedBitrateKbps)},
                    {"queueDurationMs", std::to_string(duration)},
                    {"dropped", std::to_string(metrics.dropped)},
                });
        } else if (duration < recoverThresholdMs_ && metrics.recommendedBitrateKbps < maxBitrateKbps_) {
            metrics.recommendedBitrateKbps = std::min(maxBitrateKbps_, metrics.recommendedBitrateKbps + 100);
            metrics.bitrateChanges++;
            hjstudy::logFields(
                toString(strategy_),
                "auto-adjust-bitrate-up",
                {
                    {"recommendedKbps", std::to_string(metrics.recommendedBitrateKbps)},
                    {"queueDurationMs", std::to_string(duration)},
                });
        }
    }

    Strategy strategy_;
    std::deque<Packet> packets_;
    int lastDropCount_{};

    const int64_t lowDropThresholdMs_{120};
    const int64_t middleDropThresholdMs_{180};
    const int64_t highWatermarkMs_{240};
    const int64_t recoverThresholdMs_{40};
    const int minBitrateKbps_{700};
    const int maxBitrateKbps_{2200};
};

int networkBudgetBytes(int tick)
{
    // 0-5 正常，6-18 弱网，19 以后恢复；对应 HJRTMPAsyncWrapper 上报 netkbps 变化。
    if (tick < 6) {
        return 5200;
    }
    if (tick < 19) {
        return 1400;
    }
    return 6000;
}

int videoSizeBytes(int recommendedBitrateKbps)
{
    return 900 + recommendedBitrateKbps / 2;
}

std::vector<Packet> producePackets(int tick, int recommendedBitrateKbps)
{
    const int64_t nowMs = tick * 20;
    std::vector<Packet> packets;

    packets.push_back(Packet{nowMs, hjstudy::MediaType::Audio, false, 3, 420, "aac"});

    if (tick % 2 == 0) {
        const bool keyFrame = tick % 10 == 0;
        const int priority = keyFrame ? 3 : (tick % 4 == 0 ? 2 : 1);
        packets.push_back(Packet{nowMs, hjstudy::MediaType::Video, keyFrame, priority, videoSizeBytes(recommendedBitrateKbps), keyFrame ? "idr" : "p/b"});
    }

    return packets;
}

Metrics runScenario(Strategy strategy)
{
    hjstudy::printTitle(toString(strategy));
    WeakNetworkQueue queue(strategy);
    Metrics metrics;

    for (int tick = 0; tick < 28; ++tick) {
        const auto budget = networkBudgetBytes(tick);

        if (queue.shouldBlockEncoder()) {
            metrics.blockedTicks++;
            hjstudy::logFields(
                toString(strategy),
                "block-encoder",
                {
                    {"tick", std::to_string(tick)},
                    {"queueDurationMs", std::to_string(queue.queueDurationMs())},
                });
        } else {
            for (auto packet : producePackets(tick, metrics.recommendedBitrateKbps)) {
                queue.enqueue(std::move(packet), metrics);
            }
        }

        queue.sendBudget(budget, metrics);
    }

    hjstudy::logFields(
        toString(strategy),
        "summary",
        {
            {"produced", std::to_string(metrics.produced)},
            {"sent", std::to_string(metrics.sent)},
            {"dropped", std::to_string(metrics.dropped)},
            {"blockedTicks", std::to_string(metrics.blockedTicks)},
            {"bitrateChanges", std::to_string(metrics.bitrateChanges)},
            {"finalKbps", std::to_string(metrics.recommendedBitrateKbps)},
            {"peakQueuePackets", std::to_string(metrics.peakQueuePackets)},
            {"peakQueueDurationMs", std::to_string(metrics.peakQueueDurationMs)},
        });

    return metrics;
}

} // namespace

int main()
{
    hjstudy::printReferences(
        ".agents/skills/hjmedia-daily-study/references/28-day-plan.md Day 19",
        "studyNote/19-weak-network-queue-practice.md",
        {
            "src/media/muxer/HJRTMPPacketManager.h",
            "src/media/muxer/HJRTMPPacketManager.cc",
            "src/media/muxer/HJRTMPBitrateAdapter.h",
            "src/media/muxer/HJRTMPBitrateAdapter.cc",
            "src/media/muxer/HJRTMPAsyncWrapper.cc",
            "src/media/muxer/HJRTMPMuxer.cc",
            "src/media/muxer/HJRTMPUtils.h",
            "src/media/muxer/HJRTMPWrapper.cc",
            "src/plugins/HJPluginMuxer.cpp",
        });

    const auto blocking = runScenario(Strategy::BlockEncoder);
    const auto dropping = runScenario(Strategy::DropLowPriority);
    const auto adaptive = runScenario(Strategy::AdaptiveBitrate);

    hjstudy::printTitle("strategy comparison");
    hjstudy::logFields("compare", "block-vs-drop-vs-adapt", {
        {"blockPeakDelayMs", std::to_string(blocking.peakQueueDurationMs)},
        {"blockTicks", std::to_string(blocking.blockedTicks)},
        {"dropPeakDelayMs", std::to_string(dropping.peakQueueDurationMs)},
        {"dropFrames", std::to_string(dropping.dropped)},
        {"adaptPeakDelayMs", std::to_string(adaptive.peakQueueDurationMs)},
        {"adaptFramesDropped", std::to_string(adaptive.dropped)},
        {"adaptFinalKbps", std::to_string(adaptive.recommendedBitrateKbps)},
    });
}
