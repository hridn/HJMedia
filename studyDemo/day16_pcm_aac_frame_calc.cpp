/*
 * Day 16: PCM and AAC frame calculation practice.
 *
 * Study plan: study/week3-pusher-codec-rtmp-practice.md
 * Study note: studyNote/16-audio-capture-aac.md
 *
 * HJMedia reference source:
 * - src/media/capture/hsys/HJACaptureOH.cc
 * - src/media/codec/HJAEncFDKAAC.cc
 * - src/plugins/hsys/HJPluginAudioOHCapturer.cpp
 * - src/plugins/HJPluginFDKAACEncoder.cpp
 */

#include "study_demo_common.h"

namespace {

struct PcmFormat {
    int sampleRate{};
    int channels{};
    int bytesPerSample{};
};

struct AacInputFrame {
    int index{};
    int samplesPerChannel{};
    int bytes{};
    int fdkaacNumInSamples{};
    double ptsMs{};
    double durationMs{};
};

int blockAlign(const PcmFormat& format)
{
    return format.channels * format.bytesPerSample;
}

int bytesPerSecond(const PcmFormat& format)
{
    return format.sampleRate * blockAlign(format);
}

int bytesForSamples(const PcmFormat& format, int samplesPerChannel)
{
    return samplesPerChannel * blockAlign(format);
}

int fdkaacNumInSamples(const PcmFormat& format, int samplesPerChannel)
{
    // 对应 HJAEncFDKAAC::run()：
    // m_inElemSize 固定为 2 字节，inArgs.numInSamples = m_inSize / m_inElemSize。
    // 因此双声道 1024 samples/channel 的 AAC-LC 输入是 2048 个 interleaved sample。
    return bytesForSamples(format, samplesPerChannel) / format.bytesPerSample;
}

double durationMs(const PcmFormat& format, int samplesPerChannel)
{
    return samplesPerChannel * 1000.0 / format.sampleRate;
}

std::string fixed3(double value)
{
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(3);
    out << value;
    return out.str();
}

class AacFramePacker {
public:
    AacFramePacker(PcmFormat format, int samplesPerFrame)
        : format_(format)
        , samplesPerFrame_(samplesPerFrame)
    {
    }

    std::vector<AacInputFrame> pushCaptureChunk(int samplesPerChannel)
    {
        // Harmony 采集回调 HJACaptureOH::OnReadData() 会把一段 S16 PCM 包成 AVFrame；
        // 真实 chunk 大小由系统回调决定，编码前需要按 AAC-LC 的 1024 samples/channel 重新聚合。
        bufferedSamples_ += samplesPerChannel;
        std::vector<AacInputFrame> readyFrames;
        while (bufferedSamples_ >= samplesPerFrame_) {
            readyFrames.push_back(AacInputFrame{
                nextFrameIndex_,
                samplesPerFrame_,
                bytesForSamples(format_, samplesPerFrame_),
                fdkaacNumInSamples(format_, samplesPerFrame_),
                nextPtsMs_,
                durationMs(format_, samplesPerFrame_),
            });
            bufferedSamples_ -= samplesPerFrame_;
            ++nextFrameIndex_;
            nextPtsMs_ += durationMs(format_, samplesPerFrame_);
        }
        return readyFrames;
    }

    int bufferedSamples() const
    {
        return bufferedSamples_;
    }

private:
    PcmFormat format_;
    int samplesPerFrame_;
    int bufferedSamples_{0};
    int nextFrameIndex_{0};
    double nextPtsMs_{0.0};
};

} // namespace

int main()
{
    hjstudy::printReferences(
        "study/week3-pusher-codec-rtmp-practice.md Day 16",
        "studyNote/16-audio-capture-aac.md",
        {
            "src/media/capture/hsys/HJACaptureOH.cc",
            "src/media/codec/HJAEncFDKAAC.cc",
            "src/plugins/hsys/HJPluginAudioOHCapturer.cpp",
            "src/plugins/HJPluginFDKAACEncoder.cpp",
        });

    const PcmFormat format{48000, 2, 2};
    const int aacLcSamplesPerFrame = 1024;
    hjstudy::logFields(
        "pcm-format",
        "s16-stereo-48k",
        {
            {"blockAlign", std::to_string(blockAlign(format))},
            {"bytesPerSecond", std::to_string(bytesPerSecond(format))},
            {"bytesPer10ms", std::to_string(bytesPerSecond(format) / 100)},
            {"bytesPer20ms", std::to_string(bytesPerSecond(format) / 50)},
            {"aac1024Bytes", std::to_string(bytesForSamples(format, aacLcSamplesPerFrame))},
            {"aac1024DurationMs", fixed3(durationMs(format, aacLcSamplesPerFrame))},
            {"fdkaacNumInSamples", std::to_string(fdkaacNumInSamples(format, aacLcSamplesPerFrame))},
        });

    AacFramePacker packer(format, aacLcSamplesPerFrame);
    for (const int capturedSamples : {480, 480, 480, 480, 480}) {
        const auto frames = packer.pushCaptureChunk(capturedSamples);
        hjstudy::logFields(
            "capture",
            "push-10ms-pcm",
            {
                {"capturedSamples", std::to_string(capturedSamples)},
                {"capturedBytes", std::to_string(bytesForSamples(format, capturedSamples))},
                {"readyFrames", std::to_string(frames.size())},
                {"bufferedSamples", std::to_string(packer.bufferedSamples())},
            });
        for (const auto& frame : frames) {
            hjstudy::logFields(
                "fdk-aac",
                "encode-input",
                {
                    {"index", std::to_string(frame.index)},
                    {"samplesPerChannel", std::to_string(frame.samplesPerChannel)},
                    {"bytes", std::to_string(frame.bytes)},
                    {"numInSamples", std::to_string(frame.fdkaacNumInSamples)},
                    {"ptsMs", fixed3(frame.ptsMs)},
                    {"durationMs", fixed3(frame.durationMs)},
                });
        }
    }
}
