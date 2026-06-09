/*
 * Day 20: Render and inference insertion point practice.
 *
 * Study plan: study/week3-pusher-codec-rtmp-practice.md
 * Study note: studyNote/week3-pusher-codec-rtmp-notes.md
 *
 * HJMedia reference source:
 * - src/comp/prio
 * - src/comp/rte
 * - src/detect
 * - src/entry/render
 * - src/entry/inference
 */

#include "study_demo_common.h"

namespace {

struct FaceDetectionResult {
    int faces{};
    float confidence{};
};

struct RenderCommand {
    std::string filterName;
    bool enabled{};
};

RenderCommand chooseRenderCommand(const FaceDetectionResult& result)
{
    if (result.faces == 0) {
        return {"privacy-blur", true};
    }
    if (result.confidence > 0.80F) {
        return {"faceu-sticker", true};
    }
    return {"plain-render", true};
}

} // namespace

int main()
{
    hjstudy::printReferences(
        "study/week3-pusher-codec-rtmp-practice.md Day 20",
        "studyNote/week3-pusher-codec-rtmp-notes.md Day 20",
        {
            "src/comp/prio",
            "src/comp/rte",
            "src/detect",
            "src/entry/render",
            "src/entry/inference",
        });

    const std::vector<FaceDetectionResult> detections = {
        {1, 0.94F},
        {0, 0.00F},
        {1, 0.62F},
    };

    for (const auto& detection : detections) {
        const auto command = chooseRenderCommand(detection);
        hjstudy::logFields(
            "render-graph",
            "command",
            {
                {"faces", std::to_string(detection.faces)},
                {"confidence", std::to_string(detection.confidence)},
                {"filter", command.filterName},
                {"enabled", hjstudy::yesNo(command.enabled)},
            });
    }
}
