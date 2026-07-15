/*
 * Day 24：50 个面试问答题库审计 demo。
 *
 * 学习目标：
 * 1. 不只记问题，而是为每题绑定 HJMedia 的源码 symbol 与复习落点；
 * 2. 自动检查六类题目数量、编号连续性和证据字段，防止“看起来有 50 题”；
 * 3. 通过命令行按分类或编号抽题，配合 studyNote/24-interview-qa.md 口述答案。
 *
 * 这不是 HJMedia 生产代码的缩写实现；它是 standalone C++17 学习工具。
 */

#include "study_demo_common.h"

namespace {

struct Question {
    int id{};
    std::string category;
    std::string question;
    std::string sourceAnchor;
    std::string reviewPoint;
};

const std::vector<Question> kQuestions = {
    {1, "C++", "HJ_DECLARE_PUWTR 统一了哪些指针别名？", "src/utils/HJMacros.h — HJ_DECLARE_PUWTR", "Ptr/Utr/Wtr 只是类型别名，所有权仍由具体成员关系决定"},
    {2, "C++", "sharedFrom(this) 为什么要求对象已由 shared_ptr 管理？", "src/utils/HJObject.h — HJObject::sharedFrom", "shared_from_this 与 dynamic_pointer_cast 的前置条件"},
    {3, "C++", "Plugin 拓扑为什么用 weak_ptr 保存相邻插件？", "src/plugins/HJPlugin.cpp — deliverToOutputs/receive", "lock 失败即跳过，避免把连接边变成强引用环"},
    {4, "C++", "HJOnceToken 如何体现 RAII？", "src/utils/HJUtilitys.h — HJOnceToken::~HJOnceToken", "析构执行清理回调，覆盖正常返回和提前退出"},
    {5, "C++", "HJ_AUTO_LOCK 与 HJ_AUTOU_LOCK 有什么区别？", "src/utils/HJMacros.h — HJ_AUTO_LOCK/HJ_AUTOU_LOCK", "lock_guard 与 unique_lock 的能力差异"},
    {6, "C++", "HJMediaFrameDeque 如何保证队列与统计同步更新？", "src/plugins/HJMediaFrameDeque.cpp — deliver/receive", "同一同步锁内修改 deque、EOF、音频与视频统计"},
    {7, "C++", "asyncAndClear 真能取消所有旧任务吗？", "src/utils/HJThread/HJLooperThread.cpp — Handler::asyncAndClear", "只删除同 handler、同 message id 的待处理消息"},
    {8, "C++", "HJMessageQueue 如何按时间调度消息？", "src/utils/HJThread/HJMessageQueue.cpp — enqueueMessage/next", "按 when 插入；next 等待到期并丢弃失效 target"},
    {9, "C++", "HJMediaNode 如何防止重复调度自身？", "src/core/HJMediaNode.h — HJMediaNode::asyncSelf", "递归锁保护 m_isBusy，投递失败时恢复标志"},
    {10, "C++", "HJSyncObject 的 init/done 生命周期如何收口？", "src/utils/HJSyncObject.h — init/done", "状态迁移、失败回滚和 internalRelease"},

    {11, "音频", "PCM 字节率由哪些参数决定？", "src/media/HJMediaInfo.h — HJAudioInfo", "sample rate × channels × bytes per sample"},
    {12, "音频", "为什么队列既统计 duration 又统计 samples？", "src/plugins/HJMediaFrameDeque.cpp — deliver; src/plugins/HJPlugin.cpp — reportFrameDequeInfo", "PCM 可用 samples/sampleRate 计算时长，压缩帧可退回 frame duration"},
    {13, "音频", "FDK AAC-LC 每帧输入样本数是多少？", "src/media/codec/HJAEncFDKAAC.cc — HJAEncFDKAAC::init", "AAC-LC 设置 1024；不是脱离 codec 配置的通用常量"},
    {14, "音频", "PCM 怎样进入 AAC 编码器并产出 packet？", "src/media/codec/HJAEncFDKAAC.cc — HJAEncFDKAAC::run", "取 AVFrame 数据、设置 numInSamples、aacEncEncode、封装 AVPacket"},
    {15, "音频", "重采样何时重建 SwrContext？", "src/media/HJAudioConverter.cc — HJAudioConverter::convert", "声道布局、采样格式或采样率变化时重建"},
    {16, "音频", "AudioResampler 为什么可选 FIFO？", "src/plugins/HJPluginAudioResampler.cpp — internalInit/processMediaFrame", "转换后按目标 frame size 重新分帧，适配固定样本数下游"},
    {17, "音频", "音频渲染无帧时为什么填静音？", "src/plugins/HJPluginAudioRender.cpp — fillAudioBuffer", "上报 buffering 并清零设备缓冲，避免使用未初始化数据"},
    {18, "音频", "音频 render 何时推进 timeline、何时确认 EOF？", "src/plugins/HJPluginAudioRender.cpp — fillAudioBuffer", "完整消费 PCM 帧后更新 PTS；EOF 先查询 graph 再完成播放"},

    {19, "视频", "H.264 与 H.265 参数集在源码中如何区分？", "src/media/HJMediaInfo.h — HJ_NAL_TYPE/HJ_HEVC_NAL_TYPE", "H.264 为 SPS/PPS，H.265 增加 VPS"},
    {20, "视频", "Harmony 硬编的 codec-data 怎样处理？", "src/media/codec/hsys/HJVEncOHCodec.cc — HJVEncOHCodec::getFrame", "缓存为 m_headerBuf 和 codec params，不当作普通画面直接下发"},
    {21, "视频", "为什么同步帧需要拼接参数集？", "src/media/codec/hsys/HJVEncOHCodec.cc — HJVEncOHCodec::getFrame", "SYNC_FRAME 分支将 headerBuf 放在编码数据前并标记关键帧"},
    {22, "视频", "PTS 与 DTS 在推流链路中分别解决什么问题？", "src/plugins/HJPluginAVInterleave.cpp — runTask", "交织比较 DTS；PTS 保留在 frame 中供展示时间使用"},
    {23, "视频", "Harmony 视频编码为什么使用 Surface 输入？", "src/plugins/hsys/HJPluginVideoOHEncoder.cpp — internalInit/internalRelease", "通过 surfaceCb 交付 NativeWindow，释放时反向通知"},
    {24, "视频", "LivePlayer 如何选择软解和 Harmony 硬解？", "src/graphs/HJGraphLivePlayer.cpp — internalInit", "deviceType 决定分支，OHCodec 路径受 HarmonyOS 宏约束"},
    {25, "视频", "直播追帧为什么要保留关键帧边界？", "src/plugins/HJMediaFrameDeque.cpp — dropFrames", "控制帧不丢，并避免丢到队列中没有可保留关键帧"},
    {26, "视频", "为什么 video decoder 不能无限向 render 投递？", "src/graphs/HJGraphLivePlayer.cpp — m_canVideoDecoderDeliverToOutput", "render 队列达到 2 帧即反压 decoder"},

    {27, "播放器", "MusicPlayer 的真实主链路是什么？", "src/graphs/HJGraphMusicPlayer.cpp — internalInit", "demuxer → audio decoder → resampler → 平台 audio render"},
    {28, "播放器", "Plugin 帧队列位于生产者还是消费者？", "src/plugins/HJPlugin.cpp — deliver/receive", "deliver 写目标插件 Input::mediaFrames，receive 消费并唤醒上游"},
    {29, "播放器", "LivePlayer 为什么在 demuxer 后插入 AVDropping？", "src/graphs/HJGraphLivePlayer.cpp — internalInit/registerQueryHandler_canDeliverToOutputs", "先聚合音视频 backlog，再按实时性约束反压和追帧"},
    {30, "播放器", "Live、VOD、Music 三类 Graph 的核心取舍是什么？", "src/graphs/HJGraph.cpp — HJGraphPlayer::createGraph; src/graphs/HJGraph*Player.cpp — internalInit", "选择不同 Graph；差异必须继续落到各 internalInit 与 handler"},
    {31, "播放器", "MusicPlayer::seek 为什么是异步请求？", "src/graphs/HJGraphMusicPlayer.cpp — seek", "graph handler 清同 ID 待处理 seek 后，用 weak demuxer 投递新任务"},
    {32, "播放器", "demuxer seek 成功后清理哪些状态？", "src/plugins/HJPluginDemuxer.cpp — seek/runSeek", "停 runTask、执行底层 seek、清 currentFrame、flush 下游并报告成功"},
    {33, "播放器", "为什么 demuxer EOF 不等于播放完成？", "src/graphs/HJGraphMusicPlayer.cpp — registerQueryHandler_canPluginEof", "源耗尽后仍等 audio render 消费到对应 stream EOF"},
    {34, "播放器", "MusicPlayer repeat 与 final EOF 如何协调？", "src/graphs/HJGraphMusicPlayer.cpp — registerQueryHandler_canPluginEof/setRepeats", "未到次数就 reset，最后一轮先标 pending 再由 render 确认"},
    {35, "播放器", "timeline 如何在暂停和播放时计算当前时间？", "src/plugins/HJTimeline.cpp — getTimestamp/pause/play", "暂停返回冻结 PTS；播放用 steady clock 增量乘 speed"},
    {36, "播放器", "pause/resume 的调用顺序为什么重要？", "src/graphs/HJGraphMusicPlayer.cpp — pause/resume", "pause 先冻结 timeline 再停 render；resume 先启 render 再续 timeline"},

    {37, "推流", "Pusher 音频链路如何组装？", "src/graphs/HJGraphPusher.cpp — internalInit", "Harmony capture 条件边 → resampler → FDK AAC → AVInterleave"},
    {38, "推流", "Pusher 视频硬编链在哪些条件下成立？", "src/graphs/HJGraphPusher.cpp — internalInit", "videoInfo 存在且编译 HarmonyOS 时创建 VideoOHEncoder 并接入交织"},
    {39, "推流", "AVInterleave 如何决定先发音频还是视频？", "src/plugins/HJPluginAVInterleave.cpp — runTask", "preview 两路队首并选择 DTS 较小者"},
    {40, "推流", "Muxer 为什么要从关键视频帧开始写？", "src/plugins/HJPluginMuxer.cpp — dropping/runTask", "含视频时初始 dropping，遇到 key frame 才放行"},
    {41, "推流", "RTMPMuxer 如何等待音视频起始时间基准？", "src/media/muxer/HJRTMPMuxer.cc — waitStartDTSOffset", "双流齐备后取首个音视频 DTS 的较小值并回放暂存帧"},
    {42, "推流", "编码帧怎样变成待发送 FLV tag？", "src/media/muxer/HJRTMPMuxer.cc — addRTMPPacket", "HJFLVPacket::init 后进入 HJRTMPPacketManager"},
    {43, "推流", "弱网时为什么不能无限缓存？", "src/media/muxer/HJRTMPPacketManager.cc — push/drop/dropFrames", "按队列 duration 和 packet priority 分级丢包，并反馈自适应码率"},
    {44, "推流", "RTMP 断线重试采用什么退避策略？", "src/media/muxer/HJRTMPAsyncWrapper.cc — onRTMPWrapperNotify/getRetryInterval", "错误事件异步重建 AVIO，指数间隔封顶 50 倍默认值"},

    {45, "工程", "HJMedia 如何区分平台编译分支？", "CMakeLists.txt — platform add_definitions", "WIN32_LIB/ANDROID_LIB/IOS_LIB/MACOS_LIB/LINUX_LIB/Harmony_LIB"},
    {46, "工程", "顶层 CMake 如何裁剪产品入口和可选能力？", "CMakeLists.txt — HJ_ENABLE_* / DISABLE_HJ*", "TNN/Prio 选项与 player/pusher/render/inference 子目录门控"},
    {47, "工程", "Plugin 生命周期的状态保护落在哪里？", "src/utils/HJSyncObject.h — init/done; src/plugins/HJPlugin.cpp — init/done", "同步状态机负责初始化回滚与释放，Plugin 追加状态事件"},
    {48, "工程", "HJLog 与 HJFLog 的职责有什么不同？", "src/utils/HJLog.h/HJFLog.h — HJLog*/HJFLog*", "HJLog 携带文件行函数；HJFLog 先做 fmt 格式化"},
    {49, "工程", "HJ_WOULD_BLOCK 为什么不是错误？", "src/utils/HJError.h — result/error definitions", "正值结果表示暂时无数据；负值 HJErr* 才是错误族"},
    {50, "工程", "没有统一 CTest 时怎样验证学习结论？", "studyDemo/CMakeLists.txt — add_study_demo", "用独立 C++17 target、可观察输出和源码证据表做小范围验证"},
};

const std::map<std::string, int> kExpectedCategoryCounts = {
    {"C++", 10},
    {"音频", 8},
    {"视频", 8},
    {"播放器", 10},
    {"推流", 8},
    {"工程", 6},
};

bool matchesFilter(const Question& item, const std::string& filter)
{
    if (filter.empty() || filter == "all") {
        return true;
    }
    if (item.category == filter) {
        return true;
    }
    try {
        return item.id == std::stoi(filter);
    } catch (const std::exception&) {
        return false;
    }
}

int auditQuestionBank()
{
    int failures = 0;
    std::map<std::string, int> actualCounts;
    std::map<int, bool> seenIds;

    // 编号、分类、问题、源码锚点和复习落点共同构成一条可审计题目记录。
    for (const auto& item : kQuestions) {
        actualCounts[item.category]++;
        if (item.id <= 0 || seenIds[item.id]) {
            ++failures;
            hjstudy::logFields("audit", "invalid-id", {{"id", std::to_string(item.id)}});
        }
        seenIds[item.id] = true;
        if (item.question.empty() || item.sourceAnchor.empty() || item.reviewPoint.empty()) {
            ++failures;
            hjstudy::logFields("audit", "empty-field", {{"id", std::to_string(item.id)}});
        }
    }

    if (kQuestions.size() != 50) {
        ++failures;
    }
    for (int id = 1; id <= 50; ++id) {
        if (!seenIds[id]) {
            ++failures;
            hjstudy::logFields("audit", "missing-id", {{"id", std::to_string(id)}});
        }
    }
    for (const auto& [category, expected] : kExpectedCategoryCounts) {
        const int actual = actualCounts[category];
        if (actual != expected) {
            ++failures;
        }
        hjstudy::logFields(
            "audit",
            category,
            {{"actual", std::to_string(actual)},
             {"expected", std::to_string(expected)},
             {"result", actual == expected ? "PASS" : "FAIL"}});
    }

    hjstudy::logFields(
        "audit",
        "summary",
        {{"failures", std::to_string(failures)},
         {"questions", std::to_string(kQuestions.size())},
         {"result", failures == 0 ? "PASS" : "FAIL"}});
    return failures;
}

} // namespace

int main(int argc, char* argv[])
{
    hjstudy::printTitle("Day 24：HJMedia 50 题面试题库");
    hjstudy::printReferences(
        "study/week4-interview-job-ready-practice.md Day 24",
        "studyNote/24-interview-qa.md",
        {
            "src/utils/HJObject.h / HJSyncObject.h / HJThread",
            "src/plugins/HJPlugin.cpp / HJMediaFrameDeque.cpp",
            "src/graphs/HJGraphMusicPlayer.cpp / HJGraphLivePlayer.cpp",
            "src/graphs/HJGraphPusher.cpp",
            "src/media/codec / src/media/muxer",
            "CMakeLists.txt / src/utils/HJError.h / HJLog.h",
        });

    const int failures = auditQuestionBank();
    const std::string filter = argc > 1 ? argv[1] : "all";
    int shown = 0;

    // 默认输出全部题目；传入“音频”等分类或 1-50 的编号可做定向抽题。
    for (const auto& item : kQuestions) {
        if (!matchesFilter(item, filter)) {
            continue;
        }
        ++shown;
        hjstudy::logFields(
            "question",
            std::to_string(item.id) + "." + item.category,
            {{"question", item.question},
             {"review", item.reviewPoint},
             {"source", item.sourceAnchor}});
    }

    hjstudy::logFields(
        "review",
        "selection",
        {{"filter", filter}, {"shown", std::to_string(shown)}});

    return failures == 0 && shown > 0 ? 0 : 1;
}
