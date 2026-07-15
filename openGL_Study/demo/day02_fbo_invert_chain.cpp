/*
 * Day 2：FBO 与多 Pass 后处理
 *
 * 这是可运行的 C++17 CPU 状态模型，用来验证 HJMedia FBO 组件的语义：
 * - HJOGFBOCtrl::attach 保存旧 framebuffer，绑定自己的 color attachment；
 * - HJPrioComFBOBase::check 在宽高变化时重建 FBO；
 * - 每个效果只读取输入纹理并写入另一张 FBO 纹理，禁止同纹理边读边写；
 * - 反相效果保留 alpha，仅将 RGB 变为 1.0 - color.rgb。
 */

#include "study_demo_common.h"

#include <array>
#include <cassert>

namespace {

struct Pixel {
    std::uint8_t r{};
    std::uint8_t g{};
    std::uint8_t b{};
    std::uint8_t a{255};
};

using Samples = std::array<Pixel, 4>;

struct Texture {
    int id{};
    int width{};
    int height{};
    Samples samples{};
};

bool operator==(const Pixel& lhs, const Pixel& rhs)
{
    return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b && lhs.a == rhs.a;
}

std::string rgba(const Pixel& pixel)
{
    return "(" + std::to_string(pixel.r) + "," + std::to_string(pixel.g) + ","
        + std::to_string(pixel.b) + "," + std::to_string(pixel.a) + ")";
}

class FramebufferState {
public:
    static int current()
    {
        return currentFramebuffer_;
    }

    static void bind(int framebuffer)
    {
        currentFramebuffer_ = framebuffer;
    }

private:
    static int currentFramebuffer_;
};

int FramebufferState::currentFramebuffer_ = 0;

class FboControllerModel {
public:
    void check(int width, int height, bool transparent)
    {
        if (width <= 0 || height <= 0) {
            throw std::invalid_argument("FBO 尺寸必须大于 0");
        }
        // 与 HJPrioComFBOBase::check 保持一致：只按宽高决定是否重建。
        // 若业务允许同尺寸下切换透明属性，生产代码需要另行补充该判断。
        if (!ready_ || width != color_.width || height != color_.height) {
            color_.id = nextTextureId_++;
            color_.width = width;
            color_.height = height;
            framebufferId_ = nextFramebufferId_++;
            transparent_ = transparent;
            ready_ = true;
            ++rebuildCount_;
            hjstudy::logFields("HJPrioComFBOBase::check", "尺寸/透明属性变化，重建 FBO", {
                {"fbo", std::to_string(framebufferId_)},
                {"texture", std::to_string(color_.id)},
                {"size", std::to_string(width) + "x" + std::to_string(height)},
            });
        }
    }

    void attach()
    {
        if (!ready_) {
            throw std::logic_error("attach 前 FBO 未初始化");
        }
        previousFramebuffer_ = FramebufferState::current();
        FramebufferState::bind(framebufferId_);
        hjstudy::logFields("HJOGFBOCtrl::attach", "保存旧 FBO 并绑定 color attachment", {
            {"previous", std::to_string(previousFramebuffer_)},
            {"current", std::to_string(framebufferId_)},
        });
    }

    void detach()
    {
        FramebufferState::bind(previousFramebuffer_);
        hjstudy::logFields("HJOGFBOCtrl::detach", "恢复旧 framebuffer", {
            {"restored", std::to_string(previousFramebuffer_)},
        });
    }

    Texture& colorTexture()
    {
        return color_;
    }

    int rebuildCount() const
    {
        return rebuildCount_;
    }

private:
    inline static int nextFramebufferId_{10};
    inline static int nextTextureId_{100};
    Texture color_{};
    int framebufferId_{};
    int previousFramebuffer_{};
    int rebuildCount_{};
    bool transparent_{true};
    bool ready_{false};
};

class InvertFboEffect {
public:
    const Texture& render(const Texture& input)
    {
        fbo_.check(input.width, input.height, true);
        if (input.id == fbo_.colorTexture().id) {
            throw std::logic_error("禁止从正在写入的 color attachment 同时采样");
        }

        fbo_.attach();
        hjstudy::ScopeExit restore([this] { fbo_.detach(); });

        auto& output = fbo_.colorTexture();
        for (std::size_t i = 0; i < input.samples.size(); ++i) {
            const Pixel& source = input.samples[i];
            output.samples[i] = Pixel{
                static_cast<std::uint8_t>(255 - source.r),
                static_cast<std::uint8_t>(255 - source.g),
                static_cast<std::uint8_t>(255 - source.b),
                source.a,
            };
        }
        hjstudy::logLine(
            "Invert Fragment Shader",
            "FragColor = vec4(vec3(1.0) - texture(sTexture, uv).rgb, color.a)");
        return output;
    }

    int rebuildCount() const
    {
        return fbo_.rebuildCount();
    }

private:
    FboControllerModel fbo_;
};

void printSamples(std::string_view title, const Texture& texture)
{
    std::cout << title << " texture=" << texture.id << " size=" << texture.width << "x" << texture.height << '\n';
    for (const auto& pixel : texture.samples) {
        std::cout << "  " << rgba(pixel) << '\n';
    }
}

} // namespace

int main()
{
    hjstudy::printTitle("OpenGL Study Day 2 - FBO 反相与多 Pass 基础");
    hjstudy::printReferences(
        "openGL_Study/00-hjmedia-opengl-three-day-study-plan.md",
        "openGL_Study/day02-fbo-multipass.md",
        {
            "src/comp/graphic/HJOGFBOCtrl.cpp",
            "src/comp/prio/HJPrioComFBOBase.cpp",
            "src/comp/prio/HJPrioComFBOGray.cpp",
            "src/comp/prio/HJPrioComFBOBlur.cpp",
            "src/comp/prio/HJPrioComSourceSeries.cpp",
        });

    Texture input{
        1,
        1280,
        720,
        Samples{{
            Pixel{255, 0, 0, 255},
            Pixel{0, 255, 0, 255},
            Pixel{0, 0, 255, 255},
            Pixel{64, 128, 192, 128},
        }},
    };

    // 模拟外层已有 framebuffer，验证 detach 后不会泄漏 FBO 状态。
    FramebufferState::bind(77);
    InvertFboEffect invert;
    const Texture firstOutput = invert.render(input);
    assert(FramebufferState::current() == 77);
    assert(firstOutput.id != input.id);
    const Pixel expectedCyan{0, 255, 255, 255};
    const Pixel expectedInvertedSample{191, 127, 63, 128};
    assert(firstOutput.samples[0] == expectedCyan);
    assert(firstOutput.samples[3] == expectedInvertedSample);
    printSamples("720p 反相输出", firstOutput);

    // 同尺寸复用，不应重建；尺寸变成 1080p 时必须得到新的 FBO/texture。
    (void)invert.render(input);
    assert(invert.rebuildCount() == 1);
    input.width = 1920;
    input.height = 1080;
    const Texture secondOutput = invert.render(input);
    assert(invert.rebuildCount() == 2);
    assert(secondOutput.id != firstOutput.id);
    assert(FramebufferState::current() == 77);

    hjstudy::logLine("Ping-Pong 规则", "下一 Pass 读取当前输出时，必须写入另一张 FBO texture");
    hjstudy::logLine("验证结论", "反相像素、alpha 保留、FBO 状态恢复、同尺寸复用和尺寸变化重建均通过");
    return 0;
}
