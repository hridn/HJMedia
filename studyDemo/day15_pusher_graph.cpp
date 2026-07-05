/*
 * Day 15: Pusher graph overview practice.
 *
 * Study plan: study/week3-pusher-codec-rtmp-practice.md
 * Study note: studyNote/15-pusher-graph.md
 *
 * HJMedia reference source:
 * - src/graphs/HJGraphPusher.h
 * - src/graphs/HJGraphPusher.cpp
 * - src/entry/pusher/HJPusherInterface.h
 * - src/entry/pusher/hsys/bridge/HJPusherNapi.cpp
 * - src/entry/pusher/hsys/verify/HJNAPILiveStream.cpp
 * - examples/harmony/API.md
 */

#include "study_demo_common.h"

#include <deque>
#include <set>

namespace {

struct PluginNode {
    std::string name;
    std::string hjmediaClass;
    std::string responsibility;
    std::string threadRole;
};

struct Connection {
    std::string from;
    std::string to;
    std::string mediaType;
};

struct EncodedPacket {
    std::string stream;
    int64_t dtsMs{};
    bool keyFrame{};
};

std::string boolText(bool value)
{
    return value ? "true" : "false";
}

std::vector<PluginNode> buildPusherNodes(bool enableAudio, bool enableVideo)
{
    std::vector<PluginNode> nodes = {
        {"avInterleave", "HJPluginAVInterleave", "按 DTS 预览音频和视频输入，选择更早的 packet 下发",
         "插件 worker 线程"},
        {"rtmpMuxer", "HJPluginRTMPMuxer", "封装并发送到 RTMP，负责连接事件、丢帧和码率反馈",
         "mux/network worker 线程"},
    };

    if (enableAudio) {
        // HJGraphPusher 在 Harmony 下把音频采集、重采样和 AAC 编码串起来。
        // 这里的顺序对应 HJGraphPusher::internalInit 中的 connectPlugins 调用。
        nodes.push_back({"audioCapturer", "HJPluginAudioOHCapturer", "从 OH 音频设备采集 PCM，可被 setMute 控制",
                         "设备回调/采集线程"});
        nodes.push_back({"audioResampler", "HJPluginAudioResampler", "转换采样率/声道/格式，并用 FIFO 对齐 AAC 输入粒度",
                         "audioThread 或插件自建线程"});
        nodes.push_back({"audioEncoder", "HJPluginFDKAACEncoder", "把 PCM 编成 AAC packet",
                         "codec worker 线程"});
    }

    if (enableVideo) {
        // 视频预览和美颜处理在 HJEntryBaseRender / HJRteGraphProc 中完成；
        // HJGraphPusher 通过 surfaceCb 把处理后的画面送入 Harmony 硬编 Surface。
        nodes.push_back({"previewRender", "HJEntryBaseRender/HJRteGraphProc", "预览、GPU 后处理、FaceU/ROI 等图像处理",
                         "render/GL 线程"});
        nodes.push_back({"videoEncoder", "HJPluginVideoOHEncoder", "从编码 Surface 取 H.264/H.265 packet",
                         "OHCodec 回调/codec worker 线程"});
    }

    return nodes;
}

std::vector<Connection> buildPusherConnections(bool enableAudio, bool enableVideo)
{
    std::vector<Connection> connections = {
        {"avInterleave", "rtmpMuxer", "data"},
    };

    if (enableAudio) {
        connections.push_back({"audioCapturer", "audioResampler", "audio"});
        connections.push_back({"audioResampler", "audioEncoder", "audio"});
        connections.push_back({"audioEncoder", "avInterleave", "audio"});
    }

    if (enableVideo) {
        connections.push_back({"previewRender", "videoEncoder", "surface/frame"});
        connections.push_back({"videoEncoder", "avInterleave", "video"});
    }

    return connections;
}

void printGraphAssembly(bool enableAudio, bool enableVideo)
{
    hjstudy::printTitle("Section 1: HJGraphPusher assembly");
    hjstudy::logFields(
        "openPusher",
        "input",
        {{"audioInfo", boolText(enableAudio)}, {"videoInfo", boolText(enableVideo)}, {"mediaUrl", "rtmp://..."}});

    for (const auto& node : buildPusherNodes(enableAudio, enableVideo)) {
        hjstudy::logFields(
            node.name,
            "addPlugin",
            {{"class", node.hjmediaClass}, {"responsibility", node.responsibility}, {"thread", node.threadRole}});
    }

    for (const auto& connection : buildPusherConnections(enableAudio, enableVideo)) {
        // connectPlugins(src, dst, type) 会同时登记 src 的 output 和 dst 的 input。
        hjstudy::logFields(
            "connectPlugins",
            connection.mediaType,
            {{"from", connection.from}, {"to", connection.to}});
    }
}

void printApiToGraphControlFlow()
{
    hjstudy::printTitle("Section 2: Harmony API -> NAPI -> graph");

    const std::vector<std::pair<std::string, std::string>> calls = {
        {"HJPusher.contextInit", "HJPusherNapi::contextInit -> HJEntryContext::init"},
        {"createPusher", "new HJPusherBridge / HJNAPILiveStream"},
        {"openPreview", "initRender -> acquire SurfaceId for preview"},
        {"setWindow", "setBaseNativeWindow binds preview window"},
        {"openPusher", "build HJVideoInfo/HJAudioInfo/HJMediaUrl -> HJGraphPusher::init"},
        {"setMute", "HJGraphPusher::setMute -> HJPluginAudioOHCapturer::setMute"},
        {"openRecorder", "HJGraphPusher::openRecorder adds HJPluginFFMuxer branch"},
        {"closePusher", "HJGraphPusher::done releases plugins and looper threads"},
    };

    for (const auto& call : calls) {
        // 这一段用于面试复述：从 TS API 入口讲到 C++ graph 初始化参数。
        hjstudy::logFields("api-flow", call.first, {{"nativePath", call.second}});
    }
}

std::vector<EncodedPacket> interleaveByDts(std::deque<EncodedPacket> audio, std::deque<EncodedPacket> video)
{
    std::vector<EncodedPacket> output;
    while (!audio.empty() || !video.empty()) {
        const bool chooseAudio =
            !audio.empty() && (video.empty() || audio.front().dtsMs <= video.front().dtsMs);

        if (chooseAudio) {
            output.push_back(audio.front());
            audio.pop_front();
        } else {
            output.push_back(video.front());
            video.pop_front();
        }
    }
    return output;
}

void runInterleaveAndRecorderSimulation()
{
    hjstudy::printTitle("Section 3: AV interleave + RTMP/record branches");

    // 音频大约 20ms 一包，视频大约 33ms 一帧。
    // HJPluginAVInterleave 的核心不是“混合数据”，而是按 DTS 顺序选择下一包。
    std::deque<EncodedPacket> audioPackets = {
        {"audio", 0, false}, {"audio", 20, false}, {"audio", 40, false}, {"audio", 60, false},
    };
    std::deque<EncodedPacket> videoPackets = {
        {"video", 0, true}, {"video", 33, false}, {"video", 66, false},
    };

    bool recorderOpened = false;
    std::set<std::string> sinks = {"rtmpMuxer"};

    const auto interleaved = interleaveByDts(audioPackets, videoPackets);
    for (const auto& packet : interleaved) {
        if (!recorderOpened && packet.dtsMs >= 33) {
            // openRecorder 不是重建主链路，而是在 avInterleave 后面新增 FFMuxer 输出分支。
            recorderOpened = true;
            sinks.insert("ffMuxer");
            hjstudy::logLine("openRecorder", "connect avInterleave -> ffMuxer while RTMP keeps running");
        }

        for (const auto& sink : sinks) {
            hjstudy::logFields(
                sink,
                "writeFrame",
                {{"stream", packet.stream},
                 {"dtsMs", std::to_string(packet.dtsMs)},
                 {"keyFrame", boolText(packet.keyFrame)}});
        }
    }
}

void printNotificationMapping()
{
    hjstudy::printTitle("Section 4: callbacks and pressure signals");

    const std::vector<std::pair<std::string, std::string>> events = {
        {"HJRTMP_EVENT_STREAM_CONNECTED", "HJ_PUSHER_NOTIFY_CONNECT_SUCCESS"},
        {"HJRTMP_EVENT_DROP_FRAME", "HJ_PUSHER_NOTIFY_DROP_FRAME"},
        {"HJRTMP_EVENT_AUTOADJUST_BITRATE", "HJ_PUSHER_NOTIFY_AUTOADJUST_BITRATE + adjustBitrate"},
        {"HJ_PLUGIN_NOTIFY_ERROR_MUXER_WRITEFRAME", "HJ_PUSHER_NOTIFY_ERROR_MUXER_WRITEFRAME"},
        {"HJ_PLUGIN_NOTIFY_ERROR_CODEC_RUN", "HJ_PUSHER_NOTIFY_ERROR_CODEC_RUN"},
        {"HJ_PLUGIN_NOTIFY_ERROR_CAPTURER_GETFRAME", "HJ_PUSHER_NOTIFY_ERROR_CAPTURER_GETFRAME"},
    };

    for (const auto& event : events) {
        // HJGraphPusher::registerHandlers 和 HJNAPILiveStream::openPusher
        // 会把底层 RTMP/Plugin 事件转换成产品层 pusher notify。
        hjstudy::logFields("notify", event.first, {{"mappedTo", event.second}});
    }
}

} // namespace

int main()
{
    hjstudy::printReferences(
        "study/week3-pusher-codec-rtmp-practice.md Day 15",
        "studyNote/15-pusher-graph.md",
        {
            "src/graphs/HJGraphPusher.h",
            "src/graphs/HJGraphPusher.cpp",
            "src/entry/pusher/HJPusherInterface.h",
            "src/entry/pusher/hsys/bridge/HJPusherNapi.cpp",
            "src/entry/pusher/hsys/verify/HJNAPILiveStream.cpp",
            "src/plugins/HJPluginAVInterleave.cpp",
            "src/plugins/HJPluginMuxer.cpp",
            "src/plugins/HJPluginRTMPMuxer.cpp",
            "src/plugins/hsys/HJPluginAudioOHCapturer.cpp",
            "src/plugins/hsys/HJPluginVideoOHEncoder.cpp",
            "examples/harmony/API.md",
            "examples/harmony/hjpusher/src/main/ets/native/HJPusher.ets",
        });

    printGraphAssembly(true, true);
    printApiToGraphControlFlow();
    runInterleaveAndRecorderSimulation();
    printNotificationMapping();

    hjstudy::printTitle("Day 15 takeaway");
    hjstudy::logLine(
        "interview",
        "Pusher is a real-time graph: capture/process produces frames, encoders produce packets, AVInterleave orders them by DTS, and RTMP/record muxers consume the ordered stream with bounded latency in mind.");
    return 0;
}
