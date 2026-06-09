/*
 * Day 18: AV interleave and timestamp practice.
 *
 * Study plan: study/week3-pusher-codec-rtmp-practice.md
 * Study note: studyNote/week3-pusher-codec-rtmp-notes.md
 *
 * HJMedia reference source:
 * - docs/README_HJOHMuxer.md
 * - src/media/muxer
 * - src/media/net
 * - third_party/librtmp usage locations
 */

#include "study_demo_common.h"

namespace {

struct Packet {
    int64_t ptsMs{};
    hjstudy::MediaType type{};
    std::string payload;
};

std::vector<Packet> interleave(std::vector<Packet> packets)
{
    std::stable_sort(packets.begin(), packets.end(), [](const Packet& lhs, const Packet& rhs) {
        return lhs.ptsMs < rhs.ptsMs;
    });
    return packets;
}

} // namespace

int main()
{
    hjstudy::printReferences(
        "study/week3-pusher-codec-rtmp-practice.md Day 18",
        "studyNote/week3-pusher-codec-rtmp-notes.md Day 18",
        {
            "docs/README_HJOHMuxer.md",
            "src/media/muxer",
            "src/media/net",
            "third_party/librtmp",
        });

    std::vector<Packet> packets;
    for (int i = 0; i < 5; ++i) {
        packets.push_back({i * 20, hjstudy::MediaType::Audio, "aac"});
    }
    for (int i = 0; i < 3; ++i) {
        packets.push_back({i * 33, hjstudy::MediaType::Video, i == 0 ? "idr" : "p-frame"});
    }

    for (const auto& packet : interleave(std::move(packets))) {
        hjstudy::logFields(
            "mux",
            "write",
            {{"type", hjstudy::toString(packet.type)}, {"ptsMs", std::to_string(packet.ptsMs)}, {"payload", packet.payload}});
    }
}
