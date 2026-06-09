/*
 * Day 07: Week 1 review practice.
 *
 * Study plan: study/week1-music-player-practice.md
 * Study note: studyNote/week1-music-player-notes.md
 *
 * HJMedia reference source:
 * - docs/Readme_MusicPlayer.md
 * - src/graphs/HJGraphMusicPlayer.h
 * - src/plugins/doc/README.md
 */

#include "study_demo_common.h"

namespace {

struct InterviewCard {
    std::string question;
    std::string answerHint;
};

} // namespace

int main()
{
    hjstudy::printReferences(
        "study/week1-music-player-practice.md Day 7",
        "studyNote/week1-music-player-notes.md Day 7",
        {
            "docs/Readme_MusicPlayer.md",
            "src/graphs/HJGraphMusicPlayer.h",
            "src/plugins/doc/README.md",
        });

    const std::vector<InterviewCard> cards = {
        {"HJMedia 是什么？", "跨平台 C++ 多媒体框架，核心产品链路包含 Player、Pusher、MusicPlayer。"},
        {"MusicPlayer 主链路是什么？", "demuxer -> decoder -> resampler -> audio render -> timeline。"},
        {"为什么使用 weak_ptr？", "避免 Graph/Plugin/Node 相互引用时产生循环引用。"},
        {"为什么需要 frame queue？", "解耦生产和消费线程，并支撑反压、flush、丢帧策略。"},
        {"demuxer EOF 和最终 EOF 有什么区别？", "前者表示源不再产包，后者表示 render 已消费到终点。"},
    };

    hjstudy::printTitle("Week 1 interview cards");
    for (const auto& card : cards) {
        std::cout << "Q: " << card.question << '\n';
        std::cout << "A: " << card.answerHint << "\n\n";
    }
}
