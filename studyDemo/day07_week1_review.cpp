/*
 * Day 07: Week 1 review and interview expression practice.
 *
 * Study plan: study/week1-music-player-practice.md
 * Study note: studyNote/week1-music-player-notes.md
 *
 * HJMedia reference source:
 * - README.md
 * - docs/Readme_MusicPlayer.md
 * - docs/architecture/HJGraphMusicPlayer.md
 * - docs/architecture/HJGraphMusicPlayer_AudioContextGuide.md
 * - src/graphs/HJGraphMusicPlayer.h
 * - src/plugins/doc/README.md
 */

#include "study_demo_common.h"

namespace {

struct InterviewCard {
    std::string topic;
    std::string question;
    std::string answer;
    std::vector<std::string> sourcePaths;
};

struct ReviewCheckpoint {
    std::string name;
    std::vector<std::string> mustMention;
};

void printInterviewCards(const std::vector<InterviewCard>& cards)
{
    hjstudy::printTitle("1. Ten interview cards");
    for (std::size_t i = 0; i < cards.size(); ++i) {
        const auto& card = cards[i];
        std::cout << i + 1 << ". [" << card.topic << "] " << card.question << '\n';
        std::cout << "   Answer: " << card.answer << '\n';
        std::cout << "   Source:";
        for (const auto& path : card.sourcePaths) {
            std::cout << " " << path;
        }
        std::cout << "\n\n";
    }
}

void printThreeMinuteScript()
{
    hjstudy::printTitle("2. Three-minute MusicPlayer script");
    std::cout
        << "HJMedia is a cross-platform C++ media framework. In the player side, "
        << "MusicPlayer is the smallest complete chain because it keeps only audio: "
        << "source open, demux, decode, resample, audio render, and timeline.\n\n"
        << "The data path is demuxer -> audio decoder -> audio resampler -> audio render. "
        << "Demuxer reads compressed packets from a file or network source. Decoder turns "
        << "AAC/MP3-like packets into PCM. Resampler normalizes sample rate, sample format, "
        << "channel layout, and sometimes repacks short PCM frames into render-friendly "
        << "chunks. AudioRender writes PCM to the device and advances Timeline only after "
        << "the frame is actually consumed.\n\n"
        << "The control path is separate from the data path. openURL, seek, repeat, pause, "
        << "resume, and close are coordinated by Graph instead of being hidden inside each "
        << "plugin. This matters because seek and flush must clear queue data plus internal "
        << "states such as decoder codec state, resampler FIFO, render buffer, and timeline "
        << "anchor. Otherwise old frames can leak into the new playback generation.\n\n"
        << "There are two EOF concepts. Demuxer EOF only means the source has no more packets. "
        << "Final playback EOF happens later, when AudioRender has consumed all buffered PCM. "
        << "That gap is normal because decoder, resampler, queues, and render all hold data.\n\n"
        << "The C++ design choices support this pipeline. shared_ptr owns graph objects, weak_ptr "
        << "breaks bidirectional references, RAII protects locks and busy-state cleanup, and "
        << "frame queues decouple different thread speeds while enabling backpressure.\n";
}

void printCheckpoints(const std::vector<ReviewCheckpoint>& checkpoints)
{
    hjstudy::printTitle("3. Self-check before ending week 1");
    for (const auto& checkpoint : checkpoints) {
        std::cout << "- " << checkpoint.name << '\n';
        for (const auto& item : checkpoint.mustMention) {
            std::cout << "  * must mention: " << item << '\n';
        }
    }
}

void printWeakSpots()
{
    hjstudy::printTitle("4. Questions to carry into week 2");
    std::cout
        << "1. HJThread, HJHandler, HJTask, HJScheduler, and HJExecutor each own which part "
        << "of async execution?\n"
        << "2. Plugin lifecycle init/start/process/stop/release is called from which graph "
        << "thread, and which callbacks can race with teardown?\n"
        << "3. In a real player bug, which logs prove that EOF, flush, or seek reached every "
        << "stage instead of stopping halfway?\n";
}

std::vector<InterviewCard> buildCards()
{
    return {
        {
            "project map",
            "What is HJMedia, and how is the code organized?",
            "HJMedia is a cross-platform C++ media framework for pusher, player, render, "
            "inference, and music playback. The practical reading order is entry -> graphs "
            "-> plugins/core/media/comp -> third_party/externals.",
            {"README.md", "CMakeLists.txt", "src/graphs/CMakeLists.txt"},
        },
        {
            "graph/plugin",
            "Why does the project use Graph and Plugin instead of one long player class?",
            "Graph owns business orchestration: build topology, connect plugins, decide seek, "
            "repeat, EOF, and backpressure policy. Plugin owns one processing stage such as "
            "demux, decode, resample, render, capture, or encode.",
            {"src/graphs/HJGraphMusicPlayer.h", "src/plugins/doc/README.md"},
        },
        {
            "MusicPlayer path",
            "What is the MusicPlayer main data path?",
            "openURL prepares the source, then data flows demuxer -> audio decoder -> audio "
            "resampler -> audio render -> timeline. Each stage changes the media data at a "
            "different abstraction level.",
            {"docs/Readme_MusicPlayer.md", "docs/architecture/HJGraphMusicPlayer.md"},
        },
        {
            "control path",
            "Why separate data flow from control flow?",
            "Audio frames move downstream continuously, but openURL, pause, resume, seek, repeat, "
            "and close need graph-level serialization and policy. Keeping them separate prevents "
            "a plugin from making business decisions it cannot see globally.",
            {"docs/architecture/HJGraphMusicPlayer_AudioContextGuide.md"},
        },
        {
            "timeline",
            "Why should playback progress be driven by AudioRender?",
            "Demuxer and Decoder only prove that data was read or decoded. AudioRender is closest "
            "to the hardware playback head, so Timeline should advance after render-side PCM "
            "consumption to match what the user hears.",
            {"src/plugins/doc/HJTimeline.md", "src/plugins/doc/HJPluginAudioRender.md"},
        },
        {
            "EOF",
            "What is the difference between demuxer EOF and final playback EOF?",
            "Demuxer EOF means the source has no more compressed packets. Final playback EOF means "
            "all downstream decoded/resampled/render-buffered PCM has been consumed. The latter "
            "must happen later.",
            {"docs/Readme_MusicPlayer.md", "src/plugins/doc/HJPluginDemuxer.md"},
        },
        {
            "queue",
            "Why are frame queues central in media frameworks?",
            "They decouple producer and consumer speed across threads, preserve frame order, expose "
            "buffer statistics, and give Graph or Plugin a place to implement backpressure, flush, "
            "preview, receive, and drop policy.",
            {"src/plugins/doc/HJMediaFrameDeque.md", "studyDemo/day05_bounded_frame_queue.cpp"},
        },
        {
            "smart pointer",
            "Why does HJMedia use Ptr/Wtr aliases heavily?",
            "The object graph has shared ownership and bidirectional references. Ptr expresses "
            "ownership, Wtr expresses observation without increasing reference count, and the "
            "aliases keep usage consistent across classes.",
            {"src/utils/HJObject.h", "studyDemo/day02_smart_ptr_demo.cpp"},
        },
        {
            "RAII",
            "Where does RAII matter in this project?",
            "RAII protects scope-bound cleanup: HJOnceToken-like guards reset busy state, lock "
            "guards release mutexes, and resource wrappers release files, codecs, devices, or "
            "threads even when a function exits early.",
            {"src/utils/HJObject.h", "studyDemo/day02_raii_demo.cpp"},
        },
        {
            "debugging",
            "How would you debug MusicPlayer no sound or stuck EOF?",
            "Walk the chain in order: source opened, packet produced, decoder output PCM, resampler "
            "delivered render format, render consumed PCM, timeline updated, EOF propagated through "
            "every stage, and flush/seek did not leave stale state.",
            {"studyNote/week1-music-player-notes.md", "studyDemo/day06_audio_plugin_chain.cpp"},
        },
    };
}

std::vector<ReviewCheckpoint> buildCheckpoints()
{
    return {
        {
            "Explain HJMedia in two minutes",
            {
                "cross-platform C++ media framework",
                "product lines: pusher, player, render/inference, music player",
                "entry -> graphs -> plugins/core/media/comp layering",
            },
        },
        {
            "Explain MusicPlayer in three minutes",
            {
                "demuxer -> decoder -> resampler -> render -> timeline",
                "control path is graph-level",
                "render-side timeline and final EOF",
            },
        },
        {
            "Explain the C++ basics through project code",
            {
                "shared_ptr owns shared graph/plugin objects",
                "weak_ptr prevents cycles",
                "RAII protects locks, busy state, and resources",
                "queues decouple threads and support backpressure",
            },
        },
    };
}

} // namespace

int main()
{
    hjstudy::printReferences(
        "study/week1-music-player-practice.md Day 7",
        "studyNote/week1-music-player-notes.md Day 7",
        {
            "README.md",
            "docs/Readme_MusicPlayer.md",
            "docs/architecture/HJGraphMusicPlayer.md",
            "src/graphs/HJGraphMusicPlayer.h",
            "src/plugins/doc/README.md",
        });

    printInterviewCards(buildCards());
    printThreeMinuteScript();
    printCheckpoints(buildCheckpoints());
    printWeakSpots();
}
