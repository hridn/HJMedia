/*
 * Day 19: Network backpressure practice.
 *
 * Study plan: study/week3-pusher-codec-rtmp-practice.md
 * Study note: studyNote/week3-pusher-codec-rtmp-notes.md
 *
 * HJMedia reference source:
 * - src/graphs/HJGraphPusher.cpp
 * - src/media/net
 * - src/media/muxer
 */

#include "study_demo_common.h"

namespace {

class NetworkQueue {
public:
    explicit NetworkQueue(std::size_t capacity)
        : capacity_(capacity)
    {
    }

    void enqueue(hjstudy::Frame packet)
    {
        if (packets_.size() >= capacity_) {
            dropLowPriorityPacket();
        }
        packets_.push_back(std::move(packet));
    }

    void sendOne()
    {
        if (packets_.empty()) {
            return;
        }
        const auto packet = packets_.front();
        packets_.pop_front();
        hjstudy::logFields(
            "network",
            "send",
            {{"type", hjstudy::toString(packet.type)}, {"ptsMs", std::to_string(packet.ptsMs)}, {"queueSize", std::to_string(packets_.size())}});
    }

private:
    void dropLowPriorityPacket()
    {
        const auto it = std::find_if(packets_.begin(), packets_.end(), [](const hjstudy::Frame& frame) {
            return frame.type == hjstudy::MediaType::Video && !frame.keyFrame;
        });
        if (it != packets_.end()) {
            hjstudy::logFields("network", "drop-video-p-frame", {{"ptsMs", std::to_string(it->ptsMs)}});
            packets_.erase(it);
            return;
        }
        hjstudy::logFields("network", "drop-oldest", {{"ptsMs", std::to_string(packets_.front().ptsMs)}});
        packets_.pop_front();
    }

    std::size_t capacity_;
    std::deque<hjstudy::Frame> packets_;
};

} // namespace

int main()
{
    hjstudy::printReferences(
        "study/week3-pusher-codec-rtmp-practice.md Day 19",
        "studyNote/week3-pusher-codec-rtmp-notes.md Day 19",
        {
            "src/graphs/HJGraphPusher.cpp",
            "src/media/net",
            "src/media/muxer",
        });

    NetworkQueue queue(5);
    for (int i = 0; i < 10; ++i) {
        queue.enqueue({i * 20, i % 2 == 0 ? hjstudy::MediaType::Video : hjstudy::MediaType::Audio, i == 0, 0, "packet"});
        if (i % 3 == 0) {
            queue.sendOne();
        }
    }
}
