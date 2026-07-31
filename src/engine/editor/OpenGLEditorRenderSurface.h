#pragma once

#include <array>
#include <cstdint>

namespace engine::editor {

class OpenGLEditorRenderSurface {
public:
    OpenGLEditorRenderSurface() = default;
    ~OpenGLEditorRenderSurface();

    OpenGLEditorRenderSurface(
        const OpenGLEditorRenderSurface&) = delete;
    OpenGLEditorRenderSurface& operator=(
        const OpenGLEditorRenderSurface&) = delete;

    bool begin(int width, int height);
    void end();
    void shutdown();

    std::uint32_t textureId() const noexcept {
        return texture_;
    }
    int width() const noexcept {
        return width_;
    }
    int height() const noexcept {
        return height_;
    }
    bool valid() const noexcept {
        return framebuffer_ != 0u && texture_ != 0u;
    }

private:
    bool ensure(int width, int height);

    std::uint32_t framebuffer_ = 0u;
    std::uint32_t texture_ = 0u;
    std::uint32_t depthStencil_ = 0u;
    int width_ = 0;
    int height_ = 0;
    int previousDrawFramebuffer_ = 0;
    int previousReadFramebuffer_ = 0;
    std::array<int, 4> previousViewport_{};
    bool active_ = false;
};

} // namespace engine::editor
