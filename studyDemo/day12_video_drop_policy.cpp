/*
 * Day 12: Video decode, render, and drop policy practice.
 *
 * Study plan: study/week2-thread-plugin-player-practice.md
 * Study note: studyNote/12-video-dropping-practice.md
 *
 * HJMedia reference source:
 * - src/plugins/doc/HJPluginVideoFFDecoder.md
 * - src/plugins/doc/HJPluginVideoOHDecoder.md
 * - src/plugins/doc/HJPluginVideoRender.md
 * - src/plugins/doc/HJPluginAVDropping.md
 * - src/plugins/HJPluginAVDropping.cpp
 */

#include "study_demo_common.h"

namespace {

enum class DropPolicy {
    // 完整性优先：队列满时不主动丢帧，而是让上游等待。
    NeverDrop,
    // 折中策略：队列满时优先丢弃非关键帧，尽量保留关键帧。
    DropNonKeyWhenFull,
    // 实时性优先：结合播放时钟，丢弃已经明显过期的非关键帧。
    CatchUpByTimestamp,
};

std::string toString(DropPolicy policy)
{
    switch (policy) {
    case DropPolicy::NeverDrop:
        return "never-drop";
    case DropPolicy::DropNonKeyWhenFull:
        return "drop-non-key-when-full";
    case DropPolicy::CatchUpByTimestamp:
        return "catch-up-by-timestamp";
    }
    return "unknown";
}

struct PolicyStats {
    // 统计每种策略的最终表现，方便对比延迟、丢帧和反压。
    int produced{};
    int rendered{};
    int dropped{};
    int blocked{};
    int maxBacklog{};
    int64_t lastRenderedPts{-1};
};

struct RenderClock {
    // nowMs 模拟渲染端当前播放时钟，toleranceMs 是允许落后的容忍窗口。
    int64_t nowMs{};
    int64_t toleranceMs{80};
};

class VideoBacklog {
public:
    VideoBacklog(std::size_t capacity, DropPolicy policy)
        : capacity_(capacity)
        , policy_(policy)
    {
    }

    void push(hjstudy::Frame frame, const RenderClock& clock, PolicyStats& stats)
    {
        ++stats.produced;

        // 追帧策略不是等队列满才处理，而是每次入队前先清理过期帧。
        if (policy_ == DropPolicy::CatchUpByTimestamp) {
            dropExpiredFrames(clock, stats);
            if (isExpired(frame, clock) && !frame.keyFrame) {
                ++stats.dropped;
                logDrop("drop-incoming-expired", frame, clock);
                return;
            }
        }

        if (frames_.size() >= capacity_) {
            // 队列满时的处理差异，就是本 demo 要观察的核心。
            handleFullQueue(frame, stats, clock);
            return;
        }

        frames_.push_back(std::move(frame));
        stats.maxBacklog = std::max(stats.maxBacklog, static_cast<int>(frames_.size()));
    }

    void renderOne(const RenderClock& clock, PolicyStats& stats)
    {
        // 渲染前再次追帧，模拟真实播放器在消费侧也会检查时钟落后。
        if (policy_ == DropPolicy::CatchUpByTimestamp) {
            dropExpiredFrames(clock, stats);
        }

        if (frames_.empty()) {
            return;
        }

        auto frame = std::move(frames_.front());
        frames_.pop_front();
        ++stats.rendered;
        stats.lastRenderedPts = frame.ptsMs;
        hjstudy::logFields(
            toString(policy_),
            "render",
            {
                {"ptsMs", std::to_string(frame.ptsMs)},
                {"key", hjstudy::yesNo(frame.keyFrame)},
                {"clockMs", std::to_string(clock.nowMs)},
                {"queueAfter", std::to_string(frames_.size())},
            });
    }

    void drain(const RenderClock& clock, PolicyStats& stats)
    {
        while (!frames_.empty()) {
            renderOne(clock, stats);
        }
    }

private:
    bool isExpired(const hjstudy::Frame& frame, const RenderClock& clock) const
    {
        // 帧时间戳加容忍窗口仍早于当前播放时钟，就认为它已经过期。
        return frame.ptsMs + clock.toleranceMs < clock.nowMs;
    }

    void handleFullQueue(hjstudy::Frame& incoming, PolicyStats& stats, const RenderClock& clock)
    {
        if (policy_ == DropPolicy::NeverDrop) {
            // 不丢帧策略只能反压：当前帧不入队，等待 render 消费后再继续。
            ++stats.blocked;
            hjstudy::logFields(
                toString(policy_),
                "backpressure",
                {
                    {"incomingPtsMs", std::to_string(incoming.ptsMs)},
                    {"queueSize", std::to_string(frames_.size())},
                });
            return;
        }

        if (policy_ == DropPolicy::DropNonKeyWhenFull) {
            // 先从已排队帧里找非关键帧丢掉，为新帧腾空间。
            if (dropOneNonKey(stats, clock)) {
                frames_.push_back(std::move(incoming));
                stats.maxBacklog = std::max(stats.maxBacklog, static_cast<int>(frames_.size()));
                return;
            }

            if (!incoming.keyFrame) {
                // 队列里没有可丢的非关键帧时，新的非关键帧也可以直接丢弃。
                ++stats.dropped;
                logDrop("drop-incoming-non-key", incoming, clock);
                return;
            }

            // 如果新来的帧是关键帧，为了保住解码恢复点，退而求其次丢最老帧。
            dropOldest(stats, clock);
            frames_.push_back(std::move(incoming));
            stats.maxBacklog = std::max(stats.maxBacklog, static_cast<int>(frames_.size()));
            return;
        }

        // CatchUpByTimestamp 队列仍满时，也优先找非关键帧释放空间。
        if (dropOneNonKey(stats, clock)) {
            frames_.push_back(std::move(incoming));
            stats.maxBacklog = std::max(stats.maxBacklog, static_cast<int>(frames_.size()));
            return;
        }

        // 如果队列里只剩关键帧，不强行破坏关键帧边界，先让上游等待。
        ++stats.blocked;
        hjstudy::logFields(
            toString(policy_),
            "wait-for-keyframe-boundary",
            {
                {"incomingPtsMs", std::to_string(incoming.ptsMs)},
                {"queueSize", std::to_string(frames_.size())},
            });
    }

    void dropExpiredFrames(const RenderClock& clock, PolicyStats& stats)
    {
        // 只丢队头连续过期的非关键帧；遇到关键帧先停，避免破坏解码参考点。
        while (!frames_.empty() && isExpired(frames_.front(), clock) && !frames_.front().keyFrame) {
            dropFront("drop-expired", stats, clock);
        }
    }

    bool dropOneNonKey(PolicyStats& stats, const RenderClock& clock)
    {
        // 从旧到新寻找第一帧非关键帧，模拟“尽量保关键帧”的直播丢帧原则。
        const auto it = std::find_if(frames_.begin(), frames_.end(), [](const hjstudy::Frame& frame) {
            return !frame.keyFrame;
        });
        if (it == frames_.end()) {
            return false;
        }

        ++stats.dropped;
        logDrop("drop-queued-non-key", *it, clock);
        frames_.erase(it);
        return true;
    }

    void dropOldest(PolicyStats& stats, const RenderClock& clock)
    {
        dropFront("drop-oldest", stats, clock);
    }

    void dropFront(std::string_view action, PolicyStats& stats, const RenderClock& clock)
    {
        ++stats.dropped;
        logDrop(action, frames_.front(), clock);
        frames_.pop_front();
    }

    void logDrop(std::string_view action, const hjstudy::Frame& frame, const RenderClock& clock) const
    {
        hjstudy::logFields(
            toString(policy_),
            action,
            {
                {"ptsMs", std::to_string(frame.ptsMs)},
                {"key", hjstudy::yesNo(frame.keyFrame)},
                {"clockMs", std::to_string(clock.nowMs)},
            });
    }

    std::size_t capacity_;
    DropPolicy policy_;
    std::deque<hjstudy::Frame> frames_;
};

std::vector<hjstudy::Frame> makeLiveFrames()
{
    std::vector<hjstudy::Frame> frames;
    for (int i = 0; i < 18; ++i) {
        // 约 30fps：每帧间隔 33ms；每 6 帧设置一个关键帧。
        frames.push_back({i * 33, hjstudy::MediaType::Video, i % 6 == 0, 0, "decoded-yuv"});
    }
    return frames;
}

void printPolicyIntent(DropPolicy policy)
{
    switch (policy) {
    case DropPolicy::NeverDrop:
        hjstudy::logLine("policy", "never-drop: preserve every frame; producer is blocked when render backlog is full");
        break;
    case DropPolicy::DropNonKeyWhenFull:
        hjstudy::logLine("policy", "drop-non-key: keep key frames first; discard disposable video frames when backlog is full");
        break;
    case DropPolicy::CatchUpByTimestamp:
        hjstudy::logLine("policy", "catch-up: compare frame pts with render clock; drop expired non-key frames to recover latency");
        break;
    }
}

void runScenario(DropPolicy policy)
{
    hjstudy::printTitle(toString(policy));
    printPolicyIntent(policy);

    VideoBacklog queue(5, policy);
    PolicyStats stats;
    RenderClock clock;
    const auto frames = makeLiveFrames();

    for (std::size_t i = 0; i < frames.size(); ++i) {
        // 让渲染时钟按 50ms 前进，故意慢于 33ms 的生产节奏，制造堆积。
        clock.nowMs = static_cast<int64_t>(i) * 50;
        queue.push(frames[i], clock, stats);

        // Render is intentionally slower than the producer: one frame every two input frames.
        if (i % 2 == 1) {
            queue.renderOne(clock, stats);
        }
    }

    clock.nowMs += 150;
    queue.drain(clock, stats);

    hjstudy::logFields(
        toString(policy),
        "summary",
        {
            {"produced", std::to_string(stats.produced)},
            {"rendered", std::to_string(stats.rendered)},
            {"dropped", std::to_string(stats.dropped)},
            {"blocked", std::to_string(stats.blocked)},
            {"maxBacklog", std::to_string(stats.maxBacklog)},
            {"lastRenderedPts", std::to_string(stats.lastRenderedPts)},
        });
}

void printPseudoCode()
{
    hjstudy::printTitle("three policy pseudo code");
    hjstudy::logLine("never-drop", "if renderQueue.full(): stop demux/decoder and wait for render onOutputUpdated()");
    hjstudy::logLine("drop-non-key", "if queue.full(): erase first non-key frame; if none, wait or drop oldest before next key frame");
    hjstudy::logLine("catch-up", "while frame.pts + tolerance < renderClock: drop non-key frames until latency is acceptable");
}

} // namespace

int main()
{
    hjstudy::printReferences(
        "study/week2-thread-plugin-player-practice.md Day 12",
        "studyNote/12-video-dropping-practice.md",
        {
            "src/plugins/doc/HJPluginVideoFFDecoder.md",
            "src/plugins/doc/HJPluginVideoOHDecoder.md",
            "src/plugins/doc/HJPluginVideoRender.md",
            "src/plugins/doc/HJPluginAVDropping.md",
            "src/plugins/HJPluginAVDropping.cpp",
        });

    printPseudoCode();
    runScenario(DropPolicy::NeverDrop);
    runScenario(DropPolicy::DropNonKeyWhenFull);
    runScenario(DropPolicy::CatchUpByTimestamp);
}
