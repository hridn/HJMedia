/*
 * Day 22: STAR project point practice.
 *
 * Study plan: study/week4-interview-job-ready-practice.md
 * Study note: studyNote/week4-interview-job-ready-notes.md
 *
 * HJMedia reference source:
 * - docs/Readme_MusicPlayer.md
 * - src/graphs/HJGraphMusicPlayer.cpp
 * - src/plugins/doc/README.md
 */

#include "study_demo_common.h"

namespace {

struct StarStory {
    std::string situation;
    std::string task;
    std::string action;
    std::string result;
};

void printStory(const StarStory& story)
{
    hjstudy::logFields("STAR", "situation", {{"text", story.situation}});
    hjstudy::logFields("STAR", "task", {{"text", story.task}});
    hjstudy::logFields("STAR", "action", {{"text", story.action}});
    hjstudy::logFields("STAR", "result", {{"text", story.result}});
}

} // namespace

int main()
{
    hjstudy::printReferences(
        "study/week4-interview-job-ready-practice.md Day 22",
        "studyNote/week4-interview-job-ready-notes.md Day 22",
        {
            "docs/Readme_MusicPlayer.md",
            "src/graphs/HJGraphMusicPlayer.cpp",
            "src/plugins/doc/README.md",
        });

    const StarStory story{
        "Studied HJMedia as a cross-platform C++ audio/video framework.",
        "Build an interview-ready understanding of MusicPlayer and plugin scheduling.",
        "Mapped graph/plugin/thread responsibilities and implemented small C++ demos for queues and stale tasks.",
        "Can explain demux/decode/resample/render, seek/EOF risk, and frame queue backpressure with source references.",
    };

    printStory(story);
}
