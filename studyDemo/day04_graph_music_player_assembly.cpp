/*
 * Day 04: HJGraphMusicPlayer assembly practice.
 *
 * Study plan: study/week1-music-player-practice.md
 * Study note: studyNote/week1-music-player-notes.md
 *
 * HJMedia reference source:
 * - src/graphs/HJGraphMusicPlayer.h
 * - src/graphs/HJGraphMusicPlayer.cpp
 * - src/graphs/HJGraph.h
 * - src/graphs/HJGraph.cpp
 */

#include "study_demo_common.h"

namespace {

struct Component {
    std::string name;
    std::string role;
    std::string ownerThread;
};

class MusicGraphAssembly {
public:
    void internalInit()
    {
        add({"graph-thread", "serializes control operations such as seek", "graph"});
        add({"audio-thread", "runs audio decode/resample work", "audio"});
        add({"render-thread", "supports render-side async behavior", "render"});
        add({"timeline", "reports render-driven playback position", "graph/render"});
        add({"demuxer", "opens source and outputs compressed packets", "audio"});
        add({"decoder", "decodes compressed audio packets to PCM", "audio"});
        add({"resampler", "normalizes PCM format for output", "audio"});
        add({"audio-render", "consumes PCM and drives playback head", "render"});
    }

    void connect()
    {
        edges_ = {
            {"demuxer", "decoder"},
            {"decoder", "resampler"},
            {"resampler", "audio-render"},
            {"audio-render", "timeline"},
        };
    }

    void print() const
    {
        hjstudy::printTitle("components");
        for (const auto& component : components_) {
            hjstudy::logFields(
                component.name,
                "created",
                {{"role", component.role}, {"thread", component.ownerThread}});
        }

        hjstudy::printTitle("edges");
        for (const auto& edge : edges_) {
            std::cout << edge.first << " -> " << edge.second << '\n';
        }
    }

private:
    void add(Component component)
    {
        components_.push_back(std::move(component));
    }

    std::vector<Component> components_;
    std::vector<std::pair<std::string, std::string>> edges_;
};

} // namespace

int main()
{
    hjstudy::printReferences(
        "study/week1-music-player-practice.md Day 4",
        "studyNote/week1-music-player-notes.md Day 4",
        {
            "src/graphs/HJGraphMusicPlayer.h",
            "src/graphs/HJGraphMusicPlayer.cpp",
            "src/graphs/HJGraph.h",
            "src/graphs/HJGraph.cpp",
        });

    MusicGraphAssembly graph;
    graph.internalInit();
    graph.connect();
    graph.print();
}
