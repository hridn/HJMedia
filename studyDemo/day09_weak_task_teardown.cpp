/*
 * Day 09: weak_ptr based delayed task teardown practice.
 *
 * Study plan: study/week2-thread-plugin-player-practice.md
 * Study note: studyNote/week2-thread-plugin-player-notes.md
 *
 * HJMedia reference source:
 * - src/utils/HJThread/doc/HJMessageQueue.md
 * - src/utils/HJThread/doc/HJMessage.md
 * - src/graphs/HJGraphMusicPlayer.cpp
 */

#include "study_demo_common.h"

#include <memory>

using namespace std::chrono_literals;

namespace {

class PlayerSession : public std::enable_shared_from_this<PlayerSession> {
public:
    using Ptr = std::shared_ptr<PlayerSession>;

    explicit PlayerSession(std::string name)
        : name_(std::move(name))
    {
        hjstudy::logLine(name_, "created");
    }

    ~PlayerSession()
    {
        hjstudy::logLine(name_, "destroyed");
    }

    std::weak_ptr<PlayerSession> weak()
    {
        return shared_from_this();
    }

    void seek(int64_t timestampMs)
    {
        hjstudy::logFields(name_, "seek", {{"timestampMs", std::to_string(timestampMs)}});
    }

private:
    std::string name_;
};

} // namespace

int main()
{
    hjstudy::printReferences(
        "study/week2-thread-plugin-player-practice.md Day 9",
        "studyNote/week2-thread-plugin-player-notes.md Day 9",
        {
            "src/utils/HJThread/doc/HJMessageQueue.md",
            "src/utils/HJThread/doc/HJMessage.md",
            "src/graphs/HJGraphMusicPlayer.cpp",
        });

    hjstudy::TaskRunner handler;

    auto session = std::make_shared<PlayerSession>("music-player");
    std::weak_ptr<PlayerSession> weakSession = session->weak();

    handler.postDelayed(50ms, [weakSession] {
        if (auto locked = weakSession.lock()) {
            locked->seek(3000);
        } else {
            hjstudy::logLine("graph-handler", "skip stale delayed seek because session expired");
        }
    });

    session.reset();
    std::this_thread::sleep_for(90ms);
}
