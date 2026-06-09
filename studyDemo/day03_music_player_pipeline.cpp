/*
 * Day 03: MusicPlayer pipeline practice.
 *
 * Study plan: study/week1-music-player-practice.md
 * Study note: studyNote/week1-music-player-notes.md
 *
 * HJMedia reference source:
 * - docs/Readme_MusicPlayer.md
 * - docs/architecture/HJGraphMusicPlayer_AudioContextGuide.md
 * - docs/architecture/HJGraphMusicPlayer.md
 */

#include "study_demo_common.h"

namespace {

class MusicPipeline {
public:
    void openUrl(std::string url)
    {
        url_ = std::move(url);
        hjstudy::logFields("graph", "openURL", {{"url", url_}});
    }

    void playUntilEof()
    {
        demux();
        decode();
        resample();
        render();
        reportEof();
    }

private:
    void demux()
    {
        compressedPackets_ = {
            {0, hjstudy::MediaType::Audio, true, 0, "aac-packet-0"},
            {23, hjstudy::MediaType::Audio, false, 0, "aac-packet-1"},
            {46, hjstudy::MediaType::Audio, false, 0, "aac-packet-2"},
        };
        demuxerEof_ = true;
        hjstudy::logLine("demuxer", "source has no more packets, demuxer EOF is true");
    }

    void decode()
    {
        for (const auto& packet : compressedPackets_) {
            decodedFrames_.push_back({packet.ptsMs, packet.type, packet.keyFrame, packet.generation, "pcm(" + packet.payload + ")"});
        }
        hjstudy::logLine("decoder", "compressed packets converted to PCM frames");
    }

    void resample()
    {
        for (auto& frame : decodedFrames_) {
            frame.payload = "s16-stereo-48k(" + frame.payload + ")";
        }
        hjstudy::logLine("resampler", "PCM format normalized for renderer");
    }

    void render()
    {
        for (const auto& frame : decodedFrames_) {
            visiblePlaybackHeadMs_ = frame.ptsMs;
            hjstudy::logFields("render", "consume", {{"ptsMs", std::to_string(frame.ptsMs)}, {"payload", frame.payload}});
        }
        finalPlaybackEof_ = true;
    }

    void reportEof() const
    {
        hjstudy::logFields(
            "graph",
            "eof-state",
            {
                {"demuxerEof", hjstudy::yesNo(demuxerEof_)},
                {"finalPlaybackEof", hjstudy::yesNo(finalPlaybackEof_)},
                {"timelineMs", std::to_string(visiblePlaybackHeadMs_)},
            });
    }

    std::string url_;
    std::vector<hjstudy::Frame> compressedPackets_;
    std::vector<hjstudy::Frame> decodedFrames_;
    bool demuxerEof_{false};
    bool finalPlaybackEof_{false};
    int64_t visiblePlaybackHeadMs_{0};
};

} // namespace

int main()
{
    hjstudy::printReferences(
        "study/week1-music-player-practice.md Day 3",
        "studyNote/week1-music-player-notes.md Day 3",
        {
            "docs/Readme_MusicPlayer.md",
            "docs/architecture/HJGraphMusicPlayer_AudioContextGuide.md",
            "docs/architecture/HJGraphMusicPlayer.md",
        });

    MusicPipeline pipeline;
    pipeline.openUrl("file:///demo/music.aac");
    pipeline.playUntilEof();
}
