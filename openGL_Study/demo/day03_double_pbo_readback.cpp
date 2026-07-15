/*
 * Day 3：双 PBO 回读状态机
 *
 * 这个 C++17 demo 精确复刻 HJPBORead::read 的索引语义：
 * - m_index 初始为 0，每帧先 (index + 1) % 2；
 * - glReadPixels 写当前 PBO，CPU map 另一个（上一帧）PBO；
 * - 第一帧只有提交、没有上一帧可读，因此返回 HJ_WOULD_BLOCK；
 * - 分辨率变化时 HJPBOReadWrapper 会重建 reader，流水线重新预热一帧。
 */

#include "study_demo_common.h"

#include <array>
#include <cassert>
#include <memory>

namespace {

enum class ReadResult {
    Ok,
    WouldBlock,
};

std::string_view resultName(ReadResult result)
{
    return result == ReadResult::Ok ? "HJ_OK" : "HJ_WOULD_BLOCK";
}

struct PboSlot {
    int id{};
    int frameNo{-1};
    bool valid{false};
};

struct ReadEvent {
    int submittedFrame{};
    int writePbo{};
    int mappedPbo{-1};
    int callbackFrame{-1};
    ReadResult result{ReadResult::WouldBlock};
};

class DoublePboReaderModel {
public:
    using Callback = std::function<void(int)>;

    DoublePboReaderModel(int width, int height, Callback callback)
        : width_(width)
        , height_(height)
        , callback_(std::move(callback))
    {
        slots_[0].id = 0;
        slots_[1].id = 1;
        hjstudy::logFields("HJPBORead::init", "glGenBuffers(2) + GL_PIXEL_PACK_BUFFER", {
            {"size", std::to_string(width_) + "x" + std::to_string(height_)},
            {"bytes", std::to_string(width_ * height_ * 4)},
        });
    }

    ReadEvent read(int frameNo)
    {
        currentIndex_ = (currentIndex_ + 1) % 2;
        const int previousIndex = (currentIndex_ + 1) % 2;

        PboSlot& writeSlot = slots_[currentIndex_];
        writeSlot.frameNo = frameNo;
        writeSlot.valid = true;

        ReadEvent event;
        event.submittedFrame = frameNo;
        event.writePbo = writeSlot.id;

        // 对应 glReadPixels(..., offset=0)：数据进入当前绑定的 Pixel Pack Buffer。
        hjstudy::logFields("HJPBORead::read", "提交 glReadPixels", {
            {"frame", std::to_string(frameNo)},
            {"writePbo", std::to_string(writeSlot.id)},
        });

        if (!hasPreviousFrame_) {
            hasPreviousFrame_ = true;
            event.result = ReadResult::WouldBlock;
            hjstudy::logLine("HJPBORead::read", "首帧没有 N-1 数据，返回 HJ_WOULD_BLOCK");
            return event;
        }

        PboSlot& readSlot = slots_[previousIndex];
        assert(readSlot.valid);
        event.mappedPbo = readSlot.id;
        event.callbackFrame = readSlot.frameNo;
        event.result = ReadResult::Ok;

        // 对应 glMapBufferRange + 回调 + glUnmapBuffer；映射可能仍等待 GPU，需单独计时。
        hjstudy::logFields("HJPBORead::read", "map 上一帧 PBO 并回调", {
            {"mappedPbo", std::to_string(readSlot.id)},
            {"callbackFrame", std::to_string(readSlot.frameNo)},
        });
        if (callback_) {
            callback_(readSlot.frameNo);
        }
        return event;
    }

private:
    int width_{};
    int height_{};
    int currentIndex_{0};
    bool hasPreviousFrame_{false};
    std::array<PboSlot, 2> slots_{};
    Callback callback_;
};

class ReadbackWrapperModel {
public:
    ReadEvent process(int frameNo, int width, int height)
    {
        if (!reader_ || width != width_ || height != height_) {
            width_ = width;
            height_ = height;
            reader_ = std::make_unique<DoublePboReaderModel>(width_, height_, [this](int completedFrame) {
                callbacks_.push_back(completedFrame);
                hjstudy::logLine(
                    "HJPBOReadWrapper callback",
                    "复制并 Y Flip RGBA，再交给 HJBaseGPUToRAM::setMediaData");
            });
        }
        return reader_->read(frameNo);
    }

    const std::vector<int>& callbacks() const
    {
        return callbacks_;
    }

private:
    int width_{};
    int height_{};
    std::unique_ptr<DoublePboReaderModel> reader_;
    std::vector<int> callbacks_;
};

void printEvent(const ReadEvent& event)
{
    std::cout << "frame=" << event.submittedFrame
              << " write=PBO" << event.writePbo
              << " map=" << (event.mappedPbo < 0 ? "-" : "PBO" + std::to_string(event.mappedPbo))
              << " callbackFrame=" << event.callbackFrame
              << " result=" << resultName(event.result) << '\n';
}

} // namespace

int main()
{
    hjstudy::printTitle("OpenGL Study Day 3 - 双 PBO 回读");
    hjstudy::printReferences(
        "openGL_Study/00-hjmedia-opengl-three-day-study-plan.md",
        "openGL_Study/day03-pbo-graph-debug.md",
        {
            "src/comp/graphic/HJPBORead.cpp",
            "src/comp/graphic/HJPBOReadWrapper.cpp",
            "src/comp/rte/HJRteComDraw.cpp",
            "src/comp/rte/HJRteGraphProc.cpp",
            "src/entry/hsys/HJEntryBaseRender.cpp",
        });

    ReadbackWrapperModel wrapper;
    std::vector<ReadEvent> events;
    for (int frame = 0; frame < 4; ++frame) {
        events.push_back(wrapper.process(frame, 640, 360));
        printEvent(events.back());
    }

    assert(events[0].result == ReadResult::WouldBlock);
    assert(events[0].writePbo == 1);
    assert(events[1].writePbo == 0 && events[1].mappedPbo == 1 && events[1].callbackFrame == 0);
    assert(events[2].writePbo == 1 && events[2].mappedPbo == 0 && events[2].callbackFrame == 1);
    assert(events[3].writePbo == 0 && events[3].mappedPbo == 1 && events[3].callbackFrame == 2);
    assert((wrapper.callbacks() == std::vector<int>{0, 1, 2}));

    // HJPBOReadWrapper::process 在尺寸变化时创建新 reader，所以 1080p 首帧再次 WOULD_BLOCK。
    const ReadEvent resized = wrapper.process(4, 1920, 1080);
    printEvent(resized);
    assert(resized.result == ReadResult::WouldBlock);

    hjstudy::logLine("建议指标", "记录整帧、各 FBO Pass、glReadPixels 提交、PBO map 等待、回调复制耗时");
    hjstudy::logLine("验证结论", "N 写当前 PBO、N-1 映射另一 PBO、首帧/重建后 WOULD_BLOCK 均通过");
    return 0;
}
