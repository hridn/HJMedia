/*
 * Day 23: HJGraphMusicPlayer 简历描述审计练习。
 *
 * 本 demo 不模拟完整播放器，而是把简历里的每一项陈述拆成“链路、技术点、
 * 难点、收益、边界”五个字段，再检查它们是否绑定到真实源码或已运行的练习。
 * 这样可以避免把“阅读源码 + standalone 验证”误写成“独立交付商业播放器”。
 *
 * 对应 HJMedia 源码语义：
 * - HJGraphMusicPlayer::internalInit 连接 demux -> decode -> resample -> render。
 * - HJPlugin::deliver 把帧写入下游插件自己的 input->mediaFrames。
 * - HJGraphMusicPlayer::seek 与 HJPluginDemuxer::seek 分两层投递异步 seek。
 * - registerQueryHandler_canPluginEof 区分 demuxer 源 EOF 与 render 最终 EOF。
 */

#include "study_demo_common.h"

namespace {

enum class EvidenceKind {
    SourceConfirmed,
    PracticeVerified,
};

struct Evidence {
    std::string id;
    EvidenceKind kind;
    std::string claim;
    std::string location;
    std::string safeWording;
};

struct ResumeVersion {
    std::string name;
    std::string chain;
    std::string technicalPoint;
    std::string difficulty;
    std::string benefit;
    std::string boundary;
    std::string text;
    std::vector<std::string> requiredTerms;
    std::vector<std::string> evidenceIds;
};

std::string toString(EvidenceKind kind)
{
    return kind == EvidenceKind::SourceConfirmed ? "源码确认" : "实践验证";
}

const Evidence* findEvidence(const std::vector<Evidence>& evidence, const std::string& id)
{
    const auto it = std::find_if(evidence.begin(), evidence.end(), [&id](const Evidence& item) {
        return item.id == id;
    });
    return it == evidence.end() ? nullptr : &*it;
}

std::vector<std::string> auditResumeVersion(
    const ResumeVersion& version,
    const std::vector<Evidence>& evidence)
{
    std::vector<std::string> errors;
    const auto requireField = [&errors](std::string_view name, const std::string& value) {
        if (value.empty()) {
            errors.emplace_back("缺少字段: " + std::string{name});
        }
    };

    // Day 23 的四个验收维度必须显式存在，不能只靠一句泛化描述带过。
    requireField("链路", version.chain);
    requireField("技术点", version.technicalPoint);
    requireField("难点", version.difficulty);
    requireField("收益", version.benefit);
    requireField("能力边界", version.boundary);
    requireField("简历文本", version.text);

    // 结构化字段只是审计输入，最终投到简历里的 text 也必须真的覆盖这些锚点。
    for (const auto& term : version.requiredTerms) {
        if (version.text.find(term) == std::string::npos) {
            errors.emplace_back("简历文本缺少关键词: " + term);
        }
    }

    // 边界字段必须同时说明源码分析与独立小练习，防止夸大项目所有权。
    if (version.boundary.find("源码分析") == std::string::npos
        || version.boundary.find("standalone") == std::string::npos) {
        errors.emplace_back("能力边界必须包含“源码分析”和“standalone”");
    }
    if (version.text.find("源码分析") == std::string::npos
        || (version.text.find("standalone") == std::string::npos
            && version.text.find("独立小程序") == std::string::npos)) {
        errors.emplace_back("最终简历文本没有直接说明源码分析与独立验证边界");
    }

    if (version.evidenceIds.empty()) {
        errors.emplace_back("没有绑定证据");
    }
    for (const auto& id : version.evidenceIds) {
        if (findEvidence(evidence, id) == nullptr) {
            errors.emplace_back("未知证据 ID: " + id);
        }
    }

    // 这些措辞暗示了未发生的商业交付、线上事故或量化收益，本练习直接判失败。
    const std::vector<std::string> forbiddenClaims = {
        "独立开发 HJMedia",
        "独立开发了 HJMedia",
        "主导 HJMedia",
        "解决了线上",
        "上线播放器",
        "性能提升",
    };
    for (const auto& phrase : forbiddenClaims) {
        if (version.text.find(phrase) != std::string::npos) {
            errors.emplace_back("包含无证据的强陈述: " + phrase);
        }
    }
    return errors;
}

void printEvidenceLedger(const std::vector<Evidence>& evidence)
{
    hjstudy::printTitle("陈述到证据账本");
    for (const auto& item : evidence) {
        hjstudy::logFields("evidence", item.id, {
            {"claim", item.claim},
            {"kind", toString(item.kind)},
            {"location", item.location},
            {"safeWording", item.safeWording},
        });
    }
}

void printResumeVersion(const ResumeVersion& version)
{
    hjstudy::printTitle(version.name);
    hjstudy::logFields("resume-fields", "required", {
        {"benefit", version.benefit},
        {"boundary", version.boundary},
        {"chain", version.chain},
        {"difficulty", version.difficulty},
        {"technicalPoint", version.technicalPoint},
    });
    hjstudy::logLine("resume-text", version.text);
}

} // namespace

int main()
{
    hjstudy::printReferences(
        ".agents/skills/hjmedia-daily-study/references/28-day-plan.md Day 23",
        "studyNote/23-resume-description.md",
        {
            "src/graphs/HJGraphMusicPlayer.cpp",
            "src/graphs/HJGraph.cpp",
            "src/plugins/HJPlugin.cpp",
            "src/plugins/HJPluginDemuxer.cpp",
            "src/plugins/HJPluginAudioFFDecoder.cpp",
            "src/plugins/HJPluginAudioResampler.cpp",
            "src/plugins/HJPluginAudioRender.cpp",
            "src/utils/HJThread/HJLooperThread.cpp",
        });

    const std::vector<Evidence> evidence = {
        {
            "chain",
            EvidenceKind::SourceConfirmed,
            "MusicPlayer 连接 demux、audio decoder、audio resampler、平台 audio render",
            "HJGraphMusicPlayer::internalInit + HJGraph::connectPlugins",
            "我追踪并画出了 MusicPlayer 的四段音频插件链。",
        },
        {
            "consumer-queue",
            EvidenceKind::SourceConfirmed,
            "deliver 将媒体帧写入下游插件 Input::mediaFrames，receive 再取出",
            "HJPlugin::deliver / HJPlugin::receive / HJPlugin::deliverToOutputs",
            "我核对了帧进入下游输入队列并唤醒处理任务的路径。",
        },
        {
            "async-seek",
            EvidenceKind::SourceConfirmed,
            "graph 和 demuxer 各自用 handler 清理同 ID 待处理消息后投递 seek",
            "HJGraphMusicPlayer::seek + HJPluginDemuxer::seek + HJLooperThread::Handler::asyncAndClear",
            "我分析了异步 seek 的两层 handler 投递和 weak_ptr 生命周期边界。",
        },
        {
            "final-eof",
            EvidenceKind::SourceConfirmed,
            "demuxer EOF 负责 repeat/pending；纯音频条件下，匹配的 render EOF 清完 mediaType 后才触发 graph EOF",
            "HJPluginDemuxer::runEof + HJPluginAudioRender::fillAudioBuffer + HJGraphMusicPlayer::registerQueryHandler_canPluginEof",
            "我在纯音频 MusicPlayer 范围内区分了源耗尽与最终播放完成两种 EOF 语义。",
        },
        {
            "pipeline-demo",
            EvidenceKind::PracticeVerified,
            "standalone 程序演示压缩包到 PCM 渲染及最终 EOF",
            "studyDemo/day03_music_player_pipeline.cpp",
            "我用独立 C++ 小程序验证了对播放链路的理解。",
        },
        {
            "queue-demo",
            EvidenceKind::PracticeVerified,
            "standalone 程序比较有界队列的阻塞和丢弃策略",
            "studyDemo/day05_bounded_frame_queue.cpp",
            "我用模拟练习观察了容量、反压和丢帧的取舍。",
        },
        {
            "seek-demo",
            EvidenceKind::PracticeVerified,
            "standalone 程序对比 seek 后陈旧帧/EOF 的 broken 与 fixed 场景",
            "studyDemo/day13_seek_flush_eof_debug.cpp",
            "我模拟了陈旧数据风险；generation gate 是练习方案，不声称是 MusicPlayer 现有实现。",
        },
    };

    const std::vector<ResumeVersion> versions = {
        {
            "简短版",
            "demux -> decode -> resample -> render",
            "Graph/Plugin 连接与异步 seek",
            "源 EOF 和 render 最终 EOF 的状态边界",
            "沉淀可运行 demo 和源码走读材料",
            "个人源码分析与 standalone C++ 验证，不声称参与 HJMedia 商业交付",
            "基于 HJMedia 开源代码开展 MusicPlayer 源码分析与 C++ 验证，梳理 demux -> decode -> resample -> render 插件链，围绕异步 seek 与分阶段 EOF 做问题定位练习，并以独立小程序沉淀可运行案例和源码走读材料。",
            {"demux", "异步 seek", "EOF", "可运行"},
            {"chain", "async-seek", "final-eof", "pipeline-demo"},
        },
        {
            "普通版",
            "HJPluginFFDemuxer -> HJPluginAudioFFDecoder -> HJPluginAudioResampler -> 平台 audio render",
            "消费者侧输入队列、handler 异步 seek、repeat/EOF 协调",
            "避免把 API 返回当作 seek 完成，并区分源耗尽与实际播放完成",
            "形成链路图、源码证据表和三个针对性 C++17 练习",
            "个人源码分析与 standalone C++ 验证，不使用虚构线上指标",
            "以 HJGraphMusicPlayer 为主线完成开源源码分析与个人实践：从 internalInit 追踪 demux、音频解码、重采样到平台 render 的插件链和消费者输入队列，分析两层 handler 异步 seek 及 demuxer/render 的 EOF 协作；通过 standalone C++17 demo 模拟播放链、有界队列反压和 seek 后陈旧帧/EOF 风险，形成可编译案例、Mermaid 图和源码证据表。",
            {"demux", "handler", "EOF", "可编译"},
            {"chain", "consumer-queue", "async-seek", "final-eof", "pipeline-demo", "queue-demo", "seek-demo"},
        },
        {
            "技术强化版",
            "internalInit 三次 connectPlugins 建立四段音频数据路径",
            "deliver/receive 队列语义、同 ID 消息清理、weak_ptr、Query/Event Bus EOF 状态协作",
            "异步 seek 不能用返回值代表完成；demuxer EOF 不能提前等同 graph EOF",
            "建立陈述到 path + symbol 的可审计账本，并用小程序复现实验现象",
            "个人源码分析与 standalone C++ 验证；未修改或交付生产 MusicPlayer",
            "作为个人开源源码分析与验证，我围绕 HJGraphMusicPlayer::internalInit 与 HJGraph::connectPlugins 核对四段音频链，继续下钻 HJPlugin::deliver/receive 的下游输入队列语义；追踪 HJGraphMusicPlayer::seek -> graph Handler::asyncAndClear -> HJPluginDemuxer::seek -> demux Handler::asyncAndClear -> HJPluginDemuxer::runSeek，以及 QUERY_CAN_PLUGIN_EOF_ID 下 repeat/pending EOF 到 render 最终确认的控制路径；编写 standalone C++17 练习对比有界队列策略和 seek 陈旧帧/EOF 场景，产出可运行验证与逐项源码证据。",
            {"connectPlugins", "asyncAndClear", "QUERY_CAN_PLUGIN_EOF_ID", "可运行"},
            {"chain", "consumer-queue", "async-seek", "final-eof", "queue-demo", "seek-demo"},
        },
    };

    printEvidenceLedger(evidence);

    int failedCount = 0;
    for (const auto& version : versions) {
        printResumeVersion(version);
        const auto errors = auditResumeVersion(version, evidence);
        if (errors.empty()) {
            hjstudy::logFields("audit", "PASS", {
                {"evidenceCount", std::to_string(version.evidenceIds.size())},
                {"version", version.name},
            });
            continue;
        }

        ++failedCount;
        hjstudy::logFields("audit", "FAIL", {
            {"errorCount", std::to_string(errors.size())},
            {"version", version.name},
        });
        for (const auto& error : errors) {
            hjstudy::logLine("audit-error", error);
        }
    }

    hjstudy::logFields("summary", failedCount == 0 ? "PASS" : "FAIL", {
        {"failed", std::to_string(failedCount)},
        {"versions", std::to_string(versions.size())},
    });
    return failedCount == 0 ? 0 : 1;
}
