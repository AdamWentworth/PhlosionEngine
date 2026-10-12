#include "engine/render/RainVolume.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace engine::render {
namespace {
std::uint32_t hash(std::uint32_t n) {
    n ^= n >> 16; n *= 2146121005u; n ^= n >> 15; n *= 2221713035u;
    return n ^ (n >> 16);
}
float random(std::uint32_t n) { return static_cast<float>(hash(n)&65535u)/65536.f; }
bool finite(glm::vec3 p) { return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z); }
std::uint32_t cellSeed(int x,int y,int z,std::uint32_t seed) {
    return hash((static_cast<std::uint32_t>(x)&255u)*73856093u ^
        (static_cast<std::uint32_t>(y)&255u)*19349663u ^
        (static_cast<std::uint32_t>(z)&255u)*83492791u ^ seed);
}
float fade(glm::vec3 delta,const RainVisualSettings& s) {
    return std::clamp((s.horizontalRange-glm::length(glm::vec2(delta.x,delta.z)))/s.horizontalFade,0.f,1.f) *
        std::clamp((s.verticalRange-std::abs(delta.y))/s.verticalFade,0.f,1.f) *
        std::clamp((glm::length(delta)-s.nearDistance)/s.nearFade,0.f,1.f);
}
void scopeCheck(bool ok) { if (!ok) throw std::runtime_error("Rain resource scope admission failed"); }
float smooth(float low,float high,float value) {
    const float t=std::clamp((value-low)/(high-low),0.f,1.f);return t*t*(3-2*t);
}
}
std::string_view invalidRainSetting(const RainVisualSettings& s) {
    for (const auto& field : rainSettingFields) {
        const float value=s.*field.member;
        if (!std::isfinite(value) || value<field.minimum || value>field.maximum) return field.name;
    }
    if (s.horizontalFade>s.horizontalRange) return "horizontal_fade";
    if (s.verticalFade>s.verticalRange) return "vertical_fade";
    if (s.brightnessMin>s.brightnessMax) return "brightness_min";
    // Retain vertical reach for the fixed cover/surface rays at the input wind cap.
    if (s.fallSpeed<s.windResponse*72.f) return "wind_response";
    return {};
}
std::string_view invalidRainBand(const RainBandSettings& s,std::size_t band) {
    for (const auto& field : rainBandFields) {
        const float v=s.*field.member;
        if (!std::isfinite(v)||v<field.minimum||v>field.maximum) return field.name;
    }
    if (band>=3||s.farDistance>s.cellSize*4||s.farFade>s.farDistance||
        s.nearDistance+s.nearFade>=s.farDistance) return "far_distance";
    if (s.cellSize!=2&&s.cellSize!=4&&s.cellSize!=8) return "cell_size";
    // Quarter multiples preserve the 8,192 m integrated-motion repetition.
    if (std::round(s.speed*4)!=s.speed*4) return "speed";
    return {};
}
void RainVolume::setStreakAtlas(std::string key,int tileWidth,int height,std::span<const unsigned char> rgba) {
    if (key.empty()||tileWidth<4||tileWidth>256||height<4||height>512||
        rgba.size()!=static_cast<std::size_t>(tileWidth)*3*height*4)
        throw std::runtime_error("Invalid rain streak atlas");
    atlasKey_=std::move(key);tileWidth_=tileWidth;atlasHeight_=height;
    atlasPixels_.assign(rgba.begin(),rgba.end());
}
void RainVolume::setImpactAtlas(std::string key,int tileWidth,int height,std::span<const unsigned char> rgba) {
    if (key.empty()||tileWidth<4||tileWidth>256||height<4||height>256||
        rgba.size()!=static_cast<std::size_t>(tileWidth)*8*height*4)
        throw std::runtime_error("Invalid rain impact atlas");
    impactAtlasKey_=std::move(key);impactTileWidth_=tileWidth;impactAtlasHeight_=height;
    impactAtlasPixels_.assign(rgba.begin(),rgba.end());contacts_={};contactSeconds_=-1;
}
RainVolume::RainVolume() {
    drops_.reserve(maxDrops*4);impacts_.reserve(maxImpactQuads*4);indices_.reserve(maxDrops*6);
    visibleImpacts_.reserve(maxImpacts);
    for (std::uint32_t i=0;i<maxDrops;++i)
        for (auto corner : {0u,1u,2u,0u,2u,3u}) indices_.push_back(i*4+corner);
    for (int y=0;y<64;++y) for (int x=0;x<32;++x) {
        const float u=(x+.5f)/32.f,v=(y+.5f)/64.f;
        const float alpha=std::exp(-std::pow((u-.5f)*6.f,2.f))*std::sin(v*3.14159265f);
        const auto offset=static_cast<std::size_t>(y*32+x)*4;
        streakPixels_[offset]=streakPixels_[offset+1]=streakPixels_[offset+2]=255;
        streakPixels_[offset+3]=static_cast<unsigned char>(std::lround(alpha*255));
    }
    for (int y=0;y<32;++y) for (int x=0;x<32;++x) {
        const float radius=std::hypot((x+.5f)/16.f-1,(y+.5f)/16.f-1);
        const float alpha=std::exp(-std::pow((radius-.72f)*12.f,2.f));
        const auto offset=static_cast<std::size_t>(y*32+x)*4;
        impactPixels_[offset]=impactPixels_[offset+1]=impactPixels_[offset+2]=255;
        impactPixels_[offset+3]=static_cast<unsigned char>(std::lround(alpha*255));
    }
}
IRenderBackend::WorldTextureData RainVolume::texture(bool impact,bool layered) const {
    IRenderBackend::WorldTextureData t;
    t.key=t.cacheKey=impact?"phlosion-rain-impact-v1":"phlosion-rain-streak-v1";
    t.rgba=impact?impactPixels_.data():streakPixels_.data();t.width=32;t.height=impact?32:64;
    if (!impact&&layered) { t.key=t.cacheKey=atlasKey_.c_str();t.rgba=atlasPixels_.data();t.width=tileWidth_*3;t.height=atlasHeight_; }
    if (impact&&layered&&!impactAtlasPixels_.empty()) {
        t.key=t.cacheKey=impactAtlasKey_.c_str();t.rgba=impactAtlasPixels_.data();t.width=impactTileWidth_*8;t.height=impactAtlasHeight_;
    }
    t.wrapS=t.wrapT=33071;t.alphaMode=2;t.materialMode=255;t.textureSrgb=0;
    return t;
}
void RainVolume::quad(std::vector<IRenderBackend::WorldMeshVertex>& vertices,
    glm::vec3 start,glm::vec3 end,glm::vec3 side,glm::vec3 color,float alpha,int tile,int columns,int tileWidth) {
    const std::array positions{start-side,start+side,end+side,end-side};
    for (std::size_t i=0;i<4;++i) {
        IRenderBackend::WorldMeshVertex v;
        v.x=positions[i].x;v.y=positions[i].y;v.z=positions[i].z;
        v.u=(i==1||i==2)?1.f:0.f;v.v=i<2?0.f:1.f;
        if (tile>=0) {
            const int extent=tileWidth?tileWidth:tileWidth_;
            v.u=(tile+(v.u*(extent-4)+2)/extent)/columns;
        }
        v.r=color.x;v.g=color.y;v.b=color.z;v.a=alpha;v.ny=1;v.tx=1;
        vertices.push_back(v);
    }
}
RainVolumeStats RainVolume::build(const WorldEnvironmentLighting& e,glm::vec3 camera,
    const RainCoverQuery& cover,const RainVisualSettings& s,const RainMotion* motion,const RainLayerSettings* layers) {
    drops_.clear();impacts_.clear();visibleImpacts_.clear();RainVolumeStats stats;
    layered_=layers!=nullptr;
    if (layers) {
        if (atlasPixels_.empty()||!motion) throw std::runtime_error("Layered rain requires a prepared atlas and integrated motion");
        for (std::size_t band=0;band<3;++band) if (const auto field=invalidRainBand(layers->bands[band],band);!field.empty())
            throw std::runtime_error("Invalid rain band: "+std::string(field));
    }
    if (const auto field=invalidRainSetting(s);!field.empty())
        throw std::runtime_error("Invalid rain visual setting: "+std::string(field));
    if (motion && (!std::isfinite(motion->travel.x)||!std::isfinite(motion->travel.y)||
        !std::isfinite(motion->travel.z)||glm::length(motion->travel)>2000000.||
        !std::isfinite(motion->impactSeconds)||motion->impactSeconds<0||motion->impactSeconds>8192))
        throw std::runtime_error("Invalid integrated rain motion");
    const auto generation=motion?motion->generation:0;
    const bool olderGeneration=generation && contactGeneration_ && generation<contactGeneration_;
    if (!e.enabled || !std::isfinite(e.rainIntensity) || e.rainIntensity<=0) {
        if (!olderGeneration) { contacts_={};contactSeconds_=-1; }
        return stats;
    }
    if (!finite(camera)||glm::length(camera)>1000000.f||!std::isfinite(e.precipitationSeconds)||
        e.precipitationSeconds<0||e.precipitationSeconds>8192||
        !std::isfinite(e.precipitationWind[0])||!std::isfinite(e.precipitationWind[1])||
        std::abs(e.precipitationWind[0])>100||std::abs(e.precipitationWind[1])>100||!cover||
        packWorldEnvironment(e)[0][3]==0) return stats;
    const float intensity=std::min(e.rainIntensity*s.densityScale,1.f);
    if (intensity<=0) {
        if (!olderGeneration) { contacts_={};contactSeconds_=-1; }
        return stats;
    }
    // A fixed view can still precede a clock wrap already sampled by an
    // interpolated view. It must not roll the retained contact owner backward.
    const bool reset=!olderGeneration && (contactSeconds_<0 || generation!=contactGeneration_ ||
        e.precipitationSeed!=contactSeed_ || (generation==0 && e.precipitationSeconds<contactSeconds_));
    acceptContacts_=!olderGeneration && (reset || e.precipitationSeconds>contactSeconds_);
    if (acceptContacts_) {
        if (reset) contacts_={};
        contactSeconds_=e.precipitationSeconds;
        contactSeed_=e.precipitationSeed;contactGeneration_=generation;
        for (auto& entry : contacts_) if (contactSeconds_-entry.born>=s.impactLifetime) entry.active=false;
    }
    const glm::vec3 velocity(e.precipitationWind[0]*s.windResponse,-s.fallSpeed,e.precipitationWind[1]*s.windResponse);
    const glm::vec3 direction=glm::normalize(velocity);
    const glm::dvec3 shift=motion?motion->travel:glm::dvec3(velocity)*e.precipitationSeconds;
    const glm::ivec3 origin=glm::ivec3(glm::floor((glm::dvec3(camera)-shift)/8.));
    const float brightness=std::clamp(e.ambientRadiance[1]*s.ambientScale+s.ambientBias,s.brightnessMin,s.brightnessMax);
    const glm::vec3 color=glm::vec3(s.tintR,s.tintG,s.tintB)*brightness;
    if (layers) stats=buildLayers(e,camera,cover,s,*motion,*layers);
    else for (int z=-4;z<=4;++z) for (int y=-2;y<=2;++y) for (int x=-4;x<=4;++x)
        for (std::uint32_t i=0;i<3;++i) {
            const glm::ivec3 cell=origin+glm::ivec3(x,y,z);
            const auto seed=cellSeed(cell.x,cell.y,cell.z,e.precipitationSeed)+i*17713u;
            const float opacity=std::clamp((intensity-random(seed+4))*8.f,0.f,1.f);
            if (opacity==0) continue;
            const glm::vec3 center=glm::vec3(glm::dvec3(cell)*8.+shift)+
                glm::vec3(random(seed+1),random(seed+2),random(seed+3))*8.f;
            const float alpha=opacity*fade(center-camera,s)*s.opacity*
                std::exp(-glm::length(center-camera)*e.hazeDensity);
            if (alpha<.005f) continue;
            const float length=s.length+random(seed+5)*s.lengthVariation;
            glm::vec3 end=center+direction*length;
            ++stats.queries;
            const auto hit=cover(center-direction*96.f,direction*(96.f+length));
            if (hit && (!finite(hit->point)||!std::isfinite(hit->fraction)||hit->fraction<0||hit->fraction>1)) continue;
            if (hit && hit->fraction<=96.f/(96.f+length)) { ++stats.covered;continue; }
            if (hit) end=hit->point-direction*.015f;
            glm::vec3 side=glm::cross(direction,camera-center);
            if (glm::length(side)<.001f) continue;
            side=glm::normalize(side)*(s.halfWidth+random(seed+6)*s.widthVariation);
            quad(drops_,center,end,side,color,alpha);++stats.drops;
        }
    if (layers&&!impactAtlasPixels_.empty()) { drawContacts(e,camera,s,*layers,stats);return stats; }
    const glm::ivec2 surfaceOrigin=glm::ivec2(glm::floor(glm::vec2(camera.x,camera.z)/6.f));
    for (int z=-4;z<=4;++z) for (int x=-4;x<=4;++x) {
        const auto seed=cellSeed(surfaceOrigin.x+x,0,surfaceOrigin.y+z,e.precipitationSeed);
        const double t=(motion?motion->impactSeconds:e.precipitationSeconds*s.impactRateScale)*(2.+std::floor(random(seed)*12.f)/8.)+random(seed+7);
        const float age=static_cast<float>(t-std::floor(t));
        if (age>s.impactLifetime) continue;
        glm::vec3 top((surfaceOrigin.x+x+random(seed+1))*6.f,camera.y+48.f,
            (surfaceOrigin.y+z+random(seed+2))*6.f);
        // Trace the same wind-aligned world column at any camera height.
        top.x+=velocity.x/velocity.y*top.y;top.z+=velocity.z/velocity.y*top.y;
        ++stats.queries;const auto hit=cover(top,direction*128.f);
        if (!hit||!finite(hit->point)||!finite(hit->normal)||std::abs(hit->normal.y)<.4f) continue;
        glm::vec3 normal=glm::normalize(hit->normal);if (normal.y<0) normal=-normal;
        const float distance=glm::length(hit->point-camera);
        const float alpha=intensity*(1-age/s.impactLifetime)*s.impactOpacity*std::clamp((s.horizontalRange-distance)/s.horizontalFade,0.f,1.f);
        if (alpha<.005f) continue;
        const float radius=s.impactRadius+age*s.impactGrowth;
        const glm::vec3 right=glm::normalize(glm::cross(normal,glm::vec3(0,0,1)))*radius;
        const glm::vec3 up=glm::normalize(glm::cross(normal,right))*radius;
        const glm::vec3 center=hit->point+normal*.025f;
        quad(impacts_,center-up,center+up,right,color,alpha);++stats.impacts;
    }
    stats.solidImpacts=stats.impacts;stats.impactQuads=stats.impacts;
    return stats;
}
RainVolumeStats RainVolume::buildLayers(const WorldEnvironmentLighting& e,glm::vec3 camera,
    const RainCoverQuery& cover,const RainVisualSettings& s,const RainMotion& motion,const RainLayerSettings& layers) {
    RainVolumeStats stats;
    const float brightness=std::clamp(e.ambientRadiance[1]*s.ambientScale+s.ambientBias,s.brightnessMin,s.brightnessMax);
    const glm::vec3 color=glm::vec3(s.tintR,s.tintG,s.tintB)*brightness;
    const glm::vec3 velocity(e.precipitationWind[0]*s.windResponse,-s.fallSpeed,e.precipitationWind[1]*s.windResponse);
    const glm::vec3 direction=glm::normalize(velocity);
    for (std::size_t band=0;band<3;++band) {
        const auto& b=layers.bands[band];const double cellSize=b.cellSize;
        const glm::dvec3 shift=motion.travel*static_cast<double>(b.speed);
        const auto origin=glm::ivec3(glm::floor((glm::dvec3(camera)-shift)/cellSize));
        const float density=std::min(e.rainIntensity*s.densityScale*b.density,1.f);
        for (int z=-4;z<=4;++z) for (int y=-2;y<=2;++y) for (int x=-4;x<=4;++x) {
            const auto cell=origin+glm::ivec3(x,y,z);
            const auto seed=cellSeed(cell.x,cell.y,cell.z,e.precipitationSeed)+static_cast<std::uint32_t>(band)*17713u;
            const float population=std::clamp((density-random(seed+4))*8.f,0.f,1.f);
            if (population==0) continue;
            const glm::vec3 center=glm::vec3(glm::dvec3(cell)*cellSize+shift)+
                glm::vec3(random(seed+1),random(seed+2),random(seed+3))*static_cast<float>(cellSize);
            const auto delta=center-camera;const float distance=glm::length(delta);
            const float bandFade=smooth(b.nearDistance,b.nearDistance+b.nearFade,distance)*
                (1-smooth(b.farDistance-b.farFade,b.farDistance,distance))*
                std::clamp((static_cast<float>(cellSize)*2-std::abs(delta.y))/std::min(s.verticalFade,static_cast<float>(cellSize)),0.f,1.f);
            const float alpha=population*fade(delta,s)*bandFade*s.opacity*b.opacity*std::exp(-distance*e.hazeDensity);
            if (alpha<.005f) continue;
            const float length=(s.length+random(seed+5)*s.lengthVariation)*b.length;
            auto end=center+direction*length;++stats.queries;
            const auto hit=cover(center-direction*96.f,direction*(96.f+length));
            if (hit&&(!finite(hit->point)||!std::isfinite(hit->fraction)||hit->fraction<0||hit->fraction>1)) continue;
            if (hit && !impactAtlasPixels_.empty()) {
                const auto id=(static_cast<std::uint32_t>(cell.x)&255u)|
                    ((static_cast<std::uint32_t>(cell.y)&255u)<<8)|((static_cast<std::uint32_t>(cell.z)&255u)<<16)|
                    (static_cast<std::uint32_t>(band)<<24);
                contact(*hit,end,glm::length(velocity)*b.speed,id,seed,band,e,s,stats);
            }
            if (hit&&hit->fraction<=96.f/(96.f+length)) { ++stats.covered;continue; }
            if (hit) end=hit->point-direction*.015f;
            auto side=glm::cross(direction,camera-center);if (glm::length(side)<.001f) continue;
            side=glm::normalize(side)*(s.halfWidth+random(seed+6)*s.widthVariation)*b.width;
            quad(drops_,center,end,side,color,alpha,static_cast<int>(band));++stats.drops;++stats.bandDrops[band];
        }
    }
    return stats;
}
void RainVolume::contact(const RainHit& hit,glm::vec3 tip,float speed,std::uint32_t id,std::uint32_t seed,std::size_t band,
    const WorldEnvironmentLighting& e,const RainVisualSettings& s,RainVolumeStats& stats) {
    if (!acceptContacts_ || stats.newImpacts>=maxImpacts || !finite(hit.normal)||
        glm::dot(hit.normal,hit.normal)<.0001f || !std::isfinite(hit.water)||hit.water<0||hit.water>1) return;
    const auto normal=glm::normalize(hit.normal);
    if (std::abs(normal.y)<.4f || random(seed+31)>std::min(1.f,s.impactRateScale*.5f)) return;
    const glm::vec3 velocity(e.precipitationWind[0]*s.windResponse,-s.fallSpeed,e.precipitationWind[1]*s.windResponse);
    const float age=glm::dot(tip-hit.point,glm::normalize(velocity))/speed;
    if (age<0 || age>=s.impactLifetime*.45f) return;
    for (const auto& entry : contacts_) if (entry.active&&entry.id==id) return;
    const auto slot=std::find_if(contacts_.begin(),contacts_.end(),[](const auto& entry) { return !entry.active; });
    if (slot==contacts_.end()) return;
    *slot={hit.point,normal.y<0?-normal:normal,e.precipitationSeconds-age,id,seed,band,hit.water,true};
    ++stats.newImpacts;
}
void RainVolume::drawContacts(const WorldEnvironmentLighting& e,glm::vec3 camera,const RainVisualSettings& s,
    const RainLayerSettings& layers,RainVolumeStats& stats) {
    const float brightness=std::clamp(e.ambientRadiance[1]*s.ambientScale+s.ambientBias,s.brightnessMin,s.brightnessMax);
    const glm::vec3 color=glm::vec3(s.tintR,s.tintG,s.tintB)*brightness;
    for (const auto& entry : contacts_) {
        if (!entry.active) continue;
        const float age=static_cast<float>(contactSeconds_-entry.born);
        if (age<0||age>=s.impactLifetime) continue;
        const float distance=glm::length(entry.point-camera);
        const float population=std::clamp((std::min(e.rainIntensity*s.densityScale*layers.bands[entry.band].density,1.f)-random(entry.seed+4))*8,0.f,1.f);
        const float alpha=population*s.impactOpacity*std::clamp((s.horizontalRange-distance)/s.horizontalFade,0.f,1.f)*std::exp(-distance*e.hazeDensity);
        if (alpha<.005f) continue;
        const float radius=s.impactRadius*(.8f+random(entry.seed+12)*.4f);
        const auto center=entry.point+entry.normal*.028f;
        bool visible=false;
        const float crownAge=age/(s.impactLifetime*.45f);
        if (crownAge<1) {
            auto right=glm::cross(entry.normal,camera-center);
            if (glm::length(right)<.001f) right=glm::cross(entry.normal,glm::vec3(0,0,1));
            right=glm::normalize(right)*radius*(1+crownAge*1.5f);
            const float height=(radius*2+s.impactGrowth*.25f)*(1-entry.water*.45f);
            const float crownAlpha=alpha*(1-crownAge)*(1-crownAge);
            if (crownAlpha>=.005f) {
                quad(impacts_,center+entry.normal*height,center,right,color,crownAlpha,
                    std::min(3,static_cast<int>(crownAge*4)),8,impactTileWidth_);
                visible=true;
            }
        }
        const float rippleAge=age/s.impactLifetime;
        const float rippleAlpha=alpha*entry.water*(1-rippleAge)*.8f;
        if (rippleAlpha>=.005f) {
            const auto right=glm::normalize(glm::cross(entry.normal,glm::vec3(0,0,1)))*(radius+age*s.impactGrowth);
            const auto up=glm::normalize(glm::cross(entry.normal,right))*(radius+age*s.impactGrowth);
            quad(impacts_,center-up,center+up,right,color,rippleAlpha,
                4+std::min(3,static_cast<int>(rippleAge*4)),8,impactTileWidth_);
            visible=true;
        }
        if (visible) {
            ++stats.impacts;if (entry.water>0) ++stats.waterImpacts;else ++stats.solidImpacts;
            visibleImpacts_.push_back({entry.point,entry.normal,age,entry.id,entry.water});
        }
    }
    stats.impactQuads=impacts_.size()/4;
}
void RainVolume::prewarm(IRenderBackend& renderer,WorldResourceScopeId scope) const {
    auto* scopes=dynamic_cast<IWorldResourceScope*>(&renderer);
    if (!scopes) throw std::runtime_error("Rain requires managed world resources");
    scopeCheck(scopes->beginWorldResourceScope(scope));
    for (bool impact : {false,true}) { const auto t=texture(impact);renderer.prewarmWorldTextureData(&t); }
    if (!atlasPixels_.empty()) { const auto t=texture(false,true);renderer.prewarmWorldTextureData(&t); }
    if (!impactAtlasPixels_.empty()) { const auto t=texture(true,true);renderer.prewarmWorldTextureData(&t); }
    scopeCheck(scopes->endWorldResourceScope());
}
void RainVolume::draw(IRenderBackend& renderer,WorldResourceScopeId scope,const float* vp,int width,int height) const {
    if (drops_.empty()&&impacts_.empty()) return;
    auto* scopes=dynamic_cast<IWorldResourceScope*>(&renderer);
    if (!scopes) throw std::runtime_error("Rain requires managed world resources");
    scopeCheck(scopes->beginWorldResourceScope(scope));
    for (bool impact : {false,true}) {
        const auto& vertices=impact?impacts_:drops_;if (vertices.empty()) continue;
        const auto t=texture(impact,layered_);
        renderer.drawWorldIndexedMeshTextured(vertices.data(),vertices.size(),indices_.data(),vertices.size()/4*6,&t,vp,width,height);
    }
    scopeCheck(scopes->endWorldResourceScope());
}
} // namespace engine::render
