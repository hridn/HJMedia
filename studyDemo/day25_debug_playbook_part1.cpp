/*
 * Day 25: Debug playbook part 1.
 *
 * Study plan: study/week4-interview-job-ready-practice.md
 * Study note: studyNote/week4-interview-job-ready-notes.md
 *
 * HJMedia reference source:
 * - src/graphs/HJGraphMusicPlayer.cpp
 * - src/plugins/doc/HJPluginDemuxer.md
 * - src/plugins/doc/HJPluginAudioRender.md
 * - src/plugins/doc/HJPluginAVDropping.md
 */

#include "study_demo_common.h"

namespace {

struct DebugCase {
    std::string symptom;
    std::string suspect;
    std::string logs;
    std::string fix;
    std::string risk;
};

void printCase(const DebugCase& debugCase)
{
    hjstudy::logFields(
        "debug-case",
        debugCase.symptom,
        {
            {"suspect", debugCase.suspect},
            {"logs", debugCase.logs},
            {"fix", debugCase.fix},
            {"risk", debugCase.risk},
        });
}

} // namespace

int main()
{
    hjstudy::printReferences(
        "study/week4-interview-job-ready-practice.md Day 25",
        "studyNote/week4-interview-job-ready-notes.md Day 25",
        {
            "src/graphs/HJGraphMusicPlayer.cpp",
            "src/plugins/doc/HJPluginDemuxer.md",
            "src/plugins/doc/HJPluginAudioRender.md",
            "src/plugins/doc/HJPluginAVDropping.md",
        });

    printCase({"seek old frame rendered", "demuxer/decoder/resampler/render queues", "seek id, frame pts, generation, flush events", "flush all stages and reject old generation frames", "dropping valid frames if generation is advanced too early"});
    printCase({"crash after close", "delayed handler task or stale callback", "object lifetime, weak target, handler stop order", "use weak_ptr and cancel/clear pending tasks on release", "deadlock if release waits on its own handler thread"});
    printCase({"live latency keeps growing", "render/network queue backpressure", "queue size, frame pts delta, drop count", "drop low-priority frames and keep clock close to live edge", "over-dropping hurts visual continuity"});
}
