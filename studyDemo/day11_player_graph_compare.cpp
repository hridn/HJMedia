/*
 * Day 11: Player graph comparison practice.
 *
 * Study plan: study/week2-thread-plugin-player-practice.md
 * Study note: studyNote/week2-thread-plugin-player-notes.md
 *
 * HJMedia reference source:
 * - src/graphs/HJGraphLivePlayer.h
 * - src/graphs/HJGraphLivePlayer.cpp
 * - src/graphs/HJGraphVodPlayer.h
 * - src/graphs/HJGraphVodPlayer.cpp
 * - src/graphs/HJGraphMusicPlayer.h
 * - src/graphs/HJGraphMusicPlayer.cpp
 */

#include "study_demo_common.h"

namespace {

struct PlayerGraphTraits {
    std::string name;
    std::string scenario;
    bool supportsSeek{};
    bool lowLatencyFirst{};
    bool mayDropFrames{};
    std::string eofMeaning;
};

} // namespace

int main()
{
    hjstudy::printReferences(
        "study/week2-thread-plugin-player-practice.md Day 11",
        "studyNote/week2-thread-plugin-player-notes.md Day 11",
        {
            "src/graphs/HJGraphLivePlayer.cpp",
            "src/graphs/HJGraphVodPlayer.cpp",
            "src/graphs/HJGraphMusicPlayer.cpp",
        });

    const std::vector<PlayerGraphTraits> graphs = {
        {"MusicPlayer", "pure audio", true, false, false, "render consumed all audio"},
        {"LivePlayer", "live stream", false, true, true, "network stream ended or connection closed"},
        {"VodPlayer", "file/on-demand", true, false, false, "file playback reached duration"},
    };

    for (const auto& graph : graphs) {
        hjstudy::logFields(
            graph.name,
            "traits",
            {
                {"scenario", graph.scenario},
                {"supportsSeek", hjstudy::yesNo(graph.supportsSeek)},
                {"lowLatencyFirst", hjstudy::yesNo(graph.lowLatencyFirst)},
                {"mayDropFrames", hjstudy::yesNo(graph.mayDropFrames)},
                {"eof", graph.eofMeaning},
            });
    }
}
