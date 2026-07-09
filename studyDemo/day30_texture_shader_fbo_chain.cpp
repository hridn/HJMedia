/*
 * Day 30: Texture / Shader / FBO 渲染链模拟
 *
 * 这个 demo 对应 HJOGCommon、HJOGShaderProgram、HJOGCopyShaderStrip、
 * HJOGFBOCtrl 和 HJRteComDrawFBO 的协作关系。
 * 学习重点：
 * 1. OES 纹理代表外部视频帧，2D 纹理代表 RGBA 或 FBO 中间结果。
 * 2. Shader draw 的稳定顺序是 useProgram -> bind texture -> set matrix -> drawArrays。
 * 3. FBO attach 后，shader 输出不再直接到屏幕，而是写入 FBO 的 color texture。
 */

#include "study_demo_common.h"

#include <memory>

namespace {

enum class TextureType {
    Texture2D,
    TextureOES,
};

std::string toString(TextureType type)
{
    return type == TextureType::Texture2D ? "GL_TEXTURE_2D" : "GL_TEXTURE_EXTERNAL_OES";
}

struct Texture {
    int id{};
    int width{};
    int height{};
    TextureType type{TextureType::Texture2D};
    std::string producer;
};

class TextureFactory {
public:
    Texture create(TextureType type, int width, int height, std::string producer)
    {
        Texture texture{nextId_++, width, height, type, std::move(producer)};
        hjstudy::logFields("HJOGCommon::textureCreate", "glGenTextures + glTexParameteri", {
            {"id", std::to_string(texture.id)},
            {"target", toString(type)},
            {"producer", texture.producer},
        });
        return texture;
    }

    void uploadRgba(Texture& texture)
    {
        hjstudy::logFields("HJOGCommon::textureUploadRGBA", "glBindTexture + glTexImage2D", {
            {"id", std::to_string(texture.id)},
            {"size", std::to_string(texture.width) + "x" + std::to_string(texture.height)},
        });
    }

private:
    int nextId_{100};
};

class ShaderProgram {
public:
    explicit ShaderProgram(std::string name)
        : name_(std::move(name))
    {
        hjstudy::logFields("HJOGShaderProgram::init", "compile vertex/fragment and link program", {
            {"shader", name_},
        });
    }

    void draw(const Texture& texture, int dstWidth, int dstHeight, std::string renderMode)
    {
        hjstudy::logFields("HJOGCopyShaderStrip::draw", "glUseProgram + bind texture + uniforms + glDrawArrays", {
            {"shader", name_},
            {"texture", std::to_string(texture.id)},
            {"target", toString(texture.type)},
            {"src", std::to_string(texture.width) + "x" + std::to_string(texture.height)},
            {"dst", std::to_string(dstWidth) + "x" + std::to_string(dstHeight)},
            {"mode", std::move(renderMode)},
        });
    }

private:
    std::string name_;
};

class Fbo {
public:
    Fbo(std::string name, Texture colorTexture)
        : name_(std::move(name))
        , colorTexture_(std::move(colorTexture))
    {
        hjstudy::logFields("HJOGFBOCtrl::init", "glGenFramebuffers + glFramebufferTexture2D + glCheckFramebufferStatus", {
            {"fbo", name_},
            {"colorTexture", std::to_string(colorTexture_.id)},
            {"size", std::to_string(colorTexture_.width) + "x" + std::to_string(colorTexture_.height)},
        });
    }

    void attach()
    {
        hjstudy::logFields("HJOGFBOCtrl::attach", "save previous framebuffer + glBindFramebuffer + glViewport + glClear", {
            {"fbo", name_},
            {"size", std::to_string(colorTexture_.width) + "x" + std::to_string(colorTexture_.height)},
        });
    }

    void detach()
    {
        hjstudy::logFields("HJOGFBOCtrl::detach", "restore previous framebuffer", {
            {"fbo", name_},
        });
    }

    const Texture& texture() const
    {
        return colorTexture_;
    }

private:
    std::string name_;
    Texture colorTexture_;
};

class DrawFboComponent {
public:
    DrawFboComponent(std::string name, std::shared_ptr<ShaderProgram> shader, Fbo fbo)
        : name_(std::move(name))
        , shader_(std::move(shader))
        , fbo_(std::move(fbo))
    {
    }

    Texture render(const Texture& input)
    {
        hjstudy::logFields("HJRteComDrawFBO::bind", "acquire FBO and attach", {
            {"component", name_},
        });
        fbo_.attach();
        shader_->draw(input, fbo_.texture().width, fbo_.texture().height, "fit-center");
        fbo_.detach();
        hjstudy::logFields("HJRteComDrawFBO::render", "output drift texture from FBO", {
            {"component", name_},
            {"texture", std::to_string(fbo_.texture().id)},
        });
        return fbo_.texture();
    }

private:
    std::string name_;
    std::shared_ptr<ShaderProgram> shader_;
    Fbo fbo_;
};

} // namespace

int main()
{
    hjstudy::printTitle("Day 30 - Texture / Shader / FBO 渲染链");
    hjstudy::printReferences(
        "study/opengl-api-project-practice-plan.md",
        "studyNote/opengl-api-project-practice.md",
        {
            "src/comp/graphic/HJOGCommon.cpp",
            "src/comp/graphic/HJOGShaderProgram.cpp",
            "src/comp/graphic/HJOGCopyShaderStrip.cpp",
            "src/comp/graphic/HJOGFBOCtrl.cpp",
            "src/comp/rte/HJRteComDraw.cpp",
        });

    TextureFactory factory;
    Texture cameraOes = factory.create(TextureType::TextureOES, 1280, 720, "OH_NativeImage external frame");
    Texture overlay2D = factory.create(TextureType::Texture2D, 512, 512, "CPU RGBA sticker");
    factory.uploadRgba(overlay2D);

    auto copyOes = std::make_shared<ShaderProgram>("HJOGBaseShaderType_Copy_OES");
    auto gray2D = std::make_shared<ShaderProgram>("HJOGBaseShaderType_Gray");
    auto screenCopy = std::make_shared<ShaderProgram>("HJOGBaseShaderType_PreMul_Copy_2D");

    Fbo cameraFbo("camera-copy-fbo", factory.create(TextureType::Texture2D, 1280, 720, "FBO color attachment"));
    DrawFboComponent copyToFbo("HJRteComDrawCopyOESFBO", copyOes, std::move(cameraFbo));
    Texture copied = copyToFbo.render(cameraOes);

    Fbo grayFbo("gray-filter-fbo", factory.create(TextureType::Texture2D, 1280, 720, "gray output attachment"));
    DrawFboComponent grayFilter("HJRteComDrawGrayFBO", gray2D, std::move(grayFbo));
    Texture gray = grayFilter.render(copied);

    hjstudy::logLine("HJRteComDrawEGL::bind", "eglMakeCurrent + glClear");
    screenCopy->draw(gray, 1920, 1080, "fit-center");
    screenCopy->draw(overlay2D, 1920, 1080, "overlay-premultiplied-alpha");
    hjstudy::logLine("HJRteComDrawEGL::unbind", "eglSwapBuffers");

    hjstudy::logLine("面试复述", "HJMedia 用 FBO 把每个滤镜输出变成下一段链路的 2D texture。");
    return 0;
}
