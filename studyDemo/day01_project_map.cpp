/*
 * Day 01: Project map.
 *
 * Study plan: study/week1-music-player-practice.md
 * Study note: studyNote/week1-music-player-notes.md
 *
 * HJMedia reference source:
 * - README.md
 * - CHANGELOG.md
 * - CMakeLists.txt
 * - CMakePresets.json
 * - src/graphs/CMakeLists.txt
 */

#include "study_demo_common.h"

#include <iomanip>

namespace {

struct Module {
    std::string name;
    std::string responsibility;
    std::vector<std::string> dependsOn;
};

class ArchitectureMap {
public:
    void add(Module module)
    {
        modules_.push_back(std::move(module));
    }

    void print() const
    {
        for (const auto& module : modules_) {
            std::cout << std::left << std::setw(14) << module.name << " : "
                      << module.responsibility << '\n';
            if (!module.dependsOn.empty()) {
                std::cout << "  depends on: ";
                for (std::size_t i = 0; i < module.dependsOn.size(); ++i) {
                    std::cout << module.dependsOn[i]
                              << (i + 1 == module.dependsOn.size() ? "" : ", ");
                }
                std::cout << '\n';
            }
        }
    }

private:
    std::vector<Module> modules_;
};

} // namespace

int main()
{
    hjstudy::printReferences(
        "study/week1-music-player-practice.md Day 1",
        "studyNote/week1-music-player-notes.md Day 1",
        {
            "README.md",
            "CMakeLists.txt",
            "CMakePresets.json",
            "src/graphs/CMakeLists.txt",
        });

    ArchitectureMap map;
    map.add({"entry", "Product-facing API wrappers and platform bridges", {"graphs"}});
    map.add({"graphs", "Assemble product pipelines such as pusher/player/music player", {"plugins", "core"}});
    map.add({"plugins", "Reusable media-processing units with lifecycle and queues", {"media", "utils"}});
    map.add({"core", "Node/graph runtime abstractions and scheduling model", {"utils"}});
    map.add({"media", "Codec, muxer, demuxer, render, capture and network primitives", {"externals", "third_party"}});
    map.add({"comp", "GPU processing, render pipeline, face effects and helper components", {"media"}});
    map.add({"third_party", "Source dependencies synchronized by DEPS and patches", {}});
    map.add({"externals", "Prebuilt platform-specific media libraries", {}});

    hjstudy::printTitle("HJMedia module map");
    map.print();

    hjstudy::printTitle("Five terms to keep tracking");
    for (const auto term : {"Graph", "Plugin", "Timeline", "Frame", "Demuxer"}) {
        std::cout << "- " << term << '\n';
    }
}
