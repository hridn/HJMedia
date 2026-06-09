/*
 * Day 13: seek / flush / EOF debugging practice.
 *
 * Study plan: study/week2-thread-plugin-player-practice.md
 * Study note: studyNote/week2-thread-plugin-player-notes.md
 *
 * HJMedia reference source:
 * - src/graphs/HJGraphMusicPlayer.cpp
 * - src/plugins/doc/HJPluginDemuxer.md
 * - src/plugins/doc/HJPluginAudioRender.md
 * - src/plugins/doc/HJTimeline.md
 */

#include "study_demo_common.h"

namespace {

class SeekGenerationGate {
public:
    void seekTo(int64_t timestampMs)
    {
        ++generation_;
        hjstudy::logFields(
            "seek",
            "new-generation",
            {{"generation", std::to_string(generation_)}, {"timestampMs", std::to_string(timestampMs)}});
    }

    bool accept(const hjstudy::Frame& frame) const
    {
        return frame.generation == generation_;
    }

    int generation() const
    {
        return generation_;
    }

private:
    int generation_{0};
};

} // namespace

int main()
{
    hjstudy::printReferences(
        "study/week2-thread-plugin-player-practice.md Day 13",
        "studyNote/week2-thread-plugin-player-notes.md Day 13",
        {
            "src/graphs/HJGraphMusicPlayer.cpp",
            "src/plugins/doc/HJPluginDemuxer.md",
            "src/plugins/doc/HJPluginAudioRender.md",
            "src/plugins/doc/HJTimeline.md",
        });

    SeekGenerationGate gate;

    std::vector<hjstudy::Frame> renderQueue = {
        {1000, hjstudy::MediaType::Audio, false, 0, "old-frame"},
        {1020, hjstudy::MediaType::Audio, false, 0, "old-frame"},
    };

    gate.seekTo(5000);
    renderQueue.push_back({5000, hjstudy::MediaType::Audio, false, gate.generation(), "new-frame"});

    for (const auto& frame : renderQueue) {
        hjstudy::logFields(
            "render-gate",
            gate.accept(frame) ? "accept" : "drop-stale",
            {{"ptsMs", std::to_string(frame.ptsMs)}, {"generation", std::to_string(frame.generation)}});
    }
}
