/*
 * Day 27: Mock interview scoring practice.
 *
 * Study plan: study/week4-interview-job-ready-practice.md
 * Study note: studyNote/week4-interview-job-ready-notes.md
 *
 * HJMedia reference source:
 * - study/00-four-week-study-outline.md
 * - studyDemo demos from day01 to day26
 * - studyNote week note frameworks
 */

#include "study_demo_common.h"

namespace {

struct TopicScore {
    std::string topic;
    int score{};
    std::string nextAction;
};

std::string level(int score)
{
    if (score >= 8) {
        return "interview-ready";
    }
    if (score >= 5) {
        return "needs one more review";
    }
    return "must review";
}

} // namespace

int main()
{
    hjstudy::printReferences(
        "study/week4-interview-job-ready-practice.md Day 27",
        "studyNote/week4-interview-job-ready-notes.md Day 27",
        {
            "study/00-four-week-study-outline.md",
            "studyDemo/day01_project_map.cpp",
            "studyDemo/day26_debug_playbook_part2.cpp",
        });

    const std::vector<TopicScore> scores = {
        {"project architecture", 8, "practice 3-minute introduction"},
        {"MusicPlayer chain", 9, "prepare source walkthrough"},
        {"thread teardown", 7, "review weak task demo"},
        {"RTMP weak network", 6, "review day19 and day21"},
        {"codec details", 5, "review SPS/PPS/AAC frame notes"},
    };

    for (const auto& score : scores) {
        hjstudy::logFields(
            "mock-interview",
            score.topic,
            {{"score", std::to_string(score.score)}, {"level", level(score.score)}, {"next", score.nextAction}});
    }
}
