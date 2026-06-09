/*
 * Day 23: Resume description practice.
 *
 * Study plan: study/week4-interview-job-ready-practice.md
 * Study note: studyNote/week4-interview-job-ready-notes.md
 *
 * HJMedia reference source:
 * - src/graphs/HJGraphMusicPlayer.cpp
 * - src/graphs/HJGraphPusher.cpp
 * - src/plugins/doc/HJPlugin.md
 */

#include "study_demo_common.h"

namespace {

struct ResumeBullet {
    std::string version;
    std::string text;
    std::vector<std::string> evidence;
};

} // namespace

int main()
{
    hjstudy::printReferences(
        "study/week4-interview-job-ready-practice.md Day 23",
        "studyNote/week4-interview-job-ready-notes.md Day 23",
        {
            "src/graphs/HJGraphMusicPlayer.cpp",
            "src/graphs/HJGraphPusher.cpp",
            "src/plugins/doc/HJPlugin.md",
        });

    const std::vector<ResumeBullet> bullets = {
        {
            "short",
            "Analyzed HJMedia's C++ audio/video graph and plugin architecture with focused practice demos.",
            {"MusicPlayer chain", "Plugin lifecycle", "Frame queue demo"},
        },
        {
            "standard",
            "Studied HJMedia player and pusher pipelines, including graph assembly, plugin lifecycle, async task scheduling, frame queues, seek/EOF handling, and weak-network backpressure.",
            {"HJGraphMusicPlayer", "HJPlugin", "Pusher graph", "network queue demo"},
        },
        {
            "technical",
            "Built standalone C++ practice demos based on HJMedia source to validate shared_ptr/weak_ptr ownership, RAII cleanup, bounded frame queues, handler-style delayed tasks, generation-based seek filtering, AV interleaving, and live push drop policies.",
            {"smart pointer demo", "task queue demo", "seek generation gate", "AV interleave demo"},
        },
    };

    for (const auto& bullet : bullets) {
        hjstudy::logFields("resume", bullet.version, {{"text", bullet.text}});
        for (const auto& item : bullet.evidence) {
            std::cout << "  evidence: " << item << '\n';
        }
    }
}
