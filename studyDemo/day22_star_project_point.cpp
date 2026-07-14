/*
 * Day 22: 以 HJGraphMusicPlayer 为主项目点的 STAR 表达练习。
 *
 * 这个 demo 的目的不是伪造“开发了完整播放器”的经历，而是把实际完成的
 * 源码阅读、链路梳理和独立 C++ 验证，组织成可被面试追问的项目表述。
 *
 * 对应 HJMedia 源码语义：
 * - HJGraphMusicPlayer::internalInit 组装 demux -> decode -> resample -> render。
 * - HJGraphMusicPlayer::seek 用 handler->asyncAndClear 合并快速连续的 seek。
 * - registerQueryHandler_canPluginEof 区分 demuxer EOF 与 render 最终播放 EOF。
 * - m_playbackStateMutex 保护 repeat、最终 EOF 和时间戳钳制等共享状态。
 */

#include "study_demo_common.h"

namespace {

struct Evidence {
    std::string claim;
    std::string proof;
    std::string safeWording;
};

struct StarStory {
    std::string situation;
    std::string task;
    std::string action;
    std::string result;
};

void printStory(const StarStory& story)
{
    hjstudy::logFields("STAR", "S", {{"text", story.situation}});
    hjstudy::logFields("STAR", "T", {{"text", story.task}});
    hjstudy::logFields("STAR", "A", {{"text", story.action}});
    hjstudy::logFields("STAR", "R", {{"text", story.result}});
}

void printEvidenceLedger(const std::vector<Evidence>& ledger)
{
    hjstudy::printTitle("Claim-to-evidence ledger");
    for (const auto& item : ledger) {
        // 面试表达应先说自己确实做过的动作，再引用源码事实；
        // 不把“阅读/模拟验证”偷换成“独立实现了 HJMedia”。
        hjstudy::logFields("evidence", "verified", {
            {"claim", item.claim},
            {"proof", item.proof},
            {"say", item.safeWording},
        });
    }
}

void printFollowupAnswers()
{
    hjstudy::printTitle("Likely follow-up answers");
    const std::vector<std::string> answers = {
        "主链路：demuxer 读压缩 packet，decoder 产出 PCM，resampler 适配输出格式，render 消费 PCM 并推动 timeline。",
        "seek：调用方不直接在当前线程 seek；graph 将持有 demuxer 的 weak_ptr 投给 handler->asyncAndClear，同类请求被清理合并。",
        "EOF：demuxer EOF 仅说明源耗尽；只有 render 消费到对应 stream 的尾部，graph 才报告 EVENT_GRAPH_EOF_ID。",
        "风险：seek 是异步的，close 的语义不能被默认理解为完整 teardown；状态共享还需要 mutex 和 done 检查。",
        "边界：这是对开源 HJMedia 的源码分析和 standalone C++ 验证，并非我独立交付了完整跨平台播放器。",
    };
    for (const auto& answer : answers) {
        hjstudy::logLine("follow-up", answer);
    }
}

} // namespace

int main()
{
    hjstudy::printReferences(
        ".agents/skills/hjmedia-daily-study/references/28-day-plan.md Day 22",
        "studyNote/22-resume-project-point.md",
        {
            "docs/Readme_MusicPlayer.md",
            "docs/architecture/HJGraphMusicPlayer.md",
            "docs/architecture/HJGraphMusicPlayer_AudioContextGuide.md",
            "src/graphs/HJGraphMusicPlayer.h",
            "src/graphs/HJGraphMusicPlayer.cpp",
            "studyDemo/day03_music_player_pipeline.cpp",
            "studyDemo/day13_seek_flush_eof_debug.cpp",
        });

    hjstudy::printTitle("Interview-ready STAR story");
    printStory({
        "在学习 HJMedia 这一跨平台 C++ 多媒体框架时，我选择纯音频 HJGraphMusicPlayer 作为主线，因为它覆盖 graph 组装、插件链、异步控制和 EOF 边界。",
        "我的目标是把分散的源码和文档整理成一条可讲清、可验证、能承受追问的 MusicPlayer 架构案例。",
        "我追踪了 internalInit/openURL/pause/resume/seek 及 EOF handler；绘制 demux-decode-resample-render 链路；并用 standalone demo 验证队列反压和 seek/flush/EOF 的陈旧帧风险。",
        "形成了带源码路径的学习笔记、可运行的小型验证程序和一套边界明确的讲解稿：能解释 seek 合并、render 驱动 timeline、repeat 与最终 EOF 的区别。",
    });

    printEvidenceLedger({
        {"播放器由四段音频链路组成", "HJGraphMusicPlayer::internalInit 连接 demuxer、audioDecoder、audioResampler、audioRender", "我分析并画出了 HJGraphMusicPlayer 的四段插件链。"},
        {"快速 seek 不会无界堆积", "HJGraphMusicPlayer::seek 调用 m_handler->asyncAndClear，并捕获 HJPluginFFDemuxer::Wtr", "我核对了源码中的 seek 合并与 weak_ptr 生命周期保护。"},
        {"源 EOF 不等于用户听到结束", "registerQueryHandler_canPluginEof 先记录 demuxer 最终 EOF，再由 audioRender 分支 report EVENT_GRAPH_EOF_ID", "我用时序图和 day13 模拟区分了源耗尽与最终播放完成。"},
        {"可以说明并发边界", "m_playbackStateMutex 保护 repeat、pending EOF、播放完成和 max timestamp 状态", "我把共享状态和异步 handler 作为审查重点，而非笼统地说线程安全。"},
    });

    printFollowupAnswers();
    return 0;
}
