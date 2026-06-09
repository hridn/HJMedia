/*
 * Day 12: Video queue drop policy practice.
 *
 * Study plan: study/week2-thread-plugin-player-practice.md
 * Study note: studyNote/week2-thread-plugin-player-notes.md
 *
 * HJMedia reference source:
 * - src/plugins/doc/HJPluginVideoFFDecoder.md
 * - src/plugins/doc/HJPluginVideoOHDecoder.md
 * - src/plugins/doc/HJPluginVideoRender.md
 * - src/plugins/doc/HJPluginAVDropping.md
 */

#include "study_demo_common.h"

namespace {

class LiveVideoQueue {
public:
    explicit LiveVideoQueue(std::size_t capacity)
        : capacity_(capacity)
    {
    }

    void push(hjstudy::Frame frame)
    {
        if (frames_.size() >= capacity_) {
            dropOneNonKeyFrame();
        }
        frames_.push_back(std::move(frame));
    }

    void print() const
    {
        for (const auto& frame : frames_) {
            hjstudy::logFields(
                "video-queue",
                "frame",
                {{"ptsMs", std::to_string(frame.ptsMs)}, {"key", hjstudy::yesNo(frame.keyFrame)}});
        }
    }

private:
    void dropOneNonKeyFrame()
    {
        const auto it = std::find_if(frames_.begin(), frames_.end(), [](const hjstudy::Frame& frame) {
            return !frame.keyFrame;
        });
        if (it != frames_.end()) {
            hjstudy::logFields("drop-policy", "drop-non-key", {{"ptsMs", std::to_string(it->ptsMs)}});
            frames_.erase(it);
            return;
        }
        hjstudy::logFields("drop-policy", "drop-oldest", {{"ptsMs", std::to_string(frames_.front().ptsMs)}});
        frames_.pop_front();
    }

    std::size_t capacity_;
    std::deque<hjstudy::Frame> frames_;
};

} // namespace

int main()
{
    hjstudy::printReferences(
        "study/week2-thread-plugin-player-practice.md Day 12",
        "studyNote/week2-thread-plugin-player-notes.md Day 12",
        {
            "src/plugins/doc/HJPluginVideoFFDecoder.md",
            "src/plugins/doc/HJPluginVideoOHDecoder.md",
            "src/plugins/doc/HJPluginVideoRender.md",
            "src/plugins/doc/HJPluginAVDropping.md",
        });

    LiveVideoQueue queue(4);
    for (int i = 0; i < 8; ++i) {
        queue.push({i * 33, hjstudy::MediaType::Video, i % 5 == 0, 0, "yuv-frame"});
    }

    queue.print();
}
