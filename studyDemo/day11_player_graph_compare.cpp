/*
 * Day 11: Player graph comparison practice.
 *
 * Study plan: study/week2-thread-plugin-player-practice.md
 * Study note:
 * - studyNote/11-player-graphs-compare.md
 * - studyNote/week2-thread-plugin-player-notes.md Day 11
 *
 * HJMedia reference source:
 * - src/graphs/HJGraphLivePlayer.h
 * - src/graphs/HJGraphLivePlayer.cpp
 * - src/graphs/HJGraphVodPlayer.h
 * - src/graphs/HJGraphVodPlayer.cpp
 * - src/graphs/HJGraphMusicPlayer.h
 * - src/graphs/HJGraphMusicPlayer.cpp
 */

#include "study_demo_common.h"

namespace {

struct PlayerGraphTraits {
    std::string name;
    std::string scenario;
    std::vector<std::string> audioPath;
    std::vector<std::string> videoPath;
    std::vector<std::string> sharedApis;
    std::vector<std::string> uniqueApis;
    std::string backpressureOwner;
    std::string eofPolicy;
    bool seek{};
    bool pause{};
    bool lowLatency{};
    bool dropping{};
};

struct BacklogSnapshot {
    int audioBufferedMs{};
    int videoBufferedFrames{};
    int renderBufferedFrames{};
};

std::string join(const std::vector<std::string>& values, std::string_view separator)
{
    std::ostringstream out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << separator;
        }
        out << values[i];
    }
    return out.str();
}

void printGraphTraits(const PlayerGraphTraits& graph)
{
    hjstudy::printTitle(graph.name);
    hjstudy::logFields(
        graph.name,
        "position",
        {
            {"scenario", graph.scenario},
            {"seek", hjstudy::yesNo(graph.seek)},
            {"pause", hjstudy::yesNo(graph.pause)},
            {"lowLatency", hjstudy::yesNo(graph.lowLatency)},
            {"droppingPlugin", hjstudy::yesNo(graph.dropping)},
        });
    hjstudy::logFields(
        graph.name,
        "pipeline",
        {
            {"audio", join(graph.audioPath, " -> ")},
            {"video", graph.videoPath.empty() ? "none" : join(graph.videoPath, " -> ")},
        });
    hjstudy::logFields(
        graph.name,
        "api",
        {
            {"shared", join(graph.sharedApis, ",")},
            {"unique", join(graph.uniqueApis, ",")},
        });
    hjstudy::logFields(
        graph.name,
        "runtime-policy",
        {
            {"backpressureOwner", graph.backpressureOwner},
            {"eof", graph.eofPolicy},
        });
}

bool liveDroppingCanDeliverAudio(const BacklogSnapshot& backlog)
{
    return backlog.audioBufferedMs <= 300;
}

bool liveDroppingCanDeliverVideo(const BacklogSnapshot& backlog)
{
    return backlog.videoBufferedFrames <= 30;
}

bool liveVideoDecoderCanDeliver(const BacklogSnapshot& backlog)
{
    return backlog.renderBufferedFrames < 2;
}

bool vodOrMusicDemuxerCanDeliverAudio(const BacklogSnapshot& backlog)
{
    return backlog.audioBufferedMs <= 600;
}

bool vodDemuxerCanDeliverVideo(const BacklogSnapshot& backlog)
{
    return backlog.videoBufferedFrames <= 30;
}

void printBackpressureDecision(std::string_view graphName, const BacklogSnapshot& backlog)
{
    if (graphName == "LivePlayer") {
        hjstudy::logFields(
            graphName,
            "can-deliver",
            {
                {"audioBufferedMs", std::to_string(backlog.audioBufferedMs)},
                {"videoBufferedFrames", std::to_string(backlog.videoBufferedFrames)},
                {"renderBufferedFrames", std::to_string(backlog.renderBufferedFrames)},
                {"droppingAudio", hjstudy::yesNo(liveDroppingCanDeliverAudio(backlog))},
                {"droppingVideo", hjstudy::yesNo(liveDroppingCanDeliverVideo(backlog))},
                {"videoDecoder", hjstudy::yesNo(liveVideoDecoderCanDeliver(backlog))},
            });
        return;
    }

    if (graphName == "VodPlayer") {
        hjstudy::logFields(
            graphName,
            "can-deliver",
            {
                {"audioBufferedMs", std::to_string(backlog.audioBufferedMs)},
                {"videoBufferedFrames", std::to_string(backlog.videoBufferedFrames)},
                {"demuxerAudio", hjstudy::yesNo(vodOrMusicDemuxerCanDeliverAudio(backlog))},
                {"demuxerVideo", hjstudy::yesNo(vodDemuxerCanDeliverVideo(backlog))},
            });
        return;
    }

    hjstudy::logFields(
        graphName,
        "can-deliver",
        {
            {"audioBufferedMs", std::to_string(backlog.audioBufferedMs)},
            {"demuxerAudio", hjstudy::yesNo(vodOrMusicDemuxerCanDeliverAudio(backlog))},
        });
}

void printLiveStutterPlaybook()
{
    hjstudy::printTitle("live stutter playbook");
    const std::vector<std::string> steps = {
        "1 check audioDuration/videoFrames from EVENT_* counters",
        "2 find first blocked plugin: demuxer, dropping, decoder, or render",
        "3 if dropping backlog is high, prefer low latency and let dropping catch up",
        "4 if decoder cannot deliver, inspect render frame count and render thread",
        "5 on demuxer/codec error, reset demuxer instead of waiting for normal EOF",
    };
    for (const auto& step : steps) {
        hjstudy::logLine("LivePlayer", step);
    }
}

} // namespace

int main()
{
    hjstudy::printReferences(
        "study/week2-thread-plugin-player-practice.md Day 11",
        "studyNote/11-player-graphs-compare.md",
        {
            "src/graphs/HJGraphLivePlayer.cpp",
            "src/graphs/HJGraphVodPlayer.cpp",
            "src/graphs/HJGraphMusicPlayer.cpp",
        });

    const std::vector<std::string> commonApis = {
        "openURL",
        "close",
        "setMute/isMuted",
        "setVolume/getVolume",
        "getDuration",
        "switchAudioTrack",
    };

    const std::vector<PlayerGraphTraits> graphs = {
        {
            "LivePlayer",
            "live audio/video stream",
            {"demuxer", "dropping", "audioDecoder", "audioResampler", "speedControl", "audioRender"},
            {"demuxer", "dropping", "videoDecoder", "videoMainRender"},
            commonApis,
            {"setEnableSEIUUids", "first-frame event", "stuck heartbeat"},
            "dropping plugin and video decoder",
            "demuxer EOF or codec/network error resets demuxer; graph keeps live session alive",
            false,
            false,
            true,
            true,
        },
        {
            "VodPlayer",
            "file or on-demand audio/video",
            {"demuxer", "audioDecoder", "audioResampler", "audioRender"},
            {"demuxer", "videoDecoder", "videoRender"},
            commonApis,
            {"pause/resume", "getCurrentTimestamp", "seek"},
            "demuxer and video decoder",
            "demuxer EOF records last stream, render EOF reports graph EOF after enabled streams finish",
            true,
            true,
            false,
            false,
        },
        {
            "MusicPlayer",
            "pure audio playback",
            {"demuxer", "audioDecoder", "audioResampler", "audioRender"},
            {},
            commonApis,
            {"pause/resume", "getCurrentTimestamp", "seek", "getAudioTrackIds", "setRepeats", "openRecorder"},
            "demuxer audio output",
            "demuxer EOF may reset for repeat; final audioRender EOF reports graph EOF",
            true,
            true,
            false,
            false,
        },
    };

    for (const auto& graph : graphs) {
        printGraphTraits(graph);
    }

    hjstudy::printTitle("backpressure comparison");
    const std::vector<BacklogSnapshot> snapshots = {
        {120, 8, 1},
        {420, 34, 3},
        {700, 18, 1},
    };
    for (const auto& backlog : snapshots) {
        for (const auto& graph : graphs) {
            printBackpressureDecision(graph.name, backlog);
        }
    }

    printLiveStutterPlaybook();
}
