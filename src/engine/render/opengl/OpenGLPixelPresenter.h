#pragma once
#include <cstdint>
#include <span>
#include <array>

namespace engine::render {

enum class PixelFilter { Nearest, Sharp };
struct PixelViewport { int x=0,y=0,width=0,height=0; };
struct PixelPresenterTimings { double stateMs=0,uploadMs=0,drawMs=0; };

// Optional OpenGL 3.3 presentation for CPU-generated, top-down opaque RGBA8.
// Create/use/destroy on the owning render thread with its GL context current.
// Draws into the currently bound framebuffer and restores affected GL state.
class OpenGLPixelPresenter {
public:
    OpenGLPixelPresenter();
    ~OpenGLPixelPresenter();
    OpenGLPixelPresenter(const OpenGLPixelPresenter&)=delete;
    OpenGLPixelPresenter& operator=(const OpenGLPixelPresenter&)=delete;
    void draw(std::span<const std::uint32_t> pixels,int sourceWidth,int sourceHeight,
              PixelViewport viewport,int surfaceHeight,PixelFilter filter);
    PixelPresenterTimings timings() const { return timings_; }
private:
    unsigned program_=0,vao_=0,nextTexture_=0;
    std::array<unsigned,3> textures_{};
    PixelPresenterTimings timings_{};
    int width_=0,height_=0,sizeLocation_=-1,scaleLocation_=-1,filterLocation_=-1;
};
}
