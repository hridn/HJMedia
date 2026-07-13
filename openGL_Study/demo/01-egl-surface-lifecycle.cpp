/*
 * Day 29: EGL Surface 生命周期模拟
 *
 * 这个 demo 不直接调用系统 EGL，而是用可运行的 C++17 模型复刻
 * HJOGEGLCore / HJOGRenderEnv / HJOGEGLSurface 的控制流。
 * 学习重点：
 * 1. HJOGRenderEnv::priCoreInit 先创建 EGLContext 和 1x1 offscreen surface。
 * 2. 窗口创建/变更/销毁由 HJOGRenderEnv::priUpdateEglSurface 管理。
 * 3. 每次绘制都遵循 makeCurrent -> draw -> swap 的顺序。
 */

#include "study_demo_common.h"

#include <iomanip>
#include <memory>

namespace {

enum class SurfaceType {
    Offscreen,
    UiWindow,
    Encoder,
};

std::string toString(SurfaceType type)
{
    switch (type) {
    case SurfaceType::Offscreen:
        return "offscreen";
    case SurfaceType::UiWindow:
        return "ui-window";
    case SurfaceType::Encoder:
        return "encoder";
    }
    return "unknown";
}

struct Surface {
    int id{};
    int width{};
    int height{};
    int fps{};
    SurfaceType type{SurfaceType::Offscreen};
    std::string nativeWindow;
    int64_t renderIndex{};
};

class FakeEglCore {
public:
    void init()
    {
        displayReady_ = true;
        contextReady_ = true;
        hjstudy::logLine("HJOGEGLCore::init", "eglGetDisplay -> eglInitialize -> eglChooseConfig -> eglCreateContext");
    }

    std::shared_ptr<Surface> createOffscreenSurface(int width, int height)
    {
        auto surface = createSurface(width, height, 0, SurfaceType::Offscreen, "pbuffer");
        hjstudy::logFields("HJOGEGLCore::EGLOffScreenSurfaceCreate", "eglCreatePbufferSurface", {
            {"surface", std::to_string(surface->id)},
            {"size", std::to_string(width) + "x" + std::to_string(height)},
        });
        return surface;
    }

    std::shared_ptr<Surface> createWindowSurface(std::string window, int width, int height, int fps, SurfaceType type)
    {
        auto surface = createSurface(width, height, fps, type, std::move(window));
        hjstudy::logFields("HJOGEGLCore::EGLSurfaceCreate", "eglCreateWindowSurface", {
            {"surface", std::to_string(surface->id)},
            {"window", surface->nativeWindow},
            {"type", toString(type)},
            {"size", std::to_string(width) + "x" + std::to_string(height)},
        });
        return surface;
    }

    void makeCurrent(const Surface& surface)
    {
        if (!displayReady_ || !contextReady_) {
            throw std::runtime_error("EGL context is not ready");
        }
        currentSurfaceId_ = surface.id;
        hjstudy::logFields("HJOGEGLCore::makeCurrent", "eglMakeCurrent", {
            {"surface", std::to_string(surface.id)},
            {"type", toString(surface.type)},
        });
    }

    void swap(const Surface& surface)
    {
        hjstudy::logFields("HJOGEGLCore::swap", "eglSwapBuffers", {
            {"surface", std::to_string(surface.id)},
            {"renderIndex", std::to_string(surface.renderIndex)},
        });
    }

    void release(const Surface& surface)
    {
        hjstudy::logFields("HJOGEGLCore::EGLSurfaceRelease", "eglDestroySurface + make no current", {
            {"surface", std::to_string(surface.id)},
            {"window", surface.nativeWindow},
        });
        if (currentSurfaceId_ == surface.id) {
            currentSurfaceId_ = 0;
        }
    }

    void done()
    {
        hjstudy::logLine("HJOGEGLCore::done", "eglDestroyContext -> eglTerminate");
        displayReady_ = false;
        contextReady_ = false;
        currentSurfaceId_ = 0;
    }

private:
    std::shared_ptr<Surface> createSurface(int width, int height, int fps, SurfaceType type, std::string window)
    {
        auto surface = std::make_shared<Surface>();
        surface->id = nextSurfaceId_++;
        surface->width = width;
        surface->height = height;
        surface->fps = fps;
        surface->type = type;
        surface->nativeWindow = std::move(window);
        return surface;
    }

    bool displayReady_{false};
    bool contextReady_{false};
    int nextSurfaceId_{1};
    int currentSurfaceId_{0};
};

class RenderEnv {
public:
    void init()
    {
        core_.init();
        offscreen_ = core_.createOffscreenSurface(1, 1);
        core_.makeCurrent(*offscreen_);
        hjstudy::logLine("HJOGRenderEnv::priCoreInit", "render thread keeps a valid offscreen current surface");
    }

    void createTarget(std::string window, int width, int height, int fps, SurfaceType type)
    {
        auto surface = core_.createWindowSurface(std::move(window), width, height, fps, type);
        surfaces_.push_back(surface);
    }

    void changeTarget(const std::string& window, int width, int height)
    {
        for (auto& surface : surfaces_) {
            if (surface->nativeWindow == window) {
                surface->width = width;
                surface->height = height;
                hjstudy::logFields("HJOGRenderEnv::priUpdateEglSurface", "HJTargetState_Change", {
                    {"window", window},
                    {"newSize", std::to_string(width) + "x" + std::to_string(height)},
                });
            }
        }
    }

    void destroyTarget(const std::string& window)
    {
        surfaces_.erase(std::remove_if(surfaces_.begin(), surfaces_.end(), [&](const std::shared_ptr<Surface>& surface) {
            if (surface->nativeWindow == window) {
                core_.release(*surface);
                return true;
            }
            return false;
        }), surfaces_.end());
    }

    void renderFrame(int graphFps)
    {
        core_.makeCurrent(*offscreen_);
        hjstudy::logLine("HJOGRenderEnv::foreachRender", "update graph state on offscreen surface");

        if (surfaces_.empty()) {
            drawTo(*offscreen_);
            return;
        }

        for (auto& surface : surfaces_) {
            const int targetFps = surface->fps <= 0 ? graphFps : std::min(surface->fps, graphFps);
            const int ratio = std::max(1, graphFps / targetFps);
            if ((frameIndex_ % ratio) == 0) {
                drawTo(*surface);
                ++surface->renderIndex;
            }
        }
        ++frameIndex_;
    }

    void done()
    {
        for (const auto& surface : surfaces_) {
            core_.release(*surface);
        }
        surfaces_.clear();
        if (offscreen_) {
            core_.release(*offscreen_);
        }
        core_.done();
    }

private:
    void drawTo(Surface& surface)
    {
        core_.makeCurrent(surface);
        hjstudy::logFields("HJRteComDrawEGL::bind/render", "glClear + glViewport + shader->draw", {
            {"surface", std::to_string(surface.id)},
            {"size", std::to_string(surface.width) + "x" + std::to_string(surface.height)},
            {"type", toString(surface.type)},
        });
        core_.swap(surface);
    }

    FakeEglCore core_;
    std::shared_ptr<Surface> offscreen_;
    std::vector<std::shared_ptr<Surface>> surfaces_;
    int64_t frameIndex_{0};
};

} // namespace

int main()
{
    hjstudy::printTitle("Day 29 - EGL Surface 生命周期");
    hjstudy::printReferences(
        "openGL_Study/01-opengl-study-plan.md",
        "openGL_Study/02-opengl-study-notes.md",
        {
            "src/comp/graphic/hsys/HJOGEGLCore.cpp",
            "src/comp/graphic/hsys/HJOGRenderEnv.cpp",
            "src/comp/graphic/HJOGEGLSurface.cpp",
            "src/comp/rte/HJRteComDraw.cpp",
        });

    RenderEnv env;
    env.init();
    env.createTarget("uiWindow#1", 1280, 720, 30, SurfaceType::UiWindow);
    env.createTarget("encoderWindow#1", 720, 1280, 15, SurfaceType::Encoder);

    for (int i = 0; i < 4; ++i) {
        env.renderFrame(30);
    }

    env.changeTarget("uiWindow#1", 1920, 1080);
    env.renderFrame(30);
    env.destroyTarget("encoderWindow#1");
    env.renderFrame(30);
    env.done();

    hjstudy::logLine("面试复述", "HJMedia 先用 offscreen surface 保持 GL 环境，再按窗口目标 makeCurrent/draw/swap。");
    return 0;
}
