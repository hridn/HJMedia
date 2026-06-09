/*
 * Day 10: Plugin lifecycle and logging practice.
 *
 * Study plan: study/week2-thread-plugin-player-practice.md
 * Study note: studyNote/week2-thread-plugin-player-notes.md
 *
 * HJMedia reference source:
 * - src/plugins/doc/HJPlugin.md
 * - src/plugins/doc/HJPluginCodec.md
 * - src/plugins/doc/HJMediaFrameDeque.md
 */

#include "study_demo_common.h"

namespace {

enum class PluginStatus {
    Created,
    Inited,
    Running,
    Paused,
    Flushed,
    Done,
};

std::string toString(PluginStatus status)
{
    switch (status) {
    case PluginStatus::Created:
        return "Created";
    case PluginStatus::Inited:
        return "Inited";
    case PluginStatus::Running:
        return "Running";
    case PluginStatus::Paused:
        return "Paused";
    case PluginStatus::Flushed:
        return "Flushed";
    case PluginStatus::Done:
        return "Done";
    }
    return "Unknown";
}

class PluginProbe {
public:
    explicit PluginProbe(std::string name)
        : name_(std::move(name))
    {
        log("create");
    }

    void init() { transition(PluginStatus::Inited, "init"); }
    void runTask(int64_t ptsMs) { transition(PluginStatus::Running, "runTask", ptsMs); }
    void pause() { transition(PluginStatus::Paused, "pause"); }
    void flush() { transition(PluginStatus::Flushed, "flush"); }
    void done() { transition(PluginStatus::Done, "done"); }

private:
    void transition(PluginStatus status, std::string_view action, int64_t ptsMs = -1)
    {
        status_ = status;
        log(action, ptsMs);
    }

    void log(std::string_view action, int64_t ptsMs = -1) const
    {
        std::map<std::string, std::string> fields{
            {"status", toString(status_)},
            {"queueSize", std::to_string(queueSize_)},
        };
        if (ptsMs >= 0) {
            fields["ptsMs"] = std::to_string(ptsMs);
        }
        hjstudy::logFields(name_, action, fields);
    }

    std::string name_;
    PluginStatus status_{PluginStatus::Created};
    std::size_t queueSize_{0};
};

} // namespace

int main()
{
    hjstudy::printReferences(
        "study/week2-thread-plugin-player-practice.md Day 10",
        "studyNote/week2-thread-plugin-player-notes.md Day 10",
        {
            "src/plugins/doc/HJPlugin.md",
            "src/plugins/doc/HJPluginCodec.md",
            "src/plugins/doc/HJMediaFrameDeque.md",
        });

    PluginProbe decoder("audio-decoder");
    decoder.init();
    decoder.runTask(40);
    decoder.pause();
    decoder.flush();
    decoder.done();
}
