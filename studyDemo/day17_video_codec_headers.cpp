/*
 * Day 17: Video codec header practice.
 *
 * Study plan: .agents/skills/hjmedia-daily-study/references/28-day-plan.md Day 17
 * Study note: studyNote/17-video-capture-codec.md
 *
 * HJMedia reference source:
 * - src/graphs/HJGraphPusher.cpp
 * - src/plugins/hsys/HJPluginVideoOHEncoder.cpp
 * - src/media/codec/hsys/HJVEncOHCodec.cc
 * - src/plugins/HJPluginCodec.cpp
 * - src/plugins/HJPluginAVInterleave.cpp
 * - src/media/HJMediaFrame.h
 * - src/media/HJMediaInfo.h
 */

#include "study_demo_common.h"

namespace {

enum class CodecFamily {
    H264,
    H265,
};

std::string toString(CodecFamily codec)
{
    return codec == CodecFamily::H264 ? "H.264/AVC" : "H.265/HEVC";
}

std::string expectedHeader(CodecFamily codec)
{
    return codec == CodecFamily::H264 ? "SPS/PPS" : "VPS/SPS/PPS";
}

struct EncodedVideoPacket {
    CodecFamily codec{CodecFamily::H264};
    int64_t ptsMs{};
    int64_t dtsMs{};
    bool keyFrame{};
    bool codecData{};
    bool containsCodecHeader{};
    std::string nalus;
};

class CodecHeaderCache {
public:
    void observeCodecData(const EncodedVideoPacket& packet)
    {
        if (!packet.codecData) {
            return;
        }

        // 对应 HJVEncOHCodec::getFrame() 中的 AVCODEC_BUFFER_FLAGS_CODEC_DATA：
        // 硬编码器先吐出 codec data，HJMedia 缓存在 m_headerBuf/m_keyCodecParams，
        // 后续遇到同步帧时再和 IDR 合成完整可解码的关键帧 packet。
        headers_[packet.codec] = packet.nalus;
        hjstudy::logFields(
            "header-cache",
            "store-codec-data",
            {
                {"codec", toString(packet.codec)},
                {"header", packet.nalus},
            });
    }

    std::optional<EncodedVideoPacket> normalize(EncodedVideoPacket packet) const
    {
        if (packet.codecData) {
            return std::nullopt;
        }

        if (!packet.keyFrame || packet.containsCodecHeader) {
            return packet;
        }

        const auto header = headers_.find(packet.codec);
        if (header == headers_.end()) {
            hjstudy::logFields(
                "video-packet",
                "drop-keyframe-missing-header",
                {
                    {"codec", toString(packet.codec)},
                    {"ptsMs", std::to_string(packet.ptsMs)},
                    {"expected", expectedHeader(packet.codec)},
                });
            return std::nullopt;
        }

        // 对应 HJVEncOHCodec::getFrame() 中 keyBuf 的拼接：
        // 同步帧必须携带 SPS/PPS 或 VPS/SPS/PPS，否则 RTMP/录制端从关键帧开始也无法初始化解码器。
        packet.containsCodecHeader = true;
        packet.nalus = header->second + " + " + packet.nalus;
        return packet;
    }

private:
    std::map<CodecFamily, std::string> headers_;
};

class DtsMonotonicChecker {
public:
    bool accept(const EncodedVideoPacket& packet)
    {
        // HJPluginAVInterleave::runTask() 按 DTS 预览/弹出音视频帧。
        // 推流链路中 DTS 不应倒退；PTS 可表示显示时间，在含 B 帧时可能和 DTS 不同。
        if (lastDtsMs_ && packet.dtsMs < *lastDtsMs_) {
            hjstudy::logFields(
                "mux-clock",
                "reject-non-monotonic-dts",
                {
                    {"ptsMs", std::to_string(packet.ptsMs)},
                    {"dtsMs", std::to_string(packet.dtsMs)},
                    {"lastDtsMs", std::to_string(*lastDtsMs_)},
                });
            return false;
        }

        lastDtsMs_ = packet.dtsMs;
        return true;
    }

private:
    std::optional<int64_t> lastDtsMs_;
};

void processStream(const std::vector<EncodedVideoPacket>& packets)
{
    CodecHeaderCache cache;
    DtsMonotonicChecker clock;

    for (const auto& packet : packets) {
        cache.observeCodecData(packet);

        auto normalized = cache.normalize(packet);
        if (!normalized) {
            continue;
        }
        if (!clock.accept(*normalized)) {
            continue;
        }

        hjstudy::logFields(
            "video-packet",
            "deliver-es",
            {
                {"codec", toString(normalized->codec)},
                {"ptsMs", std::to_string(normalized->ptsMs)},
                {"dtsMs", std::to_string(normalized->dtsMs)},
                {"keyFrame", hjstudy::yesNo(normalized->keyFrame)},
                {"hasHeader", hjstudy::yesNo(normalized->containsCodecHeader)},
                {"nalus", normalized->nalus},
            });
    }
}

} // namespace

int main()
{
    hjstudy::printReferences(
        ".agents/skills/hjmedia-daily-study/references/28-day-plan.md Day 17",
        "studyNote/17-video-capture-codec.md",
        {
            "src/graphs/HJGraphPusher.cpp",
            "src/plugins/hsys/HJPluginVideoOHEncoder.cpp",
            "src/media/codec/hsys/HJVEncOHCodec.cc",
            "src/plugins/HJPluginCodec.cpp",
            "src/plugins/HJPluginAVInterleave.cpp",
            "src/media/HJMediaFrame.h",
            "src/media/HJMediaInfo.h",
        });

    hjstudy::printTitle("H.264 header + IDR");
    processStream({
        {CodecFamily::H264, 0, 0, false, true, true, "SPS/PPS"},
        {CodecFamily::H264, 0, 0, true, false, false, "IDR"},
        {CodecFamily::H264, 33, 33, false, false, false, "P"},
        {CodecFamily::H264, 66, 66, false, false, false, "P"},
    });

    hjstudy::printTitle("H.265 header + IDR");
    processStream({
        {CodecFamily::H265, 0, 0, false, true, true, "VPS/SPS/PPS"},
        {CodecFamily::H265, 0, 0, true, false, false, "IDR_W_RADL"},
        {CodecFamily::H265, 33, 33, false, false, false, "TRAIL_R"},
    });

    hjstudy::printTitle("bad order cases");
    processStream({
        {CodecFamily::H264, 0, 0, true, false, false, "IDR"},
        {CodecFamily::H264, 0, 0, false, true, true, "SPS/PPS"},
        {CodecFamily::H264, 99, 99, true, false, false, "IDR"},
        {CodecFamily::H264, 132, 132, false, false, false, "P"},
        {CodecFamily::H264, 120, 100, false, false, false, "B(display-after-P)"},
        {CodecFamily::H264, 90, 80, false, false, false, "late-dts"},
    });
}
