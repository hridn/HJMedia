/*
 * Day 14: Week 2 async model review practice.
 *
 * Study plan: study/week2-thread-plugin-player-practice.md
 * Study note: studyNote/week2-thread-plugin-player-notes.md
 *
 * HJMedia reference source:
 * - src/utils/HJThread/doc/README.md
 * - src/plugins/doc/HJPlugin.md
 * - src/graphs/HJGraphMusicPlayer.cpp
 */

#include "study_demo_common.h"

namespace {

struct DiagnosticRule {
    std::string symptom;
    std::string firstLogsToCheck;
    std::string likelyRisk;
};

} // namespace

int main()
{
    hjstudy::printReferences(
        "study/week2-thread-plugin-player-practice.md Day 14",
        "studyNote/week2-thread-plugin-player-notes.md Day 14",
        {
            "src/utils/HJThread/doc/README.md",
            "src/plugins/doc/HJPlugin.md",
            "src/graphs/HJGraphMusicPlayer.cpp",
        });

    const std::vector<DiagnosticRule> rules = {
        {"seek does not take effect", "seek id, generation, demuxer flush, render queue", "old asynchronous work survived"},
        {"close crashes occasionally", "handler lifecycle, weak target, delayed tasks", "callback after release"},
        {"audio progress keeps moving", "timeline updates and render completion", "timeline source is not clamped"},
        {"plugin chain stalls", "deliver/receive/runTask queue size", "downstream backpressure"},
    };

    for (const auto& rule : rules) {
        hjstudy::logFields(
            "diagnostic",
            rule.symptom,
            {{"logs", rule.firstLogsToCheck}, {"risk", rule.likelyRisk}});
    }
}
