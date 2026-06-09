/*
 * Day 06: Audio plugin chain practice.
 *
 * Study plan: study/week1-music-player-practice.md
 * Study note: studyNote/week1-music-player-notes.md
 *
 * HJMedia reference source:
 * - src/plugins/doc/HJTimeline.md
 * - src/plugins/doc/HJPluginDemuxer.md
 * - src/plugins/doc/HJPluginAudioFFDecoder.md
 * - src/plugins/doc/HJPluginAudioResampler.md
 * - src/plugins/doc/HJPluginAudioRender.md
 */

#include "study_demo_common.h"

namespace {

class Timeline {
public:
    void update(int64_t ptsMs)
    {
        currentMs_ = std::max(currentMs_, ptsMs);
    }

    int64_t currentMs() const
    {
        return currentMs_;
    }

private:
    int64_t currentMs_{0};
};

class AudioChain {
public:
    explicit AudioChain(Timeline& timeline)
        : timeline_(timeline)
    {
    }

    void run()
    {
        auto packets = demux();
        auto frames = decode(packets);
        resample(frames);
        render(frames);
        hjstudy::logFields("timeline", "current", {{"currentMs", std::to_string(timeline_.currentMs())}});
    }

private:
    std::vector<hjstudy::Frame> demux() const
    {
        hjstudy::logLine("demuxer", "output compressed audio packets");
        return {
            {0, hjstudy::MediaType::Audio, true, 0, "aac"},
            {21, hjstudy::MediaType::Audio, false, 0, "aac"},
            {42, hjstudy::MediaType::Audio, false, 0, "aac"},
        };
    }

    std::vector<hjstudy::Frame> decode(const std::vector<hjstudy::Frame>& packets) const
    {
        std::vector<hjstudy::Frame> frames;
        for (auto packet : packets) {
            packet.payload = "decoded-pcm";
            frames.push_back(std::move(packet));
        }
        hjstudy::logLine("audio-decoder", "packet -> PCM");
        return frames;
    }

    void resample(std::vector<hjstudy::Frame>& frames) const
    {
        for (auto& frame : frames) {
            frame.payload = "48k-s16-stereo";
        }
        hjstudy::logLine("audio-resampler", "normalize sample rate / format / channel");
    }

    void render(const std::vector<hjstudy::Frame>& frames)
    {
        for (const auto& frame : frames) {
            timeline_.update(frame.ptsMs);
            hjstudy::logFields("audio-render", "consume", {{"ptsMs", std::to_string(frame.ptsMs)}, {"payload", frame.payload}});
        }
    }

    Timeline& timeline_;
};

} // namespace

int main()
{
    hjstudy::printReferences(
        "study/week1-music-player-practice.md Day 6",
        "studyNote/week1-music-player-notes.md Day 6",
        {
            "src/plugins/doc/HJTimeline.md",
            "src/plugins/doc/HJPluginDemuxer.md",
            "src/plugins/doc/HJPluginAudioFFDecoder.md",
            "src/plugins/doc/HJPluginAudioResampler.md",
            "src/plugins/doc/HJPluginAudioRender.md",
        });

    Timeline timeline;
    AudioChain chain(timeline);
    chain.run();
}
