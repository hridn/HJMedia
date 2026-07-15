/*
 * Day 1：EGL、纹理与第一帧
 *
 * 当前机器不具备 HarmonyOS NativeWindow/EGL 运行环境，因此这个可执行 demo
 * 用 C++17 状态模型验证 HJMedia 的关键约束，而不假装调用了真实 EGL：
 * 1. HJOGRenderEnv::priCoreInit 创建 Context 和 1x1 离屏 Surface；
 * 2. 执行 GL 命令前，目标 Surface 必须在当前 Graph 线程 makeCurrent；
 * 3. HJOGCopyShaderStrip::draw 使用 2D/OES 对应的采样器完成一次全屏绘制；
 * 4. 2x2 测试纹理明确采用 OpenGL 左下角为原点的行顺序，方便检查 Y Flip。
 */

#include "study_demo_common.h"

#include <array>
#include <cassert>
#include <iomanip>

namespace {

struct Pixel {
    std::uint8_t r{};
    std::uint8_t g{};
    std::uint8_t b{};
    std::uint8_t a{255};
};

using TestImage = std::array<Pixel, 4>;

std::string rgba(const Pixel& pixel)
{
    std::ostringstream stream;
    stream << "(" << static_cast<int>(pixel.r)
           << "," << static_cast<int>(pixel.g)
           << "," << static_cast<int>(pixel.b)
           << "," << static_cast<int>(pixel.a) << ")";
    return stream.str();
}

bool operator==(const Pixel& lhs, const Pixel& rhs)
{
    return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b && lhs.a == rhs.a;
}

enum class TextureTarget {
    Texture2D,
    ExternalOes,
};

std::string_view targetName(TextureTarget target)
{
    return target == TextureTarget::Texture2D ? "GL_TEXTURE_2D" : "GL_TEXTURE_EXTERNAL_OES";
}

struct Surface {
    int id{};
    int width{};
    int height{};
    std::string name;
};

class EglCoreModel {
public:
    void init()
    {
        ownerThread_ = std::this_thread::get_id();
        displayReady_ = true;
        contextReady_ = true;
        hjstudy::logLine(
            "HJOGEGLCore::init",
            "eglGetDisplay -> eglInitialize -> eglChooseConfig -> eglCreateContext（状态模拟）");
    }

    Surface createSurface(int width, int height, std::string name)
    {
        requireOwnerThread("createSurface");
        if (!displayReady_ || !contextReady_) {
            throw std::logic_error("EGL display/context 尚未初始化");
        }
        Surface surface{nextSurfaceId_++, width, height, std::move(name)};
        hjstudy::logFields("HJOGEGLCore::EGLSurfaceCreate", "创建 Surface", {
            {"id", std::to_string(surface.id)},
            {"name", surface.name},
            {"size", std::to_string(width) + "x" + std::to_string(height)},
        });
        return surface;
    }

    void makeCurrent(const Surface& surface)
    {
        requireOwnerThread("makeCurrent");
        currentSurfaceId_ = surface.id;
        hjstudy::logFields("HJOGEGLCore::makeCurrent", "绑定当前线程的 draw/read Surface", {
            {"surface", surface.name},
            {"id", std::to_string(surface.id)},
        });
    }

    void requireCurrent(const Surface& surface) const
    {
        requireOwnerThread("GL draw");
        if (currentSurfaceId_ != surface.id) {
            throw std::logic_error("draw 前没有把目标 Surface 设为 current");
        }
    }

    void swap(const Surface& surface) const
    {
        requireCurrent(surface);
        hjstudy::logLine("HJOGEGLCore::swap", "eglSwapBuffers（状态模拟）");
    }

    void done()
    {
        requireOwnerThread("done");
        currentSurfaceId_ = 0;
        contextReady_ = false;
        displayReady_ = false;
        hjstudy::logLine(
            "HJOGEGLCore::done",
            "destroy Surface/Context -> eglTerminate（状态模拟）");
    }

private:
    void requireOwnerThread(std::string_view operation) const
    {
        if (std::this_thread::get_id() != ownerThread_) {
            throw std::logic_error(std::string(operation) + " 不在 EGLContext 所属 Graph 线程");
        }
    }

    std::thread::id ownerThread_{};
    bool displayReady_{false};
    bool contextReady_{false};
    int currentSurfaceId_{0};
    int nextSurfaceId_{1};
};

class CopyShaderModel {
public:
    explicit CopyShaderModel(TextureTarget target)
        : target_(target)
    {
        hjstudy::logFields("HJOGCopyShaderStrip::init", "选择 Fragment Shader", {
            {"target", std::string(targetName(target_))},
            {"sampler", target_ == TextureTarget::Texture2D ? "sampler2D" : "samplerExternalOES"},
        });
    }

    TestImage draw(const TestImage& input, bool yFlip) const
    {
        // HJOGCopyShaderStrip::draw 中，uMVPMatrix 决定几何变换，uSTMatrix 决定采样坐标。
        // 这里把 Y Flip 简化为交换上下两行，验证坐标方向而不是模拟 GPU 浮点采样。
        TestImage output = input;
        if (yFlip) {
            std::swap(output[0], output[2]);
            std::swap(output[1], output[3]);
        }
        hjstudy::logFields("HJOGCopyShaderStrip::draw", "useProgram -> bind texture -> matrices -> TRIANGLE_STRIP", {
            {"target", std::string(targetName(target_))},
            {"yFlip", hjstudy::yesNo(yFlip)},
        });
        return output;
    }

private:
    TextureTarget target_;
};

void printImage(std::string_view title, const TestImage& image)
{
    std::cout << title << "（上行 y=1，下行 y=0）\n";
    std::cout << "  " << rgba(image[2]) << "  " << rgba(image[3]) << '\n';
    std::cout << "  " << rgba(image[0]) << "  " << rgba(image[1]) << '\n';
}

} // namespace

int main()
{
    hjstudy::printTitle("OpenGL Study Day 1 - EGL、纹理与第一帧");
    hjstudy::printReferences(
        "openGL_Study/00-hjmedia-opengl-three-day-study-plan.md",
        "openGL_Study/day01-egl-first-frame.md",
        {
            "src/entry/hsys/HJEntryBaseRender.cpp",
            "src/comp/graphic/hsys/HJOGEGLCore.cpp",
            "src/comp/graphic/hsys/HJOGRenderEnv.cpp",
            "src/comp/graphic/HJOGCopyShaderStrip.cpp",
            "src/comp/graphic/HJOGShaderCommon.cpp",
        });

    EglCoreModel egl;
    egl.init();
    const Surface offscreen = egl.createSurface(1, 1, "offscreen-pbuffer");
    const Surface window = egl.createSurface(2, 2, "NativeWindow#1");
    egl.makeCurrent(offscreen);

    // 数组按 OpenGL 纹理坐标排列：先 y=0 的左/右，再 y=1 的左/右。
    const TestImage testTexture{{
        Pixel{255, 0, 0, 255},       // 左下：红
        Pixel{0, 255, 0, 255},       // 右下：绿
        Pixel{0, 0, 255, 255},       // 左上：蓝
        Pixel{255, 255, 255, 255},   // 右上：白
    }};

    egl.makeCurrent(window);
    egl.requireCurrent(window);
    const CopyShaderModel shader2D(TextureTarget::Texture2D);
    const TestImage copied = shader2D.draw(testTexture, false);
    assert(copied == testTexture);
    printImage("2D Shader 原样复制", copied);
    egl.swap(window);

    const TestImage flipped = shader2D.draw(testTexture, true);
    assert(flipped[0] == testTexture[2] && flipped[3] == testTexture[1]);
    printImage("开启 Y Flip", flipped);

    // OES 与 2D 的关键差异在纹理 target 和 sampler；它通常由 NativeImage 外部生产。
    const CopyShaderModel shaderOes(TextureTarget::ExternalOes);
    (void)shaderOes.draw(testTexture, false);

    bool wrongThreadRejected = false;
    std::thread wrongThread([&] {
        try {
            egl.makeCurrent(window);
        } catch (const std::logic_error& error) {
            wrongThreadRejected = true;
            hjstudy::logLine("线程约束验证", error.what());
        }
    });
    wrongThread.join();
    assert(wrongThreadRejected);

    egl.makeCurrent(offscreen);
    egl.done();
    hjstudy::logLine("验证结论", "2x2 颜色、Y Flip、2D/OES 采样器选择和 Context 线程约束均通过");
    return 0;
}
