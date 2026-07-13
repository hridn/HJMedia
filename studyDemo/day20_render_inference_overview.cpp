/*
 * Day 20: Render / Faceu / AI insertion point practice.
 *
 * 本 demo 不调用真实 OpenGL 或模型，只把 HJMedia 的源码语义压缩成一个
 * 可运行的小流程：
 * 1. HJRenderGraphWrapper::render 把外部 NV12 帧转成 HJTransferMediaData 并入队；
 * 2. RTE 图中的 TargetPBODetect 分支把 GPU 纹理读回给检测入口；
 * 3. HJFaceDetectWrapper::detect 产出精简 faceInfo；
 * 4. HJRenderGraphWrapper::setFaceInfo 把 faceInfo 注入 HJRteGraphProc；
 * 5. SourceFaceu / Blur / SR / Denoise 等节点按 faceInfo 和业务开关参与渲染。
 *
 * 对照源码：
 * - src/entry/render/HJRenderGraphExport.cpp
 * - src/entry/inference/HJFaceDetectExport.cpp
 * - src/comp/rte/HJRteGraphProcConfigSetup.cpp
 * - src/comp/rte/HJRteGraphProc.cpp
 * - src/comp/rte/HJRteGraphSetupInfo.cpp
 * - src/detect/utils/HJBaseFaceDetect.h
 */

#include "study_demo_common.h"

#include <array>

namespace {

struct VideoFrame {
    int id{};
    int64_t ptsMs{};
    int width{1280};
    int height{720};
};

struct FaceDetectionResult {
    int frameId{};
    int faces{};
    float confidence{};
    bool debugPoints{};
};

struct RenderDecision {
    bool enableFaceu{};
    bool enablePrivacyBlur{};
    bool enableDenoise{};
    bool enableSR{};
    std::string faceInfo;
};

// 用字符串模拟 HJBaseFaceDetect::cvtConcisePoints 输出。
// 真实源码会把检测框和关键点序列化为 HJFacePointsInfo，随后由
// HJRteGraphProc::setFaceInfo 反序列化、平滑，再按 sourceInsName 缓存。
std::string makeConciseFaceInfo(const FaceDetectionResult& result)
{
    if (result.faces <= 0) {
        return "";
    }
    return "faceCount=" + std::to_string(result.faces)
        + ";confidence=" + std::to_string(result.confidence)
        + ";points=leftEye,rightEye,nose,mouthLeft,mouthRight";
}

RenderDecision chooseRenderDecision(const FaceDetectionResult& result)
{
    RenderDecision decision;
    decision.faceInfo = makeConciseFaceInfo(result);
    decision.enableDenoise = true;

    // RTE 中 FilterSR / FilterDenoise 是 filter node；它们和 Faceu 不冲突，
    // 但一般会按性能预算动态打开。这里用置信度模拟“画面质量足够时再开 SR”。
    decision.enableSR = result.faces > 0 && result.confidence >= 0.90F;

    // 没检测到人脸时打开隐私 Blur；检测到稳定人脸时打开 SourceFaceu 贴纸源。
    decision.enablePrivacyBlur = result.faces == 0;
    decision.enableFaceu = result.faces > 0 && result.confidence >= 0.75F;
    return decision;
}

class RenderGraphSimulator {
public:
    explicit RenderGraphSimulator(std::size_t latencyFrames)
        : latencyFrames_(latencyFrames)
    {
    }

    void init()
    {
        hjstudy::printTitle("RTE graph nodes");
        const std::array<const char*, 8> nodes = {
            "HJNodeClass_SourceBridgeMediaData",
            "HJNodeClass_FilterCopy2D",
            "HJNodeClass_TargetPBODetect",
            "HJNodeClass_FilterDenoise",
            "HJNodeClass_FilterSR",
            "HJNodeClass_FilterBlur",
            "HJNodeClass_SourceFaceu(dependsOn=SourceBridgeMediaData)",
            "HJNodeClass_TargetUI_0 / HJNodeClass_TargetEncoder",
        };
        for (const auto* node : nodes) {
            hjstudy::logLine("HJRteGraphProcConfigSetup", node);
        }
    }

    void render(VideoFrame frame)
    {
        // 对应 HJRenderGraphWrapper::render 中的 m_inputQueue 和 nLatencyCnt。
        // 上游输入太快时，render 入口会丢掉旧输入，避免 GPU 渲染链路积压历史帧。
        while (inputQueue_.size() > latencyFrames_) {
            const auto dropped = inputQueue_.front();
            inputQueue_.pop_front();
            hjstudy::logFields(
                "HJRenderGraphWrapper",
                "drop-old-input",
                {
                    {"frame", std::to_string(dropped.id)},
                    {"reason", "inputQueue > nLatencyCnt"},
                });
        }

        inputQueue_.push_back(frame);
        hjstudy::logFields(
            "HJRenderGraphWrapper",
            "render-enqueue",
            {
                {"frame", std::to_string(frame.id)},
                {"ptsMs", std::to_string(frame.ptsMs)},
                {"queue", std::to_string(inputQueue_.size())},
            });
    }

    std::optional<VideoFrame> acquireForDetect()
    {
        // 对应 TargetPBODetect / HJRteComDrawPBOFBODetect：从 GPU 链路旁路读一份
        // 给 AI 检测，主渲染链仍继续往 UI/Encoder target 走。
        if (inputQueue_.empty()) {
            return std::nullopt;
        }
        return inputQueue_.back();
    }

    void setFaceInfo(const std::string& sourceInsName, const RenderDecision& decision)
    {
        // 对应 HJRenderGraphWrapper::setFaceInfo -> HJRteGraphProc::setFaceInfo。
        // RTE 用 sourceInsName 区分多源场景，SourceFaceu 节点通过 dependsOn 找到这份人脸信息。
        faceInfoBySource_[sourceInsName] = decision.faceInfo;
        hjstudy::logFields(
            "HJRteGraphProc",
            "setFaceInfo",
            {
                {"source", sourceInsName},
                {"hasFaceInfo", hjstudy::yesNo(!decision.faceInfo.empty())},
                {"faceu", hjstudy::yesNo(decision.enableFaceu)},
                {"privacyBlur", hjstudy::yesNo(decision.enablePrivacyBlur)},
                {"denoise", hjstudy::yesNo(decision.enableDenoise)},
                {"sr", hjstudy::yesNo(decision.enableSR)},
            });
    }

    void drawLatest(const std::string& sourceInsName)
    {
        if (inputQueue_.empty()) {
            hjstudy::logLine("HJRteGraph", "no frame to draw");
            return;
        }

        const auto frame = inputQueue_.front();
        inputQueue_.pop_front();
        const auto faceInfo = faceInfoBySource_[sourceInsName];
        hjstudy::logFields(
            "HJRteGraph::renderFromBottomToTop",
            "draw",
            {
                {"frame", std::to_string(frame.id)},
                {"base", "SourceBridgeMediaData -> Copy2D"},
                {"faceInfo", faceInfo.empty() ? "empty" : "ready"},
                {"target", "UI/Encoder"},
            });
    }

private:
    std::size_t latencyFrames_{};
    std::deque<VideoFrame> inputQueue_;
    std::map<std::string, std::string> faceInfoBySource_;
};

class FaceDetectSimulator {
public:
    FaceDetectionResult detect(const VideoFrame& frame) const
    {
        // 对应 HJFaceDetectWrapper::detect。真实实现可同步或异步：
        // 异步路径使用 HJThreadPool::asyncClear，只保留最新检测任务，避免 AI 延迟堆积。
        switch (frame.id) {
        case 0:
            return {frame.id, 1, 0.96F, false};
        case 1:
            return {frame.id, 0, 0.00F, false};
        case 2:
            return {frame.id, 1, 0.64F, true};
        case 3:
            return {frame.id, 0, 0.00F, false};
        default:
            return {frame.id, 1, 0.88F, false};
        }
    }
};

} // namespace

int main()
{
    hjstudy::printReferences(
        ".agents/skills/hjmedia-daily-study/references/28-day-plan.md Day 20",
        "studyNote/20-render-inference-overview.md",
        {
            "src/comp/prio",
            "src/comp/rte",
            "src/detect",
            "src/entry/render",
            "src/entry/inference",
        });

    RenderGraphSimulator renderGraph(/*latencyFrames=*/2);
    FaceDetectSimulator detector;
    renderGraph.init();

    hjstudy::printTitle("Frame loop");
    const std::string mainSource = "HJNodeClass_SourceBridgeMediaData";
    const std::vector<VideoFrame> frames = {
        {0, 0},
        {1, 33},
        {2, 66},
        {3, 99},
        {4, 132},
    };

    for (const auto& frame : frames) {
        renderGraph.render(frame);

        const auto detectFrame = renderGraph.acquireForDetect();
        if (!detectFrame) {
            continue;
        }

        const auto result = detector.detect(*detectFrame);
        const auto decision = chooseRenderDecision(result);
        hjstudy::logFields(
            "HJFaceDetectWrapper",
            "detect-callback",
            {
                {"frame", std::to_string(result.frameId)},
                {"faces", std::to_string(result.faces)},
                {"confidence", std::to_string(result.confidence)},
                {"debugPoints", hjstudy::yesNo(result.debugPoints)},
            });

        renderGraph.setFaceInfo(mainSource, decision);
        renderGraph.drawLatest(mainSource);
    }

    hjstudy::printTitle("Latency guard burst");
    // 模拟上游短时间连续输入，但 render 线程来不及消费。
    // HJRenderGraphWrapper::render 会按 nLatencyCnt 丢弃旧输入，保护实时渲染延迟。
    const std::vector<VideoFrame> burstFrames = {
        {5, 165},
        {6, 198},
        {7, 231},
        {8, 264},
        {9, 297},
    };
    for (const auto& frame : burstFrames) {
        renderGraph.render(frame);
    }

    hjstudy::printTitle("Interview takeaway");
    hjstudy::logLine(
        "summary",
        "AI 检测不是替换渲染链路，而是通过 PBO/回调产生 faceInfo；RTE/Prio 再把 faceInfo 作为控制数据驱动 Faceu、Blur、SR、Denoise 等 GPU 节点。");
}
