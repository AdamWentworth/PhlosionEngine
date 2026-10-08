#include "OpenGLPixelPresenter.h"
#include <glad/glad.h>
#include <array>
#include <chrono>
#include <stdexcept>
#include <string>

namespace engine::render {
namespace {
GLuint shader(GLenum kind,const char* text) {
    GLuint result=glCreateShader(kind);
    glShaderSource(result,1,&text,nullptr); glCompileShader(result);
    GLint ok=0; glGetShaderiv(result,GL_COMPILE_STATUS,&ok);
    if(!ok) {
        std::array<char,2048> log{}; glGetShaderInfoLog(result,static_cast<GLsizei>(log.size()),nullptr,log.data());
        glDeleteShader(result); throw std::runtime_error(std::string("Pixel presenter shader: ")+log.data());
    }
    return result;
}
struct State {
    GLint program,vao,activeTexture,texture,sampler,unpackBuffer,alignment,rowLength,skipRows,skipPixels;
    std::array<GLint,4> viewport{};
    std::array<GLint,2> polygonMode{};
    std::array<GLboolean,4> colorMask{};
    static constexpr std::array<GLenum,8> caps={GL_DEPTH_TEST,GL_BLEND,GL_CULL_FACE,GL_SCISSOR_TEST,GL_STENCIL_TEST,GL_FRAMEBUFFER_SRGB,GL_RASTERIZER_DISCARD,GL_COLOR_LOGIC_OP};
    std::array<GLboolean,caps.size()> enabled{};
    State() {
        glGetIntegerv(GL_CURRENT_PROGRAM,&program); glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao);
        glGetIntegerv(GL_VIEWPORT,viewport.data()); glGetBooleanv(GL_COLOR_WRITEMASK,colorMask.data());
        glGetIntegerv(GL_POLYGON_MODE,polygonMode.data()); glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
        glGetIntegerv(GL_ACTIVE_TEXTURE,&activeTexture); glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture); glGetIntegerv(GL_SAMPLER_BINDING,&sampler);
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&unpackBuffer);
        glGetIntegerv(GL_UNPACK_ALIGNMENT,&alignment); glGetIntegerv(GL_UNPACK_ROW_LENGTH,&rowLength);
        glGetIntegerv(GL_UNPACK_SKIP_ROWS,&skipRows); glGetIntegerv(GL_UNPACK_SKIP_PIXELS,&skipPixels);
        for(unsigned i=0;i<caps.size();++i) { enabled[i]=glIsEnabled(caps[i]); glDisable(caps[i]); }
    }
    ~State() {
        for(unsigned i=0;i<caps.size();++i) if(enabled[i]) glEnable(caps[i]); else glDisable(caps[i]);
        glUseProgram(program); glBindVertexArray(vao);
        glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
        glPolygonMode(GL_FRONT_AND_BACK,polygonMode[0]);
        glColorMask(colorMask[0],colorMask[1],colorMask[2],colorMask[3]);
        glBindTexture(GL_TEXTURE_2D,texture); glBindSampler(0,sampler); glActiveTexture(activeTexture);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,unpackBuffer);
        glPixelStorei(GL_UNPACK_ALIGNMENT,alignment); glPixelStorei(GL_UNPACK_ROW_LENGTH,rowLength);
        glPixelStorei(GL_UNPACK_SKIP_ROWS,skipRows); glPixelStorei(GL_UNPACK_SKIP_PIXELS,skipPixels);
    }
};
}

OpenGLPixelPresenter::OpenGLPixelPresenter() {
    const char* vertex=R"(#version 330 core
out vec2 uv;
void main() {
    vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);
    gl_Position=vec4(p*2.0-1.0,0.0,1.0);
    uv=vec2(p.x,1.0-p.y);
})";
    const char* fragment=R"(#version 330 core
in vec2 uv;
out vec4 color;
uniform sampler2D screen;
uniform vec2 sourceSize;
uniform vec2 scale;
uniform int sharp;
void main() {
    vec2 sampleUv=uv;
    if(sharp!=0) {
        vec2 p=uv*sourceSize-0.5;
        vec2 blend=clamp((fract(p)-0.5)*max(scale,vec2(1.0))+0.5,0.0,1.0);
        sampleUv=(floor(p)+0.5+blend)/sourceSize;
    }
    color=vec4(texture(screen,sampleUv).rgb,1.0);
})";
    GLuint vs=0,fs=0;
    try {
        vs=shader(GL_VERTEX_SHADER,vertex); fs=shader(GL_FRAGMENT_SHADER,fragment);
        program_=glCreateProgram(); glAttachShader(program_,vs); glAttachShader(program_,fs); glLinkProgram(program_);
        GLint ok=0; glGetProgramiv(program_,GL_LINK_STATUS,&ok);
        if(!ok) throw std::runtime_error("Pixel presenter program link failed");
        sizeLocation_=glGetUniformLocation(program_,"sourceSize");
        scaleLocation_=glGetUniformLocation(program_,"scale"); filterLocation_=glGetUniformLocation(program_,"sharp");
        glGenVertexArrays(1,&vao_); glGenTextures(static_cast<GLsizei>(textures_.size()),textures_.data());
    } catch(...) {
        if(vs) glDeleteShader(vs); if(fs) glDeleteShader(fs);
        if(program_) glDeleteProgram(program_);
        throw;
    }
    glDeleteShader(vs); glDeleteShader(fs);
}
OpenGLPixelPresenter::~OpenGLPixelPresenter() {
    glDeleteTextures(static_cast<GLsizei>(textures_.size()),textures_.data()); glDeleteVertexArrays(1,&vao_); glDeleteProgram(program_);
}
void OpenGLPixelPresenter::draw(std::span<const std::uint32_t> pixels,int sourceWidth,int sourceHeight,
                               PixelViewport viewport,int surfaceHeight,PixelFilter filter) {
    if(sourceWidth<=0 || sourceHeight<=0 || sourceWidth>16384 || sourceHeight>16384 ||
       pixels.size()!=static_cast<std::size_t>(sourceWidth)*sourceHeight || viewport.width<=0 || viewport.height<=0 ||
       viewport.x<0 || viewport.y<0 || surfaceHeight<=0 || viewport.y>surfaceHeight-viewport.height ||
       (filter!=PixelFilter::Nearest && filter!=PixelFilter::Sharp))
        throw std::invalid_argument("Invalid pixel presentation dimensions or filter");
    using Clock=std::chrono::steady_clock;
    auto start=Clock::now();
    State previous;
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glBindVertexArray(vao_); glUseProgram(program_);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0); glBindSampler(0,0);
    glPixelStorei(GL_UNPACK_ALIGNMENT,4); glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS,0); glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
    auto ready=Clock::now();
    // Rotate persistent images so a fullscreen present still using the previous
    // texture does not serialize the next CPU upload behind that frame.
    unsigned texture=textures_[nextTexture_]; nextTexture_=(nextTexture_+1)%static_cast<unsigned>(textures_.size());
    if(width_!=sourceWidth || height_!=sourceHeight) {
        width_=sourceWidth; height_=sourceHeight;
        for(unsigned image:textures_) {
            glBindTexture(GL_TEXTURE_2D,image);
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width_,height_,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        }
    }
    glBindTexture(GL_TEXTURE_2D,texture);
    GLint sampling=filter==PixelFilter::Sharp?GL_LINEAR:GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,sampling); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,sampling);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,width_,height_,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    auto uploaded=Clock::now();
    glViewport(viewport.x,surfaceHeight-viewport.y-viewport.height,viewport.width,viewport.height);
    glUniform2f(sizeLocation_,static_cast<float>(width_),static_cast<float>(height_));
    glUniform2f(scaleLocation_,static_cast<float>(viewport.width)/width_,static_cast<float>(viewport.height)/height_);
    glUniform1i(filterLocation_,filter==PixelFilter::Sharp?1:0);
    glDrawArrays(GL_TRIANGLES,0,3);
    timings_={std::chrono::duration<double,std::milli>(ready-start).count(),
              std::chrono::duration<double,std::milli>(uploaded-ready).count(),
              std::chrono::duration<double,std::milli>(Clock::now()-uploaded).count()};
}
}
