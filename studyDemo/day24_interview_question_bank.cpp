/*
 * Day 24: Interview question bank practice.
 *
 * Study plan: study/week4-interview-job-ready-practice.md
 * Study note: studyNote/week4-interview-job-ready-notes.md
 *
 * HJMedia reference source:
 * - src/utils/HJObject.h
 * - src/plugins/doc/README.md
 * - src/graphs/HJGraphMusicPlayer.cpp
 * - src/graphs/HJGraphPusher.cpp
 */

#include "study_demo_common.h"

namespace {

struct Question {
    std::string category;
    std::string question;
    std::string sourceAnchor;
};

} // namespace

int main()
{
    hjstudy::printReferences(
        "study/week4-interview-job-ready-practice.md Day 24",
        "studyNote/week4-interview-job-ready-notes.md Day 24",
        {
            "src/utils/HJObject.h",
            "src/plugins/doc/README.md",
            "src/graphs/HJGraphMusicPlayer.cpp",
            "src/graphs/HJGraphPusher.cpp",
        });

    const std::vector<Question> questions = {
        {"C++", "Why does this project prefer shared_ptr/weak_ptr over raw owning pointers?", "src/utils/HJObject.h"},
        {"C++", "How does RAII reduce leak risk in media resource cleanup?", "HJOnceToken / ScopeExit style usage"},
        {"C++", "What is the role of mutex and condition_variable in a frame queue?", "src/plugins/doc/HJMediaFrameDeque.md"},
        {"Audio", "What is PCM and how do sample rate, channels, and bytes per sample affect bandwidth?", "day16 demo"},
        {"Audio", "Why does AAC encoding commonly need fixed-size input frames?", "day16 demo"},
        {"Player", "What is the MusicPlayer data path?", "docs/architecture/HJGraphMusicPlayer.md"},
        {"Player", "Why is demuxer EOF different from final playback EOF?", "docs/architecture/HJGraphMusicPlayer.md"},
        {"Thread", "Why should seek be serialized on a graph handler thread?", "src/utils/HJThread/doc/HJHandler.md"},
        {"Video", "Why can live playback drop non-key video frames under pressure?", "src/plugins/doc/HJPluginAVDropping.md"},
        {"Pusher", "Why can live push not buffer packets without limit?", "src/media/net"},
    };

    for (const auto& item : questions) {
        hjstudy::logFields(
            "question-bank",
            item.category,
            {{"question", item.question}, {"anchor", item.sourceAnchor}});
    }
}
