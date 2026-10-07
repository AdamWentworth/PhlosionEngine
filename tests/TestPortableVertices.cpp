#include "engine/vfx/portable/PortableEffect.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

struct Recorder final : IRenderBackend {
    int uploads=0,draws=0; bool preparedRepeat=false,drawnRepeat=false;
    const char* backendId() const override{return "recording";}
    void beginFrame(float,float,float,float) override{} void endFrame() override{}
    void onResize(int,int) override{} void shutdown() override{}
    bool requiresOpenGLContext() const override{return false;}
    bool handlesPresentation() const override{return false;}
    void prewarmWorldTextureData(const WorldTextureData* texture) override {
        if(texture->cacheKey)throw std::runtime_error("Preparation must retain backend sampler-aware texture identity");
        ++uploads; preparedRepeat|=texture->wrapS==10497 && texture->wrapT==33648;
    }
    void drawWorldIndexedMeshTextured(const WorldMeshVertex*,std::size_t,const std::uint32_t*,std::size_t,
        const WorldTextureData* texture,const float*,int,int) override {
        if(texture->cacheKey)throw std::runtime_error("Draw must use the same sampler-aware texture identity");
        ++draws; drawnRepeat|=texture->wrapS==10497 && texture->wrapT==33648;
    }
};

int main() {
    try {
        vfxlibrary::Effect source;source.durationMs=100;
        vfxlibrary::CardStyle style;style.shader="rgba";style.blend="alpha_blend";
        style.wrapU=style.wrapV="clamp";style.tint={.5f,.75f,1,.5f};style.opacity=.5f;
        source.cardStyles.push_back(style);
        vfxlibrary::CardParameters card;card.flags=1|8|16|32;
        card.values[0]=.8f;card.values[1]=card.values[2]=1;
        for(std::size_t i=0;i<4;++i) {
            card.values[18+i*4]=float(i+1)*.2f;card.values[19+i*4]=.4f;
            card.values[20+i*4]=.6f;card.values[21+i*4]=float(i+1)*.1f;
            card.values[34+i*3]=float(i);card.values[35+i*3]=float(i*i);card.values[36+i*3]=-float(i);
            card.values[46+i*2]=float(i)*.1f;card.values[47+i*2]=float(i)*.2f;
        }
        vfxlibrary::CardSpan span;span.startMs=0;span.endMs=100;
        span.packets.push_back({0,0,1});span.first={card};span.last={card};
        source.cardSpans.push_back(span);
        vfx::portable::PortableEffect effect(source);
        const auto draws=effect.sample(50,glm::translate(glm::mat4(1),glm::vec3(10,20,30)),{1,0,0},{0,1,0});
        if(draws.size()!=1 || draws[0].vertices.size()!=4)throw std::runtime_error("Missing explicit card");
        auto near=[](float got,float expected){if(std::abs(got-expected)>1e-6f)throw std::runtime_error("Per-vertex contract mismatch");};
        for(std::size_t i=0;i<4;++i) {
            const auto& v=draws[0].vertices[i];
            near(v.x,10+float(i));near(v.y,20+float(i*i));near(v.z,30-float(i));
            near(v.u,float(i)*.1f);near(v.v,float(i)*.2f);
            near(v.r,float(i+1)*.1f);near(v.g,.3f);near(v.b,.6f);near(v.a,float(i+1)*.02f);
        }
        if(!effect.sample(-1,glm::mat4(1),{1,0,0},{0,1,0}).empty() ||
           !effect.sample(100,glm::mat4(1),{1,0,0},{0,1,0}).empty())throw std::runtime_error("Card escaped lifetime");
        source.cardStyles[0].wrapU="repeat"; source.cardStyles[0].wrapV="mirror";
        vfx::portable::PortableEffect repeated(source);
        Recorder renderer; repeated.prewarm(renderer);
        if(renderer.uploads==0 || renderer.draws!=0 || !renderer.preparedRepeat)
            throw std::runtime_error("Preparation lost the authored sampler or drew prematurely");
        const glm::mat4 identity(1);
        repeated.render(renderer,50,identity,{1,0,0},{0,1,0},&identity[0][0],640,360);
        if(renderer.draws!=1 || !renderer.drawnRepeat)throw std::runtime_error("Prepared sampler changed on draw");
        source.cardSpans[0].first[0].flags|=64;
        bool rejected=false;try{vfx::portable::PortableEffect unsupported(source);}catch(const std::runtime_error&){rejected=true;}
        if(!rejected)throw std::runtime_error("Unsupported projection silently accepted");
        std::cout<<"PASS independent per-vertex colors, alpha, corners, UVs, world transform and unsupported flags\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
