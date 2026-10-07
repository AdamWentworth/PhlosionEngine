#include "engine/vfx/portable/PortableEffect.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

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
        source.cardSpans[0].first[0].flags|=64;
        bool rejected=false;try{vfx::portable::PortableEffect unsupported(source);}catch(const std::runtime_error&){rejected=true;}
        if(!rejected)throw std::runtime_error("Unsupported projection silently accepted");
        std::cout<<"PASS independent per-vertex colors, alpha, corners, UVs, world transform and unsupported flags\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
