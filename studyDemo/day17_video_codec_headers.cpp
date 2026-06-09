/*
 * Day 17: Video codec header practice.
 *
 * Study plan: study/week3-pusher-codec-rtmp-practice.md
 * Study note: studyNote/week3-pusher-codec-rtmp-notes.md
 *
 * HJMedia reference source:
 * - src/media video capture/codec related implementations
 * - src/plugins/hsys
 * - src/plugins/asys
 * - src/plugins/isys
 */

#include "study_demo_common.h"

namespace {

struct EncodedVideoPacket {
    int64_t ptsMs{};
    bool keyFrame{};
    bool containsCodecHeader{};
    std::string naluSummary;
};

class CodecHeaderInjector {
public:
    void updateHeader(std::string header)
    {
        codecHeader_ = std::move(header);
    }

    EncodedVideoPacket normalize(EncodedVideoPacket packet) const
    {
        if (packet.keyFrame && !packet.containsCodecHeader) {
            packet.containsCodecHeader = true;
            packet.naluSummary = codecHeader_ + " + " + packet.naluSummary;
        }
        return packet;
    }

private:
    std::string codecHeader_{"SPS/PPS"};
};

} // namespace

int main()
{
    hjstudy::printReferences(
        "study/week3-pusher-codec-rtmp-practice.md Day 17",
        "studyNote/week3-pusher-codec-rtmp-notes.md Day 17",
        {
            "src/media",
            "src/plugins/hsys",
            "src/plugins/asys",
            "src/plugins/isys",
        });

    CodecHeaderInjector injector;
    const std::vector<EncodedVideoPacket> packets = {
        {0, true, false, "IDR"},
        {33, false, false, "P-frame"},
        {66, false, false, "P-frame"},
        {99, true, true, "SPS/PPS + IDR"},
    };

    for (auto packet : packets) {
        packet = injector.normalize(packet);
        hjstudy::logFields(
            "video-packet",
            "normalize",
            {
                {"ptsMs", std::to_string(packet.ptsMs)},
                {"keyFrame", hjstudy::yesNo(packet.keyFrame)},
                {"hasHeader", hjstudy::yesNo(packet.containsCodecHeader)},
                {"nalu", packet.naluSummary},
            });
    }
}
