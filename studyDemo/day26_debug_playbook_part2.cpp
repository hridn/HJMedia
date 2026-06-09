/*
 * Day 26: Debug playbook part 2 and source walkthrough practice.
 *
 * Study plan: study/week4-interview-job-ready-practice.md
 * Study note: studyNote/week4-interview-job-ready-notes.md
 *
 * HJMedia reference source:
 * - src/graphs/HJGraphMusicPlayer.cpp
 * - src/plugins/doc/HJTimeline.md
 * - src/graphs/HJGraphPusher.cpp
 * - src/media/net
 */

#include "study_demo_common.h"

namespace {

struct Walkthrough {
    std::string target;
    std::string responsibility;
    std::string keyObjects;
    std::string threadModel;
    std::string risk;
};

} // namespace

int main()
{
    hjstudy::printReferences(
        "study/week4-interview-job-ready-practice.md Day 26",
        "studyNote/week4-interview-job-ready-notes.md Day 26",
        {
            "src/graphs/HJGraphMusicPlayer.cpp",
            "src/plugins/doc/HJTimeline.md",
            "src/graphs/HJGraphPusher.cpp",
            "src/media/net",
        });

    hjstudy::logFields(
        "debug-case",
        "audio ended but UI progress moves",
        {{"suspect", "timeline clamp and final render EOF"}, {"fix", "store max timestamp after final EOF and clamp future queries"}});

    hjstudy::logFields(
        "debug-case",
        "memory grows during weak network push",
        {{"suspect", "RTMP/network queue"}, {"fix", "bounded queue, drop policy, retry limit, bitrate adaptation"}});

    const Walkthrough walkthrough{
        "HJGraphMusicPlayer::internalInit",
        "create graph infrastructure, threads, timeline, plugins and plugin links",
        "m_thread, m_audioThread, m_renderThread, m_timeline, m_demuxer, m_audioDecoder, m_audioResampler, m_audioRender",
        "control actions serialized on graph handler; audio work and render work use dedicated roles",
        "teardown must clear plugins, delayed tasks, timeline, and callbacks in a safe order",
    };

    hjstudy::logFields(
        "walkthrough",
        walkthrough.target,
        {
            {"responsibility", walkthrough.responsibility},
            {"objects", walkthrough.keyObjects},
            {"thread", walkthrough.threadModel},
            {"risk", walkthrough.risk},
        });
}
