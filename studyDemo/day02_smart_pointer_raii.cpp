/*
 * Day 02: Smart pointer and RAII practice.
 *
 * Study plan: study/week1-music-player-practice.md
 * Study note: studyNote/week1-music-player-notes.md
 *
 * HJMedia reference source:
 * - src/utils/HJObject.h
 * - usages of HJ_DECLARE_PUWTR / Ptr / Wtr / sharedFrom
 */

#include "study_demo_common.h"

#include <memory>

namespace {

class AudioDeviceSession {
public:
    explicit AudioDeviceSession(std::string deviceName)
        : deviceName_(std::move(deviceName))
    {
        hjstudy::logLine("AudioDeviceSession", "acquire " + deviceName_);
    }

    AudioDeviceSession(const AudioDeviceSession&) = delete;
    AudioDeviceSession& operator=(const AudioDeviceSession&) = delete;

    ~AudioDeviceSession()
    {
        hjstudy::logLine("AudioDeviceSession", "release " + deviceName_);
    }

private:
    std::string deviceName_;
};

class PluginNode : public std::enable_shared_from_this<PluginNode> {
public:
    using Ptr = std::shared_ptr<PluginNode>;
    using Wtr = std::weak_ptr<PluginNode>;

    explicit PluginNode(std::string name)
        : name_(std::move(name))
    {
    }

    void connectTo(const Ptr& next)
    {
        next_ = next;
    }

    void setPrevious(const Ptr& previous)
    {
        previous_ = previous;
    }

    void printLinks() const
    {
        std::cout << name_ << " -> next="
                  << (next_.lock() ? next_.lock()->name_ : "<expired>")
                  << ", previous="
                  << (previous_.lock() ? previous_.lock()->name_ : "<expired>")
                  << '\n';
    }

private:
    std::string name_;
    Wtr next_;
    Wtr previous_;
};

} // namespace

int main()
{
    hjstudy::printReferences(
        "study/week1-music-player-practice.md Day 2",
        "studyNote/week1-music-player-notes.md Day 2",
        {
            "src/utils/HJObject.h",
            "src/graphs/HJGraphMusicPlayer.h",
            "src/plugins/doc/HJPlugin.md",
        });

    hjstudy::printTitle("RAII resource lifetime");
    {
        AudioDeviceSession session("default-audio-render-device");
        hjstudy::ScopeExit cleanup([] {
            hjstudy::logLine("ScopeExit", "flush temporary playback state");
        });
        hjstudy::logLine("main", "playback scope is active");
    }

    hjstudy::printTitle("weak_ptr link model");
    auto demuxer = std::make_shared<PluginNode>("demuxer");
    auto decoder = std::make_shared<PluginNode>("decoder");
    demuxer->connectTo(decoder);
    decoder->setPrevious(demuxer);

    demuxer->printLinks();
    decoder->printLinks();
    std::cout << "demuxer.use_count=" << demuxer.use_count()
              << ", decoder.use_count=" << decoder.use_count() << '\n';
}
