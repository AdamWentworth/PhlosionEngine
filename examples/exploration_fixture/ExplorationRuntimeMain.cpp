#define SDL_MAIN_HANDLED
#include "BattlePreview.h"
#include "ExplorationPresentation.h"
#include "engine/input/SdlInputReplay.h"
#include "engine/core/Environment.h"
#include "engine/platform/Window.h"
#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/OpenGLRenderBackend.h"
#include "engine/render/VulkanRenderBackend.h"
#include <glad/glad.h>
#include <chrono>
#include <iostream>

namespace {
struct Arguments { std::string api="opengl",caseName="walk",replay,handshake,trace; int width=960,height=540,frames=0;bool hidden=false; };
struct Graphics {
    std::unique_ptr<Window> window;
    std::unique_ptr<IRenderBackend> renderer;
};

Graphics createGraphics(const Arguments& arguments) {
    Graphics graphics;
    if (arguments.api == "d3d12") {
#if defined(_WIN32)
        graphics.window = std::make_unique<Window>("Phlosion Exploration",
                                                   arguments.width,
                                                   arguments.height,
                                                   Window::GraphicsApi::Native,
                                                   false,
                                                   !arguments.hidden);
        int width = 0;
        int height = 0;
        graphics.window->getDrawableSize(width, height);
        graphics.renderer = std::make_unique<D3D12RenderBackend>(
            graphics.window->getSDLWindow(), std::max(1, width),
            std::max(1, height), false);
        return graphics;
#else
        throw std::runtime_error("D3D12 is only available on Windows.");
#endif
    }
    if (arguments.api == "vulkan") {
        graphics.window = std::make_unique<Window>("Phlosion Exploration",
                                                   arguments.width,
                                                   arguments.height,
                                                   Window::GraphicsApi::Vulkan,
                                                   false,
                                                   !arguments.hidden);
        int width = 0;
        int height = 0;
        graphics.window->getDrawableSize(width, height);
        graphics.renderer = std::make_unique<VulkanRenderBackend>(
            graphics.window->getSDLWindow(), std::max(1, width),
            std::max(1, height), false);
        return graphics;
    }
    if (arguments.api != "opengl") {
        throw std::runtime_error("unknown exploration backend: " +
                                 arguments.api);
    }
    graphics.window = std::make_unique<Window>("Phlosion Exploration",
                                               arguments.width,
                                               arguments.height,
                                               Window::GraphicsApi::OpenGL,
                                               false,
                                               !arguments.hidden);
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
        throw std::runtime_error(
            "Failed to initialize GLAD for the exploration runtime.");
    }
    graphics.renderer = std::make_unique<OpenGLRenderBackend>();
    return graphics;
}



}
int main(int argc,char** argv) {
    try {
        Arguments a;
        for(int i=1;i<argc;++i) {
            const std::string v=argv[i]; auto value=[&](const char* prefix){return v.substr(std::strlen(prefix));};
            if(v.starts_with("--api="))a.api=value("--api=");
            else if(v.starts_with("--case="))a.caseName=value("--case=");
            else if(v.starts_with("--input-replay="))a.replay=value("--input-replay=");
            else if(v.starts_with("--handshake="))a.handshake=value("--handshake=");
            else if(v.starts_with("--trace="))a.trace=value("--trace=");
            else if(v.starts_with("--width="))a.width=std::stoi(value("--width="));
            else if(v.starts_with("--height="))a.height=std::stoi(value("--height="));
            else if(v.starts_with("--frames="))a.frames=std::stoi(value("--frames="));
            else if(v=="--hidden")a.hidden=true;
            else throw std::runtime_error("Unknown argument: "+v);
        }
        if(a.width<64||a.width>4096||a.height<64||a.height>4096||a.frames<0||a.frames>10000)throw std::runtime_error("Invalid window/frame bounds");
        auto graphics=createGraphics(a); auto& renderer=*graphics.renderer;
        renderer.setVSyncEnabled(!a.hidden);
        if(a.api!=renderer.backendId())throw std::runtime_error("Unexpected renderer fallback");
        int width=a.width,height=a.height;renderer.onResize(width,height);renderer.prewarmWorldRenderAssets();
        {
            std::unique_ptr<engine::input::SdlInputReplay> replay;
            if(!a.replay.empty())replay=std::make_unique<engine::input::SdlInputReplay>(a.replay);
            const bool battleMode = a.caseName == "outskirt-battle";
            if (battleMode && replay) throw std::runtime_error("Battle preview does not accept an input replay");
            std::unique_ptr<engine::exploration_fixture::ExplorationSession> session;
            std::unique_ptr<engine::exploration_fixture::ExplorationPresentation> presentation;
            std::unique_ptr<engine::exploration_fixture::BattlePreview> battle;
            if (battleMode) battle = std::make_unique<engine::exploration_fixture::BattlePreview>();
            else {
                session = std::make_unique<engine::exploration_fixture::ExplorationSession>(a.caseName,replay!=nullptr,engine::exploration_fixture::configuredCameraSettings());
                session->setTrace(a.trace);
                presentation = std::make_unique<engine::exploration_fixture::ExplorationPresentation>(a.caseName == "outskirt");
            }
            engine::editor::EditorSdlGameInputContext context;context.windowId=SDL_GetWindowID(graphics.window->getSDLWindow());
            context.windowFocused=(SDL_GetWindowFlags(graphics.window->getSDLWindow())&SDL_WINDOW_INPUT_FOCUS)!=0;
            context.activeGameViewport=true;context.viewportFocused=true;context.viewportHovered=true;context.playing=true;
            bool running=true;int frame=0,battleTicks=0;float accumulator=0;auto previous=std::chrono::steady_clock::now();
            const int capture=std::stoi(engine::env::get("PHLOSION_BACKEND_SCREENSHOT_FRAME").value_or("160"));
            while(running && (a.frames==0||frame<a.frames)) {
                ++frame; const auto now=std::chrono::steady_clock::now();
                const float delta=(replay || (battle && a.hidden))?engine::exploration_fixture::fixedDelta:std::min(.05f,std::chrono::duration<float>(now-previous).count());previous=now;
                context.viewport={0,0,width,height}; if(replay)replay->frame(context);if(session)session->begin(context);
                SDL_Event e{};while(SDL_PollEvent(&e)) {
                    if(e.type==SDL_QUIT)running=false;
                    if(e.type==SDL_WINDOWEVENT && e.window.windowID==context.windowId) {
                        if(e.window.event==SDL_WINDOWEVENT_FOCUS_LOST)context.windowFocused=false;
                        if(e.window.event==SDL_WINDOWEVENT_FOCUS_GAINED)context.windowFocused=true;
                        if(e.window.event==SDL_WINDOWEVENT_SIZE_CHANGED) {graphics.window->getDrawableSize(width,height);width=std::max(1,width);height=std::max(1,height);renderer.onResize(width,height);}
                    }
                    if(e.type==SDL_KEYDOWN && !e.key.repeat && e.key.windowID==context.windowId) {
                        if(e.key.keysym.sym==SDLK_ESCAPE) {
                            if(battle){if(context.windowFocused)battle->cancel();}else running=false;
                        }
                        if(battle&&context.windowFocused&&e.key.repeat==0&&e.key.keysym.sym==SDLK_SPACE)battle->skip();
                        if(battle&&context.windowFocused&&e.key.repeat==0&&e.key.keysym.sym==SDLK_r){battle->reset();battleTicks=0;}
                        if(!replay&&e.key.keysym.sym==SDLK_p)context.paused=!context.paused;
                    }
                    if(battle&&context.windowFocused&&e.type==SDL_CONTROLLERBUTTONDOWN) {
                        if(e.cbutton.button==SDL_CONTROLLER_BUTTON_B)battle->cancel();
                        if(e.cbutton.button==SDL_CONTROLLER_BUTTON_A)battle->skip();
                    }
                    if(session)session->event(context,e);
                }
                if(session)session->end(context);
                if(context.playing&&!context.paused) {
                    accumulator+=delta;int ticks=0;while(accumulator>=engine::exploration_fixture::fixedDelta&&ticks<4) {
                        if(battle)++battleTicks;else session->tick();
                        accumulator-=engine::exploration_fixture::fixedDelta;++ticks;
                    }
                    if(ticks==4)accumulator=0;
                }
                renderer.beginFrame(.035f,.05f,.085f,1);renderer.beginWorldSceneColorPass(width,height);
                if(battle)battle->draw(renderer,battleTicks,width,height);
                else presentation->draw(renderer,*session,width,height);
                renderer.endWorldSceneColorPass();
                if(battle)battle->drawOverlay(renderer,width,height);
                renderer.endFrame();
                if(frame==capture) {
                    if(battle)engine::exploration_fixture::writeBattleHandshake(a.handshake,"runtime",a.api,*battle,renderer,battleTicks,width,height,frame,{0,0,width,height});
                    else engine::exploration_fixture::writeHandshake(a.handshake,"runtime",a.api,*session,*presentation,renderer,width,height,frame);
                }
                if(!renderer.handlesPresentation())graphics.window->swapBuffers();
                if(!replay&&!battle&&frame%30==0) {
                    const std::string title="Exploration | WASD / left stick | Shift / L3 run | RMB / right stick orbit | wheel / bumpers zoom | R / Back reset | Tab / Start select pad | P pause";
                    graphics.window->setTitle(title+(session->sensorActive()?" | Sensor active":""));
                }
            }
            if(battle)battle->release();else presentation->release();
        }
        renderer.shutdown();graphics.renderer.reset();graphics.window.reset();SDL_Quit();return 0;
    }catch(const std::exception& e){std::cerr<<"[Exploration] FAILED "<<e.what()<<'\n';SDL_Quit();return 1;}
}
