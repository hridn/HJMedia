/*
 * Day 08: Task queue and handler practice.
 *
 * Study plan: study/week2-thread-plugin-player-practice.md
 * Study note: studyNote/week2-thread-plugin-player-notes.md
 *
 * HJMedia reference source:
 * - src/utils/HJThread/doc/README.md
 * - src/utils/HJThread/doc/HJLooperThread.md
 * - src/utils/HJThread/doc/HJLooper.md
 * - src/utils/HJThread/doc/HJHandler.md
 */

#include "study_demo_common.h"

using namespace std::chrono_literals;

int main()
{
    hjstudy::printReferences(
        "study/week2-thread-plugin-player-practice.md Day 8",
        "studyNote/week2-thread-plugin-player-notes.md Day 8",
        {
            "src/utils/HJThread/doc/README.md",
            "src/utils/HJThread/doc/HJLooperThread.md",
            "src/utils/HJThread/doc/HJLooper.md",
            "src/utils/HJThread/doc/HJHandler.md",
        });

    hjstudy::TaskRunner graphHandler;

    graphHandler.post([] {
        hjstudy::logFields("graph-handler", "openURL", {{"thread", "serialized worker"}});
    });
    graphHandler.postDelayed(30ms, [] {
        hjstudy::logFields("graph-handler", "seek", {{"reason", "coalesce and serialize control action"}});
    });
    graphHandler.post([] {
        hjstudy::logFields("graph-handler", "pause", {{"thread", "same serialized worker"}});
    });

    std::this_thread::sleep_for(80ms);
}
