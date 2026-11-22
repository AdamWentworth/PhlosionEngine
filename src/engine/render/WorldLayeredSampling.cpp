#include "WorldLayeredSampling.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>
namespace engine::render {
namespace {
float unit(float x){return std::clamp(x,0.f,1.f);}
float linear(float x){return x<=.04045f?x/12.92f:std::pow((x+.055f)/1.055f,2.4f);}
glm::vec3 normal(glm::vec3 n){return glm::dot(n,n)>1e-20f?glm::normalize(n):glm::vec3(0,0,1);}
glm::vec4 sample(const WorldLayerMaterialSet& set,unsigned index,glm::vec2 uv,float level){
    const auto& texture=set.textures.at(index);level=std::clamp(level,0.f,float(texture.mips.size()-1));
    const auto at=[&](unsigned mip){
        const auto& image=texture.mips[mip];
        const glm::vec2 p=glm::fract(uv)*glm::vec2(image.width,image.height)-.5f;
        const glm::ivec2 origin(glm::floor(p));const glm::vec2 weight=glm::fract(p);
        const auto texel=[&](int x,int y){
            x=(x%int(image.width)+int(image.width))%int(image.width);y=(y%int(image.height)+int(image.height))%int(image.height);
            const auto offset=(std::size_t(y)*image.width+unsigned(x))*4;
            glm::vec4 value;for(unsigned c=0;c<4;++c){value[c]=image.rgba[offset+c]/255.f;if(texture.srgb&&c<3)value[c]=linear(value[c]);}
            return value;
        };
        return glm::mix(glm::mix(texel(origin.x,origin.y),texel(origin.x+1,origin.y),weight.x),
            glm::mix(texel(origin.x,origin.y+1),texel(origin.x+1,origin.y+1),weight.x),weight.y);
    };
    const unsigned lower=unsigned(level),upper=std::min(lower+1,unsigned(texture.mips.size()-1));
    return glm::mix(at(lower),at(upper),level-float(lower));
}
glm::vec3 reconstruct(glm::vec2 n){return {n,std::sqrt(std::max(0.f,1.f-glm::dot(n,n)))};}
glm::vec3 detail(glm::vec3 a,glm::vec3 b){
    const glm::vec3 t(a.x,a.y,a.z+1),u(-b.x,-b.y,b.z);
    return normal(t*glm::dot(t,u)-u*t.z);
}
float remap(float value,const std::array<float,4>& map){return unit(unit(value*map[0]+map[1])*map[2]+map[3]);}
glm::vec3 weights(glm::vec3 n){const auto square=n*n;auto w=square*square;return w/std::max(w.x+w.y+w.z,1e-20f);}
glm::vec2 planar(glm::vec3 p,unsigned axis){
    if(axis==1)return glm::vec2(-p.x,p.z)/512.f;
    if(axis==2)return glm::vec2(p.y,-p.x)/512.f;
    return glm::vec2(p.y,p.z)/512.f;
}
}
WorldLayerSurface sampleWorldLayerMaterial(const WorldLayerMaterialSet& set,std::uint32_t index,
    glm::vec2 inputUv,glm::vec3 position,glm::vec3 sourceNormal,glm::vec4 tangent,float mip){
    if(!index||index>set.materials.size()||!std::isfinite(mip))throw std::invalid_argument("Layer sample index or mip");
    const auto& m=set.materials[index-1];
    for(float x:{inputUv.x,inputUv.y,position.x,position.y,position.z,sourceNormal.x,sourceNormal.y,sourceNormal.z,tangent.x,tangent.y,tangent.z,tangent.w})
        if(!std::isfinite(x))throw std::invalid_argument("Nonfinite layer sample");
    WorldLayerSurface out;glm::vec2 maskUv=inputUv*glm::vec2(m.uvScale[0],m.uvScale[1]),uv=maskUv;
    auto global=normal(sourceNormal);glm::vec3 globalDetail(0,0,1);
    if(m.globalNormal!=kWorldLayerNoTexture){
        const auto mapUv=maskUv*glm::vec2(m.globalNormalUvScale[0],m.globalNormalUvScale[1])+glm::vec2(m.globalNormalUvBias[0],m.globalNormalUvBias[1]);
        const auto decoded=reconstruct(glm::vec2(sample(set,m.globalNormal,mapUv,mip))*2.f-glm::vec2(1));
        if(m.terrain)global=normal({decoded.x,decoded.z,-decoded.y});
        else globalDetail=normal(glm::mix(glm::vec3(0,0,1),decoded,m.globalNormalIntensity));
    }
    if(m.terrain){
        const glm::vec3 source(position.x,-position.z,position.y),gn(global.x,-global.z,global.y);
        const auto w=weights(gn);const float largest=std::max({w.x,w.y,w.z});
        const unsigned axis=w.y==largest?1u:w.z==largest?2u:0u;
        unsigned tile=0;if(m.tilingMap!=kWorldLayerNoTexture)tile=std::min(7u,unsigned(sample(set,m.tilingMap,maskUv,0).x*255.f+.5f));
        uv=planar(source,axis)*m.tiling[tile];
    }
    uv=glm::fract(uv)*glm::vec2(m.surfaceAspect,1);
    std::uint32_t setup=0;for(unsigned i=0;i<m.layers.size();++i)if(m.layers[i].tintOpacity[3]>0)setup|=1u<<i;
    const auto masks=selectWorldLayerMasks(m.masks,setup,{maskUv.x,maskUv.y});
    auto active=masks.active;const bool multiple=std::popcount(active)>1;
    float remaining=1,borderCoverage=0,borderWeight=0,roughness=0,metalness=0;
    glm::vec2 normalXY(0),borderXY(0);glm::vec3 color(0);
    for(int priority=31;priority>=0&&remaining>0;--priority){
        const auto bit=1u<<unsigned(priority);if(!(active&bit))continue;
        const bool last=(active&(active-1u))==0;active^=bit;const auto& l=m.layers[priority];
        float coverage=1;
        if(!last){
            const auto location=worldLayerMaskUv(m.masks,worldLayerMaskRecord(m.masks,masks,unsigned(priority)),masks.flippedUv);
            coverage=sample(set,m.maskTexture,{location[0],location[1]},0).x;
        }
        if(coverage<=0)continue;
        const glm::vec2 layerUv=uv*l.detail[0]+glm::vec2(l.layerOffsetNormal[0],l.layerOffsetNormal[1]);
        const glm::vec2 microUv=uv*l.detail[1]+glm::vec2(l.microOffset[0],l.microOffset[1]);
        const auto mb=sample(set,l.textures[0],microUv,mip);
        const float shaped=unit(glm::mix(1.f-mb.w,coverage,l.detail[2]));
        const float edge=std::sqrt(std::max(0.f,1.f-std::abs(2*shaped-1)))*l.tintOpacity[3];
        const float border=unit(edge-borderCoverage),amount=shaped*l.tintOpacity[3];borderCoverage+=amount;
        borderXY=glm::mix(borderXY,(glm::vec2(mb)*2.f-glm::vec2(1))*l.detail[3],border);
        borderWeight=std::max(borderWeight,border*std::abs(l.detail[3]));
        const float contribution=std::min(remaining,amount);remaining-=contribution;
        const float rough=sample(set,l.textures[2]&65535u,layerUv,mip).x;
        color+=glm::vec3(sample(set,l.textures[1]&65535u,layerUv,mip))*
            glm::mix(glm::vec3(1),glm::vec3(l.tintOpacity[0],l.tintOpacity[1],l.tintOpacity[2]),remap(rough,l.colorMaskRemap))*contribution;
        roughness+=remap(rough,l.roughnessRemap)*contribution;
        metalness+=remap(sample(set,l.textures[2]>>16u,layerUv,mip).x,l.metalnessRemap)*contribution;
        normalXY+=(glm::vec2(sample(set,l.textures[1]>>16u,layerUv,mip))*2.f-glm::vec2(1))*(l.layerOffsetNormal[2]*contribution);
        if(contribution>out.dominantWeight){out.dominantLayer=unsigned(priority);out.dominantWeight=contribution;}
        if(unsigned(priority)>=m.selectedLayers[0]&&unsigned(priority)<=m.selectedLayers[1])out.selectedWeight+=contribution;
    }
    auto local=reconstruct(normalXY);if(multiple)local=glm::mix(local,reconstruct(borderXY),borderWeight);
    local=detail(globalDetail,local);out.color=color;out.roughness=roughness;out.metalness=m.terrain?0:metalness;out.specular=m.terrain?.04f*metalness:.04f;
    if(m.terrain){
        if(m.macroColor!=kWorldLayerNoTexture)out.color=glm::clamp(out.color*glm::vec3(sample(set,m.macroColor,maskUv,mip))*2.f,glm::vec3(0),glm::vec3(1));
        const auto d=normal(glm::vec3(glm::vec2(local)*m.normalStrength,local.z));const float h=std::abs(d.z);
        const glm::vec3 n(global.x,-global.z,global.y),w=weights(n);
        const glm::vec3 nx(h*n.x,d.y+n.y,-d.x+n.z),ny(-d.x+n.x,h*n.y,-d.y+n.z),nz(d.y+n.x,d.x+n.y,h*n.z);
        const auto result=normal(nx*w.x+ny*w.y+nz*w.z);out.normal={result.x,result.z,-result.y};
    }else{
        auto tx=glm::vec3(tangent)-glm::dot(glm::vec3(tangent),global)*global;
        if(glm::dot(tx,tx)<1e-10f)tx=glm::cross(std::abs(global.y)<.99f?glm::vec3(0,1,0):glm::vec3(0,0,1),global);
        tx=normal(tx);const auto ty=glm::cross(global,tx)*(tangent.w<0?-1.f:1.f);
        out.normal=normal(tx*local.x+ty*(local.y*m.normalYSign)+global*local.z);
    }
    return out;
}
}
