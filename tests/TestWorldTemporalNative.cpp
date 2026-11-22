#include "engine/render/LegacyOpenGL.h"
#define SDL_MAIN_HANDLED
#include "engine/platform/Window.h"
#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/VulkanRenderBackend.h"
#include <SDL.h>
#include <stb_image.h>
#include <cmath>
#include <iostream>
#include <memory>
using namespace engine::render;
namespace {
void check(bool b,const char* s){if(!b)throw std::runtime_error(s);}
}
int main(int argc,char** argv)try {
    std::cout<<std::unitbuf;check(argc==3,"Expected renderer and display image path");
    const std::string api=argv[1];check(api=="d3d12"||api=="vulkan","Invalid API");
    SDL_SetMainReady();
#if defined(_MSC_VER)
    _putenv_s("PHLOSION_BACKEND_SCREENSHOT_PATH",argv[2]);
    _putenv_s("PHLOSION_BACKEND_SCREENSHOT_FRAME","2");
    _putenv_s("PHLOSION_BACKEND_SCREENSHOT_DEFER","0");
#else
    SDL_setenv("PHLOSION_BACKEND_SCREENSHOT_PATH",argv[2],1);
    SDL_setenv("PHLOSION_BACKEND_SCREENSHOT_FRAME","2",1);
    SDL_setenv("PHLOSION_BACKEND_SCREENSHOT_DEFER","0",1);
#endif
    struct Quit{~Quit(){SDL_Quit();}}quit;
    int width=320,height=240;
    Window window("Temporal reconstruction",width,height,api=="vulkan"?Window::GraphicsApi::Vulkan:Window::GraphicsApi::Native,false,false);
    std::unique_ptr<IRenderBackend> renderer;
    if(api=="vulkan")renderer=std::make_unique<VulkanRenderBackend>(window.getSDLWindow(),width,height,false);
    else renderer=std::make_unique<D3D12RenderBackend>(window.getSDLWindow(),width,height,false);
    auto& surface=dynamic_cast<IWorldSurfaceBuffer&>(*renderer);
    constexpr std::array<std::uint32_t,6> indices{0,2,1,0,3,2};
    std::array<IRenderBackend::WorldMeshVertex,4> vertices{};
    for(int i=0;i<4;++i){auto& v=vertices[i];v.x=i==0||i==3?-1.f:1.f;v.y=i<2?-1.f:1.f;
        v.u=i==0||i==3?0.f:1.f;v.v=i<2?0.f:1.f;v.r=v.g=v.b=v.a=1;v.nz=v.tx=v.tw=v.weight0=1;}

    const auto frame=[&](WorldTemporalView view,std::uint32_t id,int pattern,bool reset=false,
                         float transparent=0,float previousZ=0,float colorScale=1,bool valid=true,float currentZ=0,bool additive=false,int copies=1) {
        view.width=width;view.height=height;view.reset=reset;view.deltaSeconds=1.f/60.f;
        constexpr std::array<unsigned char,4> pixels{255,255,255,255};
        IRenderBackend::WorldTextureData material;material.key=material.cacheKey="temporal-white";
        material.rgba=pixels.data();material.width=material.height=1;material.materialMode=2;material.textureSrgb=0;material.modelMatrix[14]=currentZ;
        for(auto& vertex:vertices)vertex.r=vertex.g=vertex.b=colorScale;
        WorldMotionPose previous;previous.model=previous.instance=worldMotionIdentity;previous.model[14]=previousZ;
        WorldMotionSample sample{&previous,id,valid};
        renderer->beginFrame(0,0,0,0);renderer->beginWorldSceneColorPass(width,height);
        check(surface.beginWorldSurfaceBuffer(worldMotionIdentity.data(),width,height),"surface begin");
        check(surface.setWorldSurfaceMotion(worldMotionIdentity,{&sample,1}),"motion set");
        renderer->drawWorldIndexedMeshTextured(vertices.data(),4,indices.data(),6,&material,worldMotionIdentity.data(),width,height);
        check(surface.endWorldSurfaceBuffer(),"surface end");
        check(surface.beginWorldTemporalScene(view),"temporal begin");
        const auto feedback=surface.worldTemporalFeedback();
        if(feedback.valid){check(feedback.frame+1==view.frame&&feedback.view==view.identity,"feedback frame ownership");
            check(feedback.width==std::max(1,width/2)&&feedback.height==std::max(1,height/2)&&feedback.mips>1,"feedback extent/mips");}
        // Pixel-wide geometry gives controlled spatial contrast without texture
        // minification/mip filtering masking a reconstruction failure.
        std::vector<IRenderBackend::WorldMeshVertex> stripes;
        std::vector<std::uint32_t> stripeIndices;
        for(int x=0;x<width;++x) {
            const auto base=std::uint32_t(stripes.size());
            for(int i=0;i<4;++i) {
                auto vertex=vertices[i];vertex.x=2.f*(x+(i==1||i==2?1:0))/width-1.f;
                vertex.r=vertex.g=vertex.b=pattern==3?((x/2)%2?.8f:0.f):pattern==2?colorScale:((x+pattern)%2?colorScale:0);
                stripes.push_back(vertex);
            }
            for(auto i:indices)stripeIndices.push_back(base+i);
        }
        material.materialMode=255;
        renderer->drawWorldIndexedMeshTextured(stripes.data(),stripes.size(),stripeIndices.data(),stripeIndices.size(),&material,worldMotionIdentity.data(),width,height);

        if(transparent>0) {
            for(auto& vertex:vertices){vertex.r=1;vertex.g=vertex.b=0;vertex.a=transparent;vertex.z=-.1f;}
            material.alphaMode=2;material.blendMode=additive?3:0;
            for(int i=0;i<copies;++i)
                renderer->drawWorldIndexedMeshTextured(vertices.data(),4,indices.data(),6,&material,worldMotionIdentity.data(),width,height);
            for(auto& vertex:vertices){vertex.a=1;vertex.z=0;}
        }
        check(surface.resolveWorldTemporalScene(),"temporal resolve");
        check(!surface.resolveWorldTemporalScene(),"duplicate resolve accepted");
        check(surface.captureWorldTemporalScene(),"temporal readback");
        renderer->endWorldSceneColorPass();renderer->endFrame();
        if(!renderer->handlesPresentation())window.swapBuffers();
        auto image=surface.readWorldTemporalScene();
        check(image.width==width&&image.height==height&&image.color.size()==std::size_t(width*height),"readback extent");
        check(surface.readWorldTemporalScene().color.empty(),"readback not consumed");
        for(const auto& px:image.color)for(float channel:px)check(std::isfinite(channel)&&channel>=0,"invalid reconstructed radiance");
        std::cout<<"view "<<view.identity<<" frame "<<view.frame<<" center "<<image.color[(height/2)*width+width/2][0]
            <<" mask "<<image.mask[(image.maskHeight/2)*image.maskWidth+image.maskWidth/2][2]
            <<" feedback "<<image.feedback[(image.feedbackHeight/2)*image.feedbackWidth+image.feedbackWidth/2][0]<<'\n';
        return image;
    };
    WorldTemporalView view{1,1,1,width,height};
    auto first=frame(view,0xfedcba98u,0,false,0,0,1,false);
    check(first.historyTexelBytes==2662312,"temporal history compact storage budget");
    std::cout<<"history payload "<<first.historyTexelBytes<<" bytes\n";
    ++view.frame;auto accumulated=frame(view,0xfedcba98u,1);
    const auto center=[&](const WorldTemporalImage& i){return i.color[(height/2)*width+width/2];};
    const auto mask=[&](const WorldTemporalImage& i){return i.mask[(i.maskHeight/2)*i.maskWidth+i.maskWidth/2][2];};
    check(mask(first)>.99f&&mask(accumulated)<.01f,"history invalidation");
    const float old=center(first)[0],now=1-old,resolved=center(accumulated)[0];
    check(std::abs(resolved-now)>.1f,"history was not reconstructed");
    ++view.frame;auto cut=frame(view,0xfedcba98u,0,true);
    check(mask(cut)>.99f&&std::abs(center(cut)[0]-old)<.01f,"cut ghost");
    ++view.frame;auto replaced=frame(view,0xabcdef12u,1);
    check(mask(replaced)>.99f&&std::abs(center(replaced)[0]-now)<.01f,"identity reuse ghost");
    ++view.frame;auto depth=frame(view,0xabcdef12u,0,false,0,.2f);
    check(mask(depth)>.99f,"disoccluded depth accepted");
    view.frame+=2;auto skipped=frame(view,0xabcdef12u,1);
    check(mask(skipped)>.99f,"skipped view reused");
    WorldTemporalView other{2,1,1,width,height};
    auto isolated=frame(other,0xabcdef12u,2,false,0,0,.25f);
    check(mask(isolated)>.99f,"independent view borrowed history");
    other.frame=2;other.preExposure=2;
    auto exposure=frame(other,0xabcdef12u,2,false,0,0,.5f);
    check(std::abs(center(exposure)[0]-.5f)<.02f,"pre-exposure compensation");
    ++view.frame;auto blended=frame(view,0xabcdef12u,2,false,.75f);
    check(center(blended)[3]>.9f&&center(blended)[1]<.3f,"transparent coverage not reactive");
    ++view.frame;auto removed=frame(view,0xabcdef12u,2);
    check(std::abs(center(removed)[1]-1)<.01f,"removed transparency ghost");
    for(const auto& p:removed.feedback)check(std::abs(p[0]-1)<.01&&std::abs(p[1]-1)<.01,"feedback filter radiance");
    ++view.frame;auto additive=frame(view,0xabcdef12u,2,false,.25f,0,1,true,0,true);
    check(std::abs(center(additive)[0]-2)<.02f&&std::abs(center(additive)[1]-1)<.02f,"premultiplied additive energy changed");
    check(std::abs(center(additive)[3]-.5f)<.02f,"premultiplied additive mask lost coverage");
    WorldTemporalView precision{3,1,1,width,height};
    frame(precision,0x87654321u,2,false,0,.4f,1,false,.4f);
    ++precision.frame;
    auto precise=frame(precision,0x87654321u,2,false,0,.4f,1,true,.4f);
    check(mask(precise)<.01f,"half-depth quantization rejected stationary surface");
    auto hdrFeedback=frame({4,1,1,width,height},0x1234abcd,3);
    // Five bilinear inputs are {0,.4,.4,.4,.4}. Compressing by 1+red,
    // averaging, then decoding gives 8/27, not the linear mean .32.
    const auto& filtered=hdrFeedback.feedback[(hdrFeedback.feedbackHeight/2)*hdrFeedback.feedbackWidth+hdrFeedback.feedbackWidth/2];
    // Compact unsigned-float UAV stores may truncate: one representable step
    // at 8/27 is 1/256 for R/G and 1/128 for B. A linear mean (.32)
    // fails either bound; this still distinguishes the nonlinear filter.
    for(int channel=0;channel<3;++channel)
        check(std::abs(filtered[channel]-8.f/27.f)<(channel==2?1.f/128:1.f/256),
            "HDR feedback compression/filter changed beyond storage precision");
    // Vertex colours are UNORM. Additive HDR draws exercise radiance above one.
    auto bright=frame({4,1,2,width,height},0x1234abcd,2,true,.25f,0,1,true,0,true,31);
    for(const auto& pixel:bright.feedback)for(int c=0;c<3;++c)
        check(std::isfinite(pixel[c])&&std::abs(pixel[c]-(c==0?32.f:1.f))<(c==0?.51f:.02f),"compact feedback lost HDR range");
    // Odd dimensions exercise footprint coverage and last mip boundaries.
    width=321;height=243;SDL_SetWindowSize(window.getSDLWindow(),width,height);renderer->onResize(width,height);
    ++view.frame;auto resized=frame(view,0xabcdef12u,2);
    check(mask(resized)>.99f,"resized history reused");
    check(resized.feedbackWidth==160&&resized.feedbackHeight==121,"odd feedback extent");
    surface.releaseWorldTemporalView(1);
    view.frame=1;
    auto recreated=frame(view,0xabcdef12u,2);
    check(mask(recreated)>.99f,"retired scene history reused");
    width=1920;height=1080;SDL_SetWindowSize(window.getSDLWindow(),width,height);renderer->onResize(width,height);
    ++view.frame;auto full=frame(view,0xabcdef12u,2);
    check(full.historyTexelBytes==71883648,"1080p temporal history storage budget");
    std::cout<<"1080p history payload "<<full.historyTexelBytes<<" bytes\n";
    renderer->shutdown();renderer.reset();
    int w=0,h=0,channels=0;
    std::unique_ptr<unsigned char,decltype(&stbi_image_free)> displayed(stbi_load(argv[2],&w,&h,&channels,3),stbi_image_free);
    check(displayed&&w==320&&h==240,"Missing reconstructed display image");
    const float linear=accumulated.color[120*320+160][0];
    const float encoded=255*(linear<=.0031308f?linear*12.92f:1.055f*std::pow(linear,1.f/2.4f)-.055f);
    const float displayError=std::abs(displayed.get()[(120*320+160)*3]-encoded);
    std::cout<<"reconstructed display error "<<displayError<<"/255\\n";
    check(displayError<2,"Display bypassed reconstruction");
    std::cout<<api<<" native temporal PASS\\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
