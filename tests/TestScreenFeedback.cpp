#include "engine/render/LegacyOpenGL.h"
#define SDL_MAIN_HANDLED
#include "engine/platform/Window.h"
#include "engine/render/OpenGLRenderBackend.h"
#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/VulkanRenderBackend.h"
#include "engine/render/ScreenFeedback.h"
#include <glad/glad.h>
#include <stb_image.h>
#include <SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace engine::render;
using Color=std::array<double,3>;
constexpr int W=8,H=6,SW=320,SH=180;
using History=std::array<Color,W*H>;
constexpr std::array<unsigned char,16> mask={255,255,255,0,255,255,255,128,255,255,255,192,255,255,255,255};

Color encoded(Color c) {
    for(auto& v:c)v=std::floor((v<=.0031308?v*12.92:1.055*std::pow(v,1/2.4)-.055)*255+.5);
    return c;
}
Color compose(const History& history,const ScreenFeedbackUpdate& update,double x,double y) {
    Color result=encoded({.8,.1,.02});
    for(int i=0;i<update.count;++i) {
        const auto& pass=update.passes[static_cast<std::size_t>(i)];const auto& r=pass.rectangle;
        if(x<r[0] || y<r[1] || x>=r[2] || y>=r[3])continue;
        const double tx=(x-r[0])/(r[2]-r[0]),ty=(y-r[1])/(r[3]-r[1]);
        const double u=std::lerp(double(pass.uv[0]),double(pass.uv[2]),tx);
        const double v=std::lerp(double(pass.uv[1]),double(pass.uv[3]),ty);
        const double px=std::floor(std::max(0.,u)*W*128)/128-.5,py=std::floor(std::max(0.,v)*H*128)/128-.5;
        const int ix=int(std::floor(px)),iy=int(std::floor(py));
        auto sample=[&](int dx,int dy,int c){return history[std::clamp(iy+dy,0,H-1)*W+std::clamp(ix+dx,0,W-1)][c];};
        double alpha=pass.options[1]/255.;double gate=1;
        if(pass.options[0]==1) {
            const int m=mask[std::clamp(int(tx*4),0,3)*4+3];alpha*=m/255.;gate=m>128?1:0;
        }
        for(int c=0;c<3;++c) {
            const double top=std::lerp(sample(0,0,c),sample(1,0,c),px-ix);
            const double bottom=std::lerp(sample(0,1,c),sample(1,1,c),px-ix);
            const double source=pass.options[0]==2?0:gate*std::floor(std::lerp(top,bottom,py-iy)+.5);
            result[c]=std::floor(result[c]*(1-alpha)+source*alpha+.5);
        }
    }
    return result;
}
History update(const History& history,const ScreenFeedbackUpdate& program) {
    History result{};
    for(int y=0;y<H;++y)for(int x=0;x<W;++x) {
        Color sum{};
        for(double dy:{.25,.75})for(double dx:{.25,.75}) {
            const auto value=compose(history,program,x+dx,y+dy);
            for(int c=0;c<3;++c)sum[c]+=value[c];
        }
        for(int c=0;c<3;++c)result[y*W+x][c]=std::floor(sum[c]/4);
    }
    return result;
}
int main(int argc,char** argv) {
    if(argc!=3)return 2;
    try {
        SDL_SetMainReady();struct Quit {~Quit(){SDL_Quit();}} quit;
        std::filesystem::create_directories(std::filesystem::path(argv[2]).parent_path());
        const auto environment=[](const char* key,const char* value) {
#if defined(_MSC_VER)
            _putenv_s(key,value);
#else
            setenv(key,value,1);
#endif
        };
        environment("PHLOSION_BACKEND_SCREENSHOT_PATH",argv[2]);
        environment("PHLOSION_BACKEND_SCREENSHOT_FRAME","4");
        environment("PHLOSION_BACKEND_SCREENSHOT_DEFER","0");
        const std::string api=argv[1];
        Window window("Screen feedback contracts",SW,SH,api=="opengl"?Window::GraphicsApi::OpenGL:
            api=="vulkan"?Window::GraphicsApi::Vulkan:Window::GraphicsApi::Native,false,false);
        std::unique_ptr<IRenderBackend> renderer;
        if(api=="opengl") {
            if(!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress)))throw std::runtime_error("GL loader");
            renderer=engine::render::createLegacyOpenGLBackend();renderer->onResize(SW,SH);
        } else if(api=="vulkan")renderer=std::make_unique<VulkanRenderBackend>(window.getSDLWindow(),SW,SH,false);
        else if(api=="d3d12")renderer=std::make_unique<D3D12RenderBackend>(window.getSDLWindow(),SW,SH,false);
        else throw std::runtime_error("Unknown renderer");
        std::array<ScreenFeedbackUpdate,3> programs{};
        programs[0].count=2;
        programs[0].passes[0]={{0,0,8,6},{0,0,1,1},{1,255,0,0}};
        programs[0].passes[1]={{1,1,5,5},{0,0,1,1},{2,93,0,0}};
        programs[1].count=1;programs[1].passes[0]={{.5f,-.75f,7.5f,6.25f},{.05f,0,.98f,1},{0,201,0,0}};
        programs[2].count=2;programs[2].passes[0]={{0,0,8,6},{0,0,1,1},{0,160,0,0}};
        programs[2].passes[1]={{2.25f,.25f,7.75f,5.75f},{0,0,1,1},{1,220,0,0}};
        ScreenFeedbackFrame frame;frame.sequence=1;frame.width=W;frame.height=H;
        frame.maskKey="feedback-contract-mask";frame.maskRgba=mask.data();frame.maskWidth=4;frame.maskHeight=1;
        // Seed, one update, two batched updates, then two paused presentations.
        for(int i=0;i<5;++i) {
            renderer->beginFrame(i==0?.25f:.8f,i==0?.5f:.1f,i==0?.75f:.02f,1);
            renderer->beginWorldSceneColorPass(SW,SH);
            frame.seed=i==0;frame.updates={};
            if(i==1)frame.updates=std::span(programs).first(1);
            if(i==2)frame.updates=std::span(programs).subspan(1);
            renderer->endWorldSceneColorPass(frame);renderer->endFrame();
            if(!renderer->handlesPresentation())window.swapBuffers();
        }
        renderer->shutdown();renderer.reset();
        int width=0,height=0,channels=0;
        auto* image=stbi_load(argv[2],&width,&height,&channels,3);
        if(!image || width!=SW || height!=SH)throw std::runtime_error("Missing native feedback image");
        std::unique_ptr<unsigned char,decltype(&stbi_image_free)> owned(image,stbi_image_free);
        History history;history.fill(encoded({.25,.5,.75}));
        history=update(history,programs[0]);history=update(history,programs[1]);
        double maximum=0;
        for(int y=0;y<SH;++y)for(int x=0;x<SW;++x) {
            const double px=(std::floor((x+.5)/SW*W*2)+.5)*.5;
            const double py=(std::floor((y+.5)/SH*H*2)+.5)*.5;
            const auto expected=compose(history,programs[2],px,py);
            for(int c=0;c<3;++c)maximum=std::max(maximum,std::abs(image[(y*SW+x)*3+c]-expected[c]));
        }
        if(maximum>2)throw std::runtime_error("Feedback/reference maximum byte error: "+std::to_string(maximum));
        std::cout<<"PASS "<<api<<" RGB8 history, ordered mask/black/alpha, batched updates, pause, non-4:3 output; max error="<<maximum<<'\n';
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
