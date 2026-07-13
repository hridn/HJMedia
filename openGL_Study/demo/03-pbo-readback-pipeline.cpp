/*
 * Day 31: PBO 双缓冲回读模拟
 *
 * 这个 demo 对应 HJPBORead / HJPBOReadWrapper / HJRteComDrawPBOFBO。
 * 学习重点：
 * 1. 当前帧 glReadPixels 写当前 PBO。
 * 2. CPU map 的是上一帧 PBO，因此第一帧没有可读数据，会返回 WOULD_BLOCK。
 * 3. PBO 只是降低同步概率，不代表完全没有 GPU/CPU 等待。
 */

#include "study_demo_common.h"

namespace {

enum class ReadResult {
    Ok,
    WouldBlock,
};

std::string toString(ReadResult result)
{
    return result == ReadResult::Ok ? "HJ_OK" : "HJ_WOULD_BLOCK";
}

struct PboSlot {
    int id{};
    int frameNo{-1};
    bool hasGpuData{false};
};

class PboReader {
public:
    PboReader(int width, int height)
        : width_(width)
        , height_(height)
    {
        slots_[0].id = 1;
        slots_[1].id = 2;
        hjstudy::logFields("HJPBORead::init", "glGenBuffers(2) + glBufferData(GL_PIXEL_PACK_BUFFER)", {
            {"size", std::to_string(width_) + "x" + std::to_string(height_)},
            {"bytes", std::to_string(width_ * height_ * 4)},
        });
    }

    ReadResult readAfterFboDraw(int frameNo)
    {
        currentIndex_ = (currentIndex_ + 1) % 2;
        const int previousIndex = (currentIndex_ + 1) % 2;

        PboSlot& writeSlot = slots_[currentIndex_];
        hjstudy::logFields("HJPBORead::read", "glBindBuffer + glReadPixels writes current PBO", {
            {"frame", std::to_string(frameNo)},
            {"writePbo", std::to_string(writeSlot.id)},
        });
        writeSlot.frameNo = frameNo;
        writeSlot.hasGpuData = true;

        PboSlot& readSlot = slots_[previousIndex];
        if (!readSlot.hasGpuData) {
            hjstudy::logFields("HJPBORead::read", "first frame has no previous PBO to map", {
                {"result", toString(ReadResult::WouldBlock)},
            });
            return ReadResult::WouldBlock;
        }

        hjstudy::logFields("HJPBORead::read", "glMapBufferRange reads previous PBO, then glUnmapBuffer", {
            {"readPbo", std::to_string(readSlot.id)},
            {"mappedFrame", std::to_string(readSlot.frameNo)},
            {"result", toString(ReadResult::Ok)},
        });
        return ReadResult::Ok;
    }

private:
    int width_{};
    int height_{};
    int currentIndex_{1};
    PboSlot slots_[2];
};

class PboFboComponent {
public:
    PboFboComponent(int width, int height)
        : reader_(width, height)
    {
    }

    void renderAndRead(int frameNo)
    {
        hjstudy::logFields("HJRteComDrawPBOFBO::bind", "attach FBO before draw", {
            {"frame", std::to_string(frameNo)},
        });
        hjstudy::logFields("HJRteComDrawPBOFBO::render", "shader draws texture into FBO", {
            {"frame", std::to_string(frameNo)},
        });
        hjstudy::logFields("HJRteComDrawPBOFBO::unbind", "detach FBO and call priReadPBO", {
            {"frame", std::to_string(frameNo)},
        });
        const ReadResult result = reader_.readAfterFboDraw(frameNo);
        hjstudy::logFields("HJRteComDrawPBOFBO::priReadPBO", "read callback status", {
            {"frame", std::to_string(frameNo)},
            {"result", toString(result)},
        });
    }

private:
    PboReader reader_;
};

} // namespace

int main()
{
    hjstudy::printTitle("Day 31 - PBO 双缓冲回读");
    hjstudy::printReferences(
        "openGL_Study/01-opengl-study-plan.md",
        "openGL_Study/02-opengl-study-notes.md",
        {
            "src/comp/graphic/HJPBORead.cpp",
            "src/comp/graphic/HJPBOReadWrapper.cpp",
            "src/comp/rte/HJRteComDraw.cpp",
        });

    PboFboComponent pboTarget(640, 360);
    for (int frame = 0; frame < 5; ++frame) {
        pboTarget.renderAndRead(frame);
    }

    hjstudy::logLine("面试复述", "双 PBO 让当前帧发起 readPixels，CPU 读取上一帧数据，所以第一帧会 WOULD_BLOCK。");
    return 0;
}
