#define SDL_MAIN_HANDLED
#include "engine/platform/Window.h"
#include "engine/render/opengl/OpenGLPixelPresenter.h"
#include <glad/glad.h>
#include <SDL.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace engine::render;
#define CHECK(c) do { if(!(c)) throw std::runtime_error(std::to_string(__LINE__)+": " #c); } while(false)

// Independent box-footprint reference: preserve a texel's interior and blend
// only in the one-output-pixel band straddling an input-pixel boundary.
double coordinate(double p,double scale) {
    double whole=std::floor(p),f=p-whole;
    return whole+.5+std::min(0.0,f*scale-.5)+std::max(0.0,(f-1)*scale+.5);
}
void compare(OpenGLPixelPresenter& presenter,std::vector<uint32_t> data,int sw,int sh,PixelViewport r,PixelFilter filter) {
    constexpr int surface=128;
    glViewport(0,0,surface,surface); glDisable(GL_SCISSOR_TEST); glColorMask(1,1,1,1);
    glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
    presenter.draw(data,sw,sh,r,surface,filter);
    std::vector<uint32_t> actual(surface*surface);
    glReadPixels(0,0,surface,surface,GL_RGBA,GL_UNSIGNED_BYTE,actual.data());
    for(int y=0;y<surface;++y) for(int x=0;x<surface;++x) {
        bool inside=x>=r.x && x<r.x+r.width && y>=r.y && y<r.y+r.height;
        double px=(x-r.x+.5)*sw/r.width,py=(y-r.y+.5)*sh/r.height;
        for(unsigned channel=0;channel<3;++channel) {
            auto value=[&](int sx,int sy) { return double((data[std::clamp(sy,0,sh-1)*sw+std::clamp(sx,0,sw-1)]>>(channel*8))&255); };
            double expected=0;
            if(inside && filter==PixelFilter::Nearest) expected=value(int(px),int(py));
            if(inside && filter==PixelFilter::Sharp) {
                px=channel==0?coordinate(px,std::max(1.0,double(r.width)/sw)):px;
                py=channel==0?coordinate(py,std::max(1.0,double(r.height)/sh)):py;
                double tx=px-.5,ty=py-.5;
                int ix=int(std::floor(tx)),iy=int(std::floor(ty)); double fx=tx-ix,fy=ty-iy;
                expected=(value(ix,iy)*(1-fx)+value(ix+1,iy)*fx)*(1-fy)+(value(ix,iy+1)*(1-fx)+value(ix+1,iy+1)*fx)*fy;
            }
            int got=(actual[(surface-1-y)*surface+x]>>(channel*8))&255;
            if(std::abs(got-expected)>1.6) throw std::runtime_error("Pixel mismatch at "+std::to_string(x)+","+std::to_string(y)+" got="+std::to_string(got)+" expected="+std::to_string(expected));
        }
    }
    CHECK(glGetError()==GL_NO_ERROR);
}
void stateRestoration(OpenGLPixelPresenter& presenter) {
    GLuint buffer,texture,sampler,vao;
    glGenBuffers(1,&buffer); glBindBuffer(GL_PIXEL_UNPACK_BUFFER,buffer); glBufferData(GL_PIXEL_UNPACK_BUFFER,512,nullptr,GL_STREAM_DRAW);
    glGenTextures(1,&texture); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,texture);
    glGenSamplers(1,&sampler); glBindSampler(0,sampler);
    glGenVertexArrays(1,&vao); glBindVertexArray(vao);
    glActiveTexture(GL_TEXTURE3);
    glPixelStorei(GL_UNPACK_ALIGNMENT,8); glPixelStorei(GL_UNPACK_ROW_LENGTH,7);
    glPixelStorei(GL_UNPACK_SKIP_ROWS,1); glPixelStorei(GL_UNPACK_SKIP_PIXELS,2);
    glEnable(GL_DEPTH_TEST); glEnable(GL_CULL_FACE); glEnable(GL_BLEND); glEnable(GL_SCISSOR_TEST); glEnable(GL_STENCIL_TEST); glEnable(GL_FRAMEBUFFER_SRGB);
    glViewport(2,3,7,9); glColorMask(0,1,0,1);
    std::array<uint32_t,4> data={0xff0000ff,0xff00ff00,0xffff0000,0xffffffff};
    presenter.draw(data,2,2,{5,6,8,8},128,PixelFilter::Sharp);
    GLint value=0; glGetIntegerv(GL_ACTIVE_TEXTURE,&value); CHECK(value==GL_TEXTURE3);
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&value); CHECK(value==int(buffer));
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&value); CHECK(value==int(vao));
    glGetIntegerv(GL_UNPACK_ALIGNMENT,&value); CHECK(value==8);
    glGetIntegerv(GL_UNPACK_ROW_LENGTH,&value); CHECK(value==7);
    glGetIntegerv(GL_UNPACK_SKIP_ROWS,&value); CHECK(value==1);
    glGetIntegerv(GL_UNPACK_SKIP_PIXELS,&value); CHECK(value==2);
    std::array<GLint,4> viewport{}; glGetIntegerv(GL_VIEWPORT,viewport.data()); CHECK((viewport==std::array<GLint,4>{2,3,7,9}));
    std::array<GLboolean,4> mask{}; glGetBooleanv(GL_COLOR_WRITEMASK,mask.data()); CHECK((mask==std::array<GLboolean,4>{0,1,0,1}));
    for(GLenum cap:{GL_DEPTH_TEST,GL_CULL_FACE,GL_BLEND,GL_SCISSOR_TEST,GL_STENCIL_TEST,GL_FRAMEBUFFER_SRGB}) { CHECK(glIsEnabled(cap)); glDisable(cap); }
    glActiveTexture(GL_TEXTURE0); glGetIntegerv(GL_TEXTURE_BINDING_2D,&value); CHECK(value==int(texture));
    glGetIntegerv(GL_SAMPLER_BINDING,&value); CHECK(value==int(sampler));
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0); glBindSampler(0,0); glBindVertexArray(0);
    glDeleteBuffers(1,&buffer); glDeleteTextures(1,&texture); glDeleteSamplers(1,&sampler); glDeleteVertexArrays(1,&vao);
    glPixelStorei(GL_UNPACK_ROW_LENGTH,0); glPixelStorei(GL_UNPACK_SKIP_ROWS,0); glPixelStorei(GL_UNPACK_SKIP_PIXELS,0); glColorMask(1,1,1,1);
}
void rasterStateIsolation(OpenGLPixelPresenter& presenter) {
    const std::vector<uint32_t> pixels{0xff0000ff,0xff00ff00,0xffff0000,0xffffffff};
    glPolygonMode(GL_FRONT_AND_BACK,GL_LINE);
    std::array<GLint,2> before{};
    glGetIntegerv(GL_POLYGON_MODE,before.data());
    CHECK(before[0]==GL_LINE);
    compare(presenter,pixels,2,2,{0,0,128,128},PixelFilter::Nearest);
    std::array<GLint,2> polygonMode{};
    glGetIntegerv(GL_POLYGON_MODE,polygonMode.data());
    CHECK(polygonMode==before);
    glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
    for(GLenum cap:{GL_RASTERIZER_DISCARD,GL_COLOR_LOGIC_OP}) {
        glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
        glLogicOp(GL_CLEAR); glEnable(cap);
        compare(presenter,pixels,2,2,{0,0,128,128},PixelFilter::Nearest);
        CHECK(glIsEnabled(cap)); glDisable(cap);
    }
    glLogicOp(GL_COPY);
}
int main() {
    try {
        SDL_SetMainReady();
        struct SdlLifetime { ~SdlLifetime(){SDL_Quit();} } sdl;
        Window window("Pixel presenter contracts",128,128,Window::GraphicsApi::OpenGL,false,false);
        CHECK(gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress)));
        OpenGLPixelPresenter presenter;
        for(PixelFilter filter:{PixelFilter::Nearest,PixelFilter::Sharp}) {
            compare(presenter,{0xff1020e0,0xff50d030,0xffe08010,0xffefdf9f,0xff1090af,0xff3020d0},3,2,{4,7,18,12},filter);
            compare(presenter,{0xffffffff,0xff000000,0xff202080,0xff808020,0xff0080ff,0xffa05010},3,2,{7,9,29,19},filter);
            compare(presenter,{0xff112233,0xff998877,0xffaacc00,0xffdd2244},2,2,{2,3,80,83},filter);
        }
        for(unsigned i=0;i<7;++i) {
            std::vector<uint32_t> changing(6,0xff000000u|((i*37u)<<16)|((255-i*31u)<<8)|(i*29u));
            compare(presenter,changing,3,2,{5,8,83,59},PixelFilter::Sharp);
            auto timings=presenter.timings();
            CHECK(std::isfinite(timings.stateMs) && timings.stateMs>=0 && timings.uploadMs>=0 && timings.drawMs>=0);
        }
        stateRestoration(presenter);
        rasterStateIsolation(presenter);
        bool rejected=false; try { presenter.draw({},2,2,{0,0,2,2},128,PixelFilter::Nearest); } catch(const std::invalid_argument&) { rejected=true; } CHECK(rejected);
        CHECK(glGetError()==GL_NO_ERROR);
        std::cout<<"Pixel presenter GPU/reference, dynamic upload, resize, borders, state restoration and invalid-input contracts passed\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
