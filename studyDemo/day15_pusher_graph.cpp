/*
 * Day 15: Pusher graph overview practice.
 *
 * Study plan: study/week3-pusher-codec-rtmp-practice.md
 * Study note: studyNote/week3-pusher-codec-rtmp-notes.md
 *
 * HJMedia reference source:
 * - src/graphs/HJGraphPusher.h
 * - src/graphs/HJGraphPusher.cpp
 * - src/entry/pusher
 * - examples/harmony/API.md
 */

#include "study_demo_common.h"

namespace {

struct PusherStage {
    std::string name;
    std::string responsibility;
    std::string threadRole;
};

} // namespace

int main()
{
    hjstudy::printReferences(
        "study/week3-pusher-codec-rtmp-practice.md Day 15",
        "studyNote/week3-pusher-codec-rtmp-notes.md Day 15",
        {
            "src/graphs/HJGraphPusher.h",
            "src/graphs/HJGraphPusher.cpp",
            "src/entry/pusher",
            "examples/harmony/API.md",
        });

    const std::vector<PusherStage> stages = {
        {"capture", "camera and microphone input", "device callback"},
        {"process", "GPU effects, face protection, frame adaptation", "render/worker"},
        {"encode", "AAC and H.264/H.265 encoding", "codec worker"},
        {"mux", "packetize and interleave audio/video", "mux worker"},
        {"rtmp", "send packets, retry, drop under pressure", "network worker"},
    };

    for (const auto& stage : stages) {
        hjstudy::logFields(
            stage.name,
            "stage",
            {{"responsibility", stage.responsibility}, {"thread", stage.threadRole}});
    }
}
