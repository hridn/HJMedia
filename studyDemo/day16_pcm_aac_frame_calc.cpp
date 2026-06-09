/*
 * Day 16: PCM and AAC frame calculation practice.
 *
 * Study plan: study/week3-pusher-codec-rtmp-practice.md
 * Study note: studyNote/week3-pusher-codec-rtmp-notes.md
 *
 * HJMedia reference source:
 * - src/media audio/capture/codec related implementations
 * - third_party/fdk-aac usage locations
 * - third_party/miniaudio usage locations
 */

#include "study_demo_common.h"

namespace {

struct PcmFormat {
    int sampleRate{};
    int channels{};
    int bytesPerSample{};
};

int bytesPerSecond(const PcmFormat& format)
{
    return format.sampleRate * format.channels * format.bytesPerSample;
}

int bytesForSamples(const PcmFormat& format, int samplesPerChannel)
{
    return samplesPerChannel * format.channels * format.bytesPerSample;
}

class AacFramePacker {
public:
    explicit AacFramePacker(int samplesPerFrame)
        : samplesPerFrame_(samplesPerFrame)
    {
    }

    std::vector<int> pushSamples(int samples)
    {
        bufferedSamples_ += samples;
        std::vector<int> readyFrames;
        while (bufferedSamples_ >= samplesPerFrame_) {
            readyFrames.push_back(samplesPerFrame_);
            bufferedSamples_ -= samplesPerFrame_;
        }
        return readyFrames;
    }

    int bufferedSamples() const
    {
        return bufferedSamples_;
    }

private:
    int samplesPerFrame_;
    int bufferedSamples_{0};
};

} // namespace

int main()
{
    hjstudy::printReferences(
        "study/week3-pusher-codec-rtmp-practice.md Day 16",
        "studyNote/week3-pusher-codec-rtmp-notes.md Day 16",
        {
            "src/media",
            "third_party/fdk-aac",
            "third_party/miniaudio",
        });

    const PcmFormat format{48000, 2, 2};
    hjstudy::logFields(
        "pcm",
        "format",
        {
            {"bytesPerSecond", std::to_string(bytesPerSecond(format))},
            {"bytesPer20ms", std::to_string(bytesPerSecond(format) / 50)},
            {"aac1024Bytes", std::to_string(bytesForSamples(format, 1024))},
        });

    AacFramePacker packer(1024);
    for (const int capturedSamples : {480, 480, 480, 480, 480}) {
        const auto frames = packer.pushSamples(capturedSamples);
        hjstudy::logFields(
            "aac-packer",
            "push",
            {
                {"capturedSamples", std::to_string(capturedSamples)},
                {"readyFrames", std::to_string(frames.size())},
                {"bufferedSamples", std::to_string(packer.bufferedSamples())},
            });
    }
}
