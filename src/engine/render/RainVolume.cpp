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
void RainVolume::setImpactAtlas(std::string key,int tileWidth,int height,std::span<const unsigned char> rgba,int columns) {
    if (key.empty()||tileWidth<4||tileWidth>256||height<4||height>256||(columns!=8&&columns!=12&&columns!=16)||
        rgba.size()!=static_cast<std::size_t>(tileWidth)*columns*height*4)
        throw std::runtime_error("Invalid rain impact atlas");
    impactAtlasKey_=std::move(key);impactTileWidth_=tileWidth;impactAtlasHeight_=height;
    impactColumns_=columns;
    impactAtlasPixels_.assign(rgba.begin(),rgba.end());contacts_={};contactSeconds_=-1;
}
void RainVolume::setMistTexture(std::string key,int width,int height,std::span<const unsigned char> rgba) {
    if (key.empty()||width<4||width>256||height<4||height>256||rgba.size()!=static_cast<std::size_t>(width)*height*4)
        throw std::runtime_error("Invalid rain mist texture");
    mistKey_=std::move(key);mistWidth_=width;mistHeight_=height;mistPixels_.assign(rgba.begin(),rgba.end());
}
RainVolume::RainVolume() {
    constexpr auto maxBatch=std::max({maxDrops,maxContactQuads,maxSprayQuads+maxMistQuads});
    drops_.reserve(maxDrops*4);impacts_.reserve(maxContactQuads*4);indices_.reserve(maxBatch*6);
    visibleImpacts_.reserve(maxImpacts);
    mist_.reserve(maxMistQuads*4);
    spray_.reserve(maxSprayQuads*4);fog_.reserve((maxMistQuads+maxSprayQuads)*4);
    fogSort_.reserve(maxMistQuads+maxSprayQuads);
    for (std::uint32_t i=0;i<maxBatch;++i)
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
        t.key=t.cacheKey=impactAtlasKey_.c_str();t.rgba=impactAtlasPixels_.data();t.width=impactTileWidth_*impactColumns_;t.height=impactAtlasHeight_;
    }
    t.wrapS=t.wrapT=33071;t.alphaMode=2;t.materialMode=255;t.textureSrgb=0;
    return t;
}
IRenderBackend::WorldTextureData RainVolume::mistTexture() const {
    auto t=texture(true);t.key=t.cacheKey=mistKey_.c_str();t.rgba=mistPixels_.data();
    t.width=mistWidth_;t.height=mistHeight_;t.materialMode=249;
    t.materialTimeSec=static_cast<float>(std::fmod(std::max(0.,contactSeconds_),512.)/128.*6.283185307);
    t.directionalShadowCaster=t.directionalShadowReceiver=0;return t;
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
    drops_.clear();impacts_.clear();mist_.clear();spray_.clear();fog_.clear();visibleImpacts_.clear();RainVolumeStats stats;
    layered_=layers!=nullptr;
    if (layers) {
        if (atlasPixels_.empty()||!motion) throw std::runtime_error("Layered rain requires a prepared atlas and integrated motion");
        for (std::size_t band=0;band<3;++band) if (const auto field=invalidRainBand(layers->bands[band],band);!field.empty())
            throw std::runtime_error("Invalid rain band: "+std::string(field));
    }
    if (const auto field=invalidRainSetting(s);!field.empty())
        throw std::runtime_error("Invalid rain visual setting: "+std::string(field));
    if (s.sprayOpacity>0 && (!layers||impactAtlasPixels_.empty()||impactColumns_<12))
        throw std::runtime_error("Contact spray requires layered rain and a twelve- or sixteen-column impact atlas");
    if (s.glowOpacity>0 && (!layers||impactAtlasPixels_.empty()||impactColumns_!=16))
        throw std::runtime_error("Contact glow requires layered rain and a sixteen-column impact atlas");
    if (s.mistOpacity>0 && (!layers||mistPixels_.empty()))
        throw std::runtime_error("Drifting rain mist requires layered rain and a prepared mist texture");
    if (s.sprayOpacity>0 && impactColumns_==16 && mistPixels_.empty())
        throw std::runtime_error("Spatial contact spray requires a prepared mist texture");
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
        acceptedTravel_=motion?motion->travel:glm::dvec3{};
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
    if (layers&&!impactAtlasPixels_.empty()) {
        drawContacts(e,camera,s,*layers,stats);buildMist(e,camera,cover,s,stats);return stats;
    }
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
        if (!hit||!hit->spawnImpact||!finite(hit->point)||!finite(hit->normal)||std::abs(hit->normal.y)<.4f) continue;
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
    if (layers) buildMist(e,camera,cover,s,stats);
    return stats;
}
RainVolumeStats RainVolume::buildImpacts(const WorldEnvironmentLighting& e,glm::vec3 camera,
    std::span<const RainImpact> impacts,const RainVisualSettings& s,float motionLifetime) {
    drops_.clear();impacts_.clear();mist_.clear();spray_.clear();fog_.clear();visibleImpacts_.clear();
    contacts_={};contactSeconds_=0;layered_=true;RainVolumeStats stats;
    if(impacts.size()>maxImpacts || impactAtlasPixels_.empty() || !invalidRainSetting(s).empty() ||
        s.mistOpacity!=0 || s.sprayOpacity!=0 || s.glowOpacity!=0 ||
        !std::isfinite(motionLifetime) || motionLifetime<0 || motionLifetime>1)
        throw std::runtime_error("Invalid sampled rain contacts/settings");
    if(!e.enabled || !std::isfinite(e.rainIntensity) || e.rainIntensity<=0 || !finite(camera) ||
        packWorldEnvironment(e)[0][3]==0) return stats;
    for(std::size_t i=0;i<impacts.size();++i) {
        const auto& hit=impacts[i];
        if(!finite(hit.point)||!finite(hit.normal)||glm::dot(hit.normal,hit.normal)<.0001f ||
            !std::isfinite(hit.age)||hit.age<0 || !std::isfinite(hit.water)||hit.water<0||hit.water>1 ||
            !std::isfinite(hit.strength)||hit.strength<0||hit.strength>1)
            throw std::runtime_error("Invalid sampled rain contact");
        auto normal=glm::normalize(hit.normal);if(normal.y<0) normal=-normal;
        if(normal.y<.4f || hit.age>=s.impactLifetime || hit.strength==0) continue;
        contacts_[i]={hit.point,normal,-hit.age,hit.id,hit.id,0,hit.water,true,hit.strength};
    }
    drawContacts(e,camera,s,RainLayerSettings{},stats,true,motionLifetime);
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
    if (!acceptContacts_ || !hit.spawnImpact || stats.newImpacts>=maxImpacts || !finite(hit.normal)||
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
    const RainLayerSettings& layers,RainVolumeStats& stats,bool sampled,float motionLifetime) {
    const float brightness=std::clamp(e.ambientRadiance[1]*s.ambientScale+s.ambientBias,s.brightnessMin,s.brightnessMax);
    const glm::vec3 color=glm::vec3(s.tintR,s.tintG,s.tintB)*brightness;
    for (const auto& entry : contacts_) {
        if (!entry.active) continue;
        const float age=static_cast<float>(contactSeconds_-entry.born);
        if (age<0||age>=s.impactLifetime) continue;
        const float distance=glm::length(entry.point-camera);
        const float population=sampled?entry.strength:std::clamp((std::min(e.rainIntensity*s.densityScale*layers.bands[entry.band].density,1.f)-random(entry.seed+4))*8,0.f,1.f);
        const float exposure=population*std::clamp((s.horizontalRange-distance)/s.horizontalFade,0.f,1.f)*std::exp(-distance*e.hazeDensity);
        const float alpha=exposure*s.impactOpacity;
        if (alpha<.005f && exposure*s.sprayOpacity<.001f && exposure*s.glowOpacity<.001f) continue;
        const float radius=s.impactRadius*(.8f+random(entry.seed+12)*.4f);
        const auto center=entry.point+entry.normal*.028f;
        bool visible=false;
        const float motionDuration=motionLifetime>0?std::min(motionLifetime,s.impactLifetime):s.impactLifetime;
        const float crownAge=age/(motionDuration*.45f);
        const auto tangent=glm::normalize(glm::cross(entry.normal,glm::vec3(0,0,1)));
        const auto bitangent=glm::cross(tangent,entry.normal);
        if (impactColumns_==16) {
            if (crownAge<1 && alpha*(1-crownAge)>=.005f) {
                const float spread=radius*(.45f+crownAge*1.4f);
                const float height=radius*(1.2f-crownAge)*(.8f-entry.water*.3f);
                for (int sector=0;sector<8;++sector) {
                    const float angle=(sector+random(entry.seed+12))*6.283185307f/8;
                    const auto radial=tangent*std::cos(angle)+bitangent*std::sin(angle);
                    const auto sideways=(-tangent*std::sin(angle)+bitangent*std::cos(angle))*spread*.42f;
                    // Curved, world-oriented crown segments; no camera-facing silhouette.
                    quad(impacts_,center+radial*spread+entry.normal*height,
                        center+radial*(spread*.7f),sideways,color,alpha*(1-crownAge)*.8f,
                        static_cast<int>(entry.seed%3),impactColumns_,impactTileWidth_);
                }
                visible=true;
            }
            const float flight=age/motionDuration;
            for (int droplet=0;droplet<4;++droplet) {
                const float t=flight*(1.1f+random(entry.seed+droplet+61)*.5f);
                if (t>=1) continue;
                const float angle=random(entry.seed+droplet+71)*6.283185307f;
                const auto radial=tangent*std::cos(angle)+bitangent*std::sin(angle);
                const float reach=radius*(1.2f+random(entry.seed+droplet+81));
                const float height=radius*(.8f+random(entry.seed+droplet+91));
                const auto position=center+radial*reach*t+entry.normal*(4*height*t*(1-t));
                const auto axis=glm::normalize(radial*reach+entry.normal*(4*height*(1-2*t)));
                const auto u=glm::normalize(glm::cross(axis,entry.normal));
                const auto v=glm::cross(axis,u);
                const float width=std::clamp(radius*.045f,.003f,.014f);
                const float length=width*(2+2*std::abs(1-2*t));
                const float opacity=alpha*(1-t)*.9f;
                if (opacity<.005f) continue;
                // Three-sided micro-prism follows the ballistic velocity in 3D.
                for (int face=0;face<3;++face) {
                    const float a=face*6.283185307f/3,b=(face+1)*6.283185307f/3;
                    const auto edge0=(u*std::cos(a)+v*std::sin(a))*width;
                    const auto edge1=(u*std::cos(b)+v*std::sin(b))*width;
                    const auto start=position+axis*length+(edge0+edge1)*.5f;
                    const auto end=position-axis*length+(edge0+edge1)*.5f;
                    quad(impacts_,start,end,(edge1-edge0)*.5f,color*(.75f+.25f*face),opacity,
                        12+droplet,impactColumns_,impactTileWidth_);
                }
                visible=true;
            }
        } else {
        if (crownAge<1) {
            auto right=glm::cross(entry.normal,camera-center);
            if (glm::length(right)<.001f) right=glm::cross(entry.normal,glm::vec3(0,0,1));
            right=glm::normalize(right)*radius*(1+crownAge*1.5f);
            const float height=(radius*2+s.impactGrowth*.25f)*(1-entry.water*.45f);
            const float crownAlpha=alpha*(1-crownAge)*(impactColumns_==16?1.f:1-crownAge);
            if (crownAlpha>=.005f) {
                quad(impacts_,center+entry.normal*height,center,right,color,crownAlpha,
                    impactColumns_==16?static_cast<int>(entry.seed%3):std::min(3,static_cast<int>(crownAge*4)),impactColumns_,impactTileWidth_);
                visible=true;
            }
        }
        }
        const float rippleAge=age/s.impactLifetime;
        const float rippleAlpha=alpha*entry.water*(1-rippleAge)*.8f;
        if (rippleAlpha>=.005f) {
            const auto right=glm::normalize(glm::cross(entry.normal,glm::vec3(0,0,1)))*(radius+age*s.impactGrowth);
            const auto up=glm::normalize(glm::cross(entry.normal,right))*(radius+age*s.impactGrowth);
            quad(impacts_,center-up,center+up,right,color,rippleAlpha,
                4+std::min(3,static_cast<int>(rippleAge*4)),impactColumns_,impactTileWidth_);
            visible=true;
        }
        const float sprayAlpha=exposure*s.sprayOpacity*4*rippleAge*(1-rippleAge);
        if (sprayAlpha>=.001f && impactColumns_==16) {
            const auto first=spray_.size();
            densityVolume(spray_,center,entry.normal,s.sprayRadius*(.6f+.4f*rippleAge),
                s.sprayHeight,color,sprayAlpha,entry.seed,8,camera);
            stats.sprayQuads+=(spray_.size()-first)/4;visible=true;
        } else if (sprayAlpha>=.001f) {
            auto right=glm::cross(entry.normal,camera-center);
            if (glm::length(right)<.001f) right=glm::cross(entry.normal,glm::vec3(0,0,1));
            const float spread=(.65f+rippleAge*.35f)*(.85f+random(entry.seed+45)*.3f);
            right=glm::normalize(right)*s.sprayRadius*spread;
            // The base stays on the exposed first-hit surface, including roofs.
            // Ordinary depth testing occludes the spray behind scene geometry.
            quad(impacts_,center+entry.normal*s.sprayHeight*(.6f+.4f*rippleAge),center,
                right,color,sprayAlpha,8+static_cast<int>(entry.seed%4),impactColumns_,impactTileWidth_);
            ++stats.sprayQuads;visible=true;
        }
        const float glowAlpha=exposure*s.glowOpacity*std::pow(std::sin(rippleAge*3.14159265f),2.f);
        if (glowAlpha>=.001f) {
            auto right=tangent;
            const float glowRadius=s.glowRadius*(.7f+random(entry.seed+46)*.6f);
            right=glm::normalize(right)*glowRadius;
            const auto glowCenter=center;
            quad(impacts_,glowCenter+bitangent*glowRadius,glowCenter-bitangent*glowRadius,
                right,color,glowAlpha,12+static_cast<int>(entry.seed%4),impactColumns_,impactTileWidth_);
            ++stats.glowQuads;visible=true;
        }
        if (visible) {
            ++stats.impacts;if (entry.water>0) ++stats.waterImpacts;else ++stats.solidImpacts;
            visibleImpacts_.push_back({entry.point,entry.normal,age,entry.id,entry.water,entry.strength});
        }
    }
    stats.impactQuads=impacts_.size()/4-(impactColumns_==16?0:stats.sprayQuads)-stats.glowQuads;
}
void RainVolume::buildMist(const WorldEnvironmentLighting& e,glm::vec3 camera,const RainCoverQuery& cover,
    const RainVisualSettings& s,RainVolumeStats& stats) {
    // 1/64 travel and a 16-cell seed period preserve the 8192 m motion wrap.
    const glm::vec2 drift(acceptedTravel_.x/64.,acceptedTravel_.z/64.);
    const glm::ivec2 origin=glm::ivec2(glm::floor((glm::vec2(camera.x,camera.z)-drift)/8.f));
    struct Patch { glm::vec3 base,normal; float radius,height,alpha,distance; std::uint32_t seed; };
    std::array<Patch,maxMistVolumes> patches{};std::size_t count=0;
    if (s.mistOpacity>0) for (int z=-3;z<=3;++z) for (int x=-3;x<=3;++x) {
        const auto cell=origin+glm::ivec2(x,z);
        const auto seed=cellSeed(cell.x&15,0,cell.y&15,contactSeed_^9173u);
        const float phase=static_cast<float>(std::fmod(contactSeconds_,128.)/128.)+random(seed+3);
        const float wave=std::sin(phase*6.283185307f);
        glm::vec3 top((cell.x+.2f+random(seed)*.6f)*8+drift.x+wave*.15f,camera.y+48,
            (cell.y+.2f+random(seed+1)*.6f)*8+drift.y+std::cos(phase*6.283185307f)*.15f);
        const float distance=glm::length(glm::vec2(top.x-camera.x,top.z-camera.z));
        const float edge=1-smooth(12,20,distance);
        if (edge<=0) continue;
        ++stats.queries;const auto hit=cover(top,{0,-128,0});
        if (!hit||!finite(hit->point)||!finite(hit->normal)||!std::isfinite(hit->fraction)||
            hit->fraction<0||hit->fraction>1||glm::length(hit->normal)<.001f) continue;
        auto normal=glm::normalize(hit->normal);if (normal.y<0) normal=-normal;
        if (normal.y<.8f||std::abs(hit->point.y-camera.y)>s.verticalRange) continue;
        const float alpha=s.mistOpacity*std::min(e.rainIntensity,1.f)*edge*(.25f+.75f*wave*wave)*
            std::exp(-distance*e.hazeDensity)*smooth(.7f,3.f,glm::length(hit->point-camera));
        if (alpha<.001f) continue;
        patches[count++]={hit->point+normal*.04f,normal,s.mistRadius*(.8f+random(seed+6)*.4f),
            s.mistHeight*(.8f+random(seed+7)*.4f),alpha,glm::length(hit->point-camera),seed};
    }
    std::sort(patches.begin(),patches.begin()+count,[](const auto& a,const auto& b){return a.distance>b.distance;});
    const float brightness=std::clamp(e.ambientRadiance[1]*s.ambientScale+s.ambientBias,s.brightnessMin,s.brightnessMax);
    const auto color=glm::vec3(s.tintR,s.tintG,s.tintB)*brightness;
    for (std::size_t i=0;i<count;++i) {
        const auto& p=patches[i];const auto first=mist_.size();
        densityVolume(mist_,p.base,p.normal,p.radius,p.height,color,p.alpha,p.seed,16,camera);
        stats.mistQuads+=(mist_.size()-first)/4;
    }
    fogSort_.clear();fog_.clear();
    for (const auto* vertices : {&mist_,&spray_}) for (std::size_t i=0;i<vertices->size();i+=4) {
        FogQuad item;glm::vec3 center{};
        for (int j=0;j<4;++j) {item.vertices[j]=(*vertices)[i+j];const auto& v=item.vertices[j];center+=glm::vec3(v.x,v.y,v.z)*.25f;}
        item.distance=glm::length(center-camera);fogSort_.push_back(item);
    }
    std::sort(fogSort_.begin(),fogSort_.end(),[](const auto& a,const auto& b){return a.distance>b.distance;});
    for (const auto& item:fogSort_) fog_.insert(fog_.end(),item.vertices.begin(),item.vertices.end());
}
void RainVolume::densityVolume(std::vector<IRenderBackend::WorldMeshVertex>& vertices,
    glm::vec3 base,glm::vec3 normal,float radius,float height,glm::vec3 color,float opacity,
    std::uint32_t seed,int slices,glm::vec3 camera) {
    const auto center=base+normal*(height*.5f);
    const auto xAxis=glm::normalize(glm::cross(normal,glm::vec3(0,0,1))),zAxis=glm::cross(xAxis,normal);
    auto view=camera-center;if (glm::length(view)<.001f) view={0,0,1};view=glm::normalize(view);
    auto right=glm::cross(view,glm::vec3(0,1,0));if(glm::length(right)<.001f) right={1,0,0};right=glm::normalize(right);
    const auto up=glm::cross(right,view);const float extent=std::max(radius,height*.5f);
    for (int slice=0;slice<slices;++slice) {
        const auto point=center+view*((2*(slice+.5f)/slices-1)*extent);
        const auto first=vertices.size();quad(vertices,point+up*extent,point-up*extent,right*extent,color,opacity);
        for(std::size_t i=first;i<vertices.size();++i) {
            auto& vertex=vertices[i];const auto delta=glm::vec3(vertex.x,vertex.y,vertex.z)-center;
            vertex.sourceUv1U=glm::dot(delta,xAxis)/radius;vertex.sourceUv1V=glm::dot(delta,normal)/(height*.5f);
            vertex.sourceUv2U=glm::dot(delta,zAxis)/radius;vertex.sourceUv2V=random(seed)*6.283185307f;
            vertex.u=2*extent/slices;vertex.v=0;
        }
    }
}
void RainVolume::prewarm(IRenderBackend& renderer,WorldResourceScopeId scope) const {
    auto* scopes=dynamic_cast<IWorldResourceScope*>(&renderer);
    if (!scopes) throw std::runtime_error("Rain requires managed world resources");
    scopeCheck(scopes->beginWorldResourceScope(scope));
    for (bool impact : {false,true}) { const auto t=texture(impact);renderer.prewarmWorldTextureData(&t); }
    if (!atlasPixels_.empty()) { const auto t=texture(false,true);renderer.prewarmWorldTextureData(&t); }
    if (!impactAtlasPixels_.empty()) { const auto t=texture(true,true);renderer.prewarmWorldTextureData(&t); }
    if (!mistPixels_.empty()) { const auto t=mistTexture();renderer.prewarmWorldTextureData(&t); }
    scopeCheck(scopes->endWorldResourceScope());
}
void RainVolume::draw(IRenderBackend& renderer,WorldResourceScopeId scope,const float* vp,int width,int height,unsigned layerMask) const {
    if (drops_.empty()&&impacts_.empty()&&fog_.empty()) return;
    auto* scopes=dynamic_cast<IWorldResourceScope*>(&renderer);
    if (!scopes) throw std::runtime_error("Rain requires managed world resources");
    scopeCheck(scopes->beginWorldResourceScope(scope));
    const auto& fog=(layerMask&6)==6?fog_:(layerMask&4)?mist_:spray_;
    if (!fog.empty()&&(layerMask&6)) {
        const auto t=mistTexture();
        renderer.drawWorldIndexedMeshTextured(fog.data(),fog.size(),indices_.data(),fog.size()/4*6,&t,vp,width,height);
    }
    for (bool impact : {false,true}) {
        if (!(layerMask&(impact?2:1))) continue;
        const auto& vertices=impact?impacts_:drops_;if (vertices.empty()) continue;
        const auto t=texture(impact,layered_);
        renderer.drawWorldIndexedMeshTextured(vertices.data(),vertices.size(),indices_.data(),vertices.size()/4*6,&t,vp,width,height);
    }
    scopeCheck(scopes->endWorldResourceScope());
}
} // namespace engine::render
