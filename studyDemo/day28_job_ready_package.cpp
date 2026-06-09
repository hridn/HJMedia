/*
 * Day 28: Job-ready package validation practice.
 *
 * Study plan: study/week4-interview-job-ready-practice.md
 * Study note: studyNote/week4-interview-job-ready-notes.md
 *
 * HJMedia reference source:
 * - study/00-four-week-study-outline.md
 * - study/week1-music-player-practice.md
 * - study/week2-thread-plugin-player-practice.md
 * - study/week3-pusher-codec-rtmp-practice.md
 * - study/week4-interview-job-ready-practice.md
 */

#include "study_demo_common.h"

namespace {

struct Deliverable {
    std::string name;
    bool ready{};
    std::string evidence;
};

} // namespace

int main()
{
    hjstudy::printReferences(
        "study/week4-interview-job-ready-practice.md Day 28",
        "studyNote/week4-interview-job-ready-notes.md Day 28",
        {
            "study/00-four-week-study-outline.md",
            "study/week1-music-player-practice.md",
            "study/week2-thread-plugin-player-practice.md",
            "study/week3-pusher-codec-rtmp-practice.md",
            "study/week4-interview-job-ready-practice.md",
        });

    const std::vector<Deliverable> deliverables = {
        {"resume bullet", true, "day23"},
        {"3-minute project intro", true, "day27"},
        {"10-minute source walkthrough", true, "day26"},
        {"50 interview questions", false, "day24 needs expansion from 10 anchors to 50 full Q&A"},
        {"5 debugging cases", true, "day25/day26"},
        {"C++ practice demos", true, "day01-day28"},
    };

    int readyCount = 0;
    for (const auto& item : deliverables) {
        if (item.ready) {
            ++readyCount;
        }
        hjstudy::logFields(
            "job-package",
            item.name,
            {{"ready", hjstudy::yesNo(item.ready)}, {"evidence", item.evidence}});
    }

    hjstudy::logFields(
        "job-package",
        "summary",
        {{"readyCount", std::to_string(readyCount)}, {"total", std::to_string(deliverables.size())}});
}
