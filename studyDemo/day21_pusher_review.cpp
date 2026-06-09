/*
 * Day 21: Week 3 pusher review practice.
 *
 * Study plan: study/week3-pusher-codec-rtmp-practice.md
 * Study note: studyNote/week3-pusher-codec-rtmp-notes.md
 *
 * HJMedia reference source:
 * - src/graphs/HJGraphPusher.cpp
 * - src/media/muxer
 * - src/media/net
 */

#include "study_demo_common.h"

namespace {

struct CaseStep {
    std::string title;
    std::string detail;
};

} // namespace

int main()
{
    hjstudy::printReferences(
        "study/week3-pusher-codec-rtmp-practice.md Day 21",
        "studyNote/week3-pusher-codec-rtmp-notes.md Day 21",
        {
            "src/graphs/HJGraphPusher.cpp",
            "src/media/muxer",
            "src/media/net",
        });

    const std::vector<CaseStep> weakNetworkCase = {
        {"symptom", "memory grows and live latency increases"},
        {"suspect", "network sender slower than encoder/muxer producer"},
        {"logs", "packet pts, queue size, send error, retry count, drop count"},
        {"policy", "drop low priority video frames, reconnect, reduce bitrate"},
        {"risk", "dropping keyframes breaks decoder recovery; unlimited buffering breaks real time"},
    };

    for (const auto& step : weakNetworkCase) {
        hjstudy::logFields("weak-network-case", step.title, {{"detail", step.detail}});
    }
}
