#include "engine/render/WorldDiffuseProbes.h"
#include "engine/render/gltf/GltfSceneAsset.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace engine::render {
namespace {
constexpr float pi=3.14159265358979323846f;
glm::vec3 vec(const std::array<float,3>& a) { return {a[0],a[1],a[2]}; }
int sector(glm::vec3 d) {
    const auto a=glm::abs(d); const int axis=a.x>a.y?(a.x>a.z?0:2):(a.y>a.z?1:2);
    return axis*2+(d[axis]<0?1:0);
}
bool sameSource(const WorldRectLight& a,const WorldRectLight& b) {
    return a.position==b.position&&a.halfU==b.halfU&&a.halfV==b.halfV&&a.range==b.range;
}
void validateSource(const WorldRectLight& light) {
    auto white=light; white.radiance={1,1,1};
    WorldEnvironmentLighting e;e.enabled=true;e.rectLights[0]=white;
    if(packWorldRectLights(e)[0][3]<=0) throw std::invalid_argument("Invalid diffuse probe source geometry");
    for(float c:light.radiance) if(!std::isfinite(c)||c<0) throw std::invalid_argument("Invalid probe source radiance");
}
void validateTransport(const DiffuseProbeTransport& t) {
    const auto count=validateDiffuseGrid(t.settings.grid);
    if(t.sources.empty()||t.sources.size()>16||t.transfer.size()!=count*t.sources.size()||
       t.distanceMoments.size()!=count||t.validity.size()!=count||t.sceneIdentity.empty())
        throw std::invalid_argument("Invalid diffuse probe transport dimensions");
    for(const auto& light:t.sources) validateSource(light);
    for(const auto& sh:t.transfer) for(const auto& rgb:sh) for(float c:rgb)
        if(!std::isfinite(c)||std::abs(c)>1000) throw std::invalid_argument("Invalid diffuse transport coefficient");
    for(const auto& moments:t.distanceMoments) for(const auto& m:moments)
        if(!std::isfinite(m[0])||!std::isfinite(m[1])||m[0]<0||m[1]<m[0]*m[0]-.01f)
            throw std::invalid_argument("Invalid diffuse visibility moment");
    for(float v:t.validity) if(v!=0&&v!=1) throw std::invalid_argument("Invalid diffuse probe validity");
}
}
std::size_t validateDiffuseGrid(const DiffuseProbeGrid& g) {
    std::size_t count=1;
    for(int i=0;i<3;++i) {
        if(g.counts[i]<2||g.counts[i]>32||!std::isfinite(g.origin[i])||
           !std::isfinite(g.spacing[i])||g.spacing[i]<.01f||g.spacing[i]>1000)
            throw std::invalid_argument("Invalid diffuse probe grid");
        count*=static_cast<std::size_t>(g.counts[i]);
    }
    if(count>kDiffuseProbeCapacity||!std::isfinite(g.fadeDistance)||g.fadeDistance<=0||g.fadeDistance>1000)
        throw std::invalid_argument("Diffuse probe grid exceeds capacity or invalid fade");
    return count;
}
std::array<float,9> diffuseShBasis(glm::vec3 n) {
    return {.282094792f,.488602512f*n.y,.488602512f*n.z,.488602512f*n.x,
        1.092548431f*n.x*n.y,1.092548431f*n.y*n.z,.315391565f*(3*n.z*n.z-1),
        1.092548431f*n.x*n.z,.546274215f*(n.x*n.x-n.y*n.y)};
}
glm::vec3 evaluateDiffuseSh(const DiffuseSh& sh,glm::vec3 normal) {
    glm::vec3 out(0);const auto basis=diffuseShBasis(normal);
    for(std::size_t i=0;i<9;++i) out+=vec(sh[i])*basis[i];
    return glm::max(out,glm::vec3(0));
}
std::string diffuseSceneIdentity(const gltf::scene::GltfSceneAsset& asset) {
    std::uint64_t hash=14695981039346656037ull;
    const auto add=[&](const void* data,std::size_t bytes) {
        const auto* b=static_cast<const unsigned char*>(data);
        for(std::size_t i=0;i<bytes;++i) hash=(hash^b[i])*1099511628211ull;
    };
    add(asset.sourceIdentity.data(),asset.sourceIdentity.size());
    for(const auto& node:asset.nodes) add(&node.defaultGlobal,sizeof(node.defaultGlobal));
    for(const auto& p:asset.primitives) {
        add(p.geometryKey.data(),p.geometryKey.size());add(&p.nodeIndex,sizeof(p.nodeIndex));
        add(&p.materialIndex,sizeof(p.materialIndex));add(&p.skinIndex,sizeof(p.skinIndex));
    }
    for(const auto& m:asset.materials) {
        add(m.baseColorFactor.data(),sizeof(m.baseColorFactor));add(&m.metallicFactor,sizeof(float));
        add(&m.alphaMode,sizeof(m.alphaMode));add(&m.alphaCutoff,sizeof(float));add(&m.unlit,sizeof(m.unlit));
        for(const auto* tex:{&m.baseColor,&m.metallicRoughness}) {
            add(&tex->contentHash,sizeof(tex->contentHash));add(&tex->wrapS,sizeof(int));add(&tex->wrapT,sizeof(int));
        }
    }
    std::ostringstream s;s<<std::hex<<std::setfill('0')<<std::setw(16)<<hash;return s.str();
}
WorldDiffuseProbeUniforms relightDiffuseProbes(const DiffuseProbeTransport& t,std::span<const WorldRectLight> lights) {
    validateTransport(t);
    if(lights.size()!=t.sources.size()) throw std::invalid_argument("Diffuse probe source count changed; rebake required");
    bool active=false;
    for(std::size_t s=0;s<lights.size();++s) {
        validateSource(lights[s]);
        if(!sameSource(lights[s],t.sources[s])) throw std::invalid_argument("Diffuse probe source moved; rebake required");
        for(float c:lights[s].radiance) active|=c>0;
    }
    WorldDiffuseProbeUniforms out{}; if(!active) return out;
    const auto& g=t.settings.grid;
    for(int axis=0;axis<3;++axis) {out[0][axis]=g.origin[axis];out[1][axis]=g.spacing[axis];out[2][axis]=float(g.counts[axis]);}
    out[0][3]=1;out[1][3]=g.fadeDistance;
    for(std::size_t p=0;p<t.validity.size();++p) {
        const auto offset=3+p*kDiffuseProbeStride;
        for(std::size_t i=0;i<9;++i) for(std::size_t s=0;s<lights.size();++s) for(std::size_t c=0;c<3;++c)
            out[offset+i][c]+=t.transfer[p*lights.size()+s][i][c]*lights[s].radiance[c];
        out[offset][3]=t.validity[p];
        for(std::size_t i=0;i<6;++i) {out[offset+9+i][0]=t.distanceMoments[p][i][0];out[offset+9+i][1]=t.distanceMoments[p][i][1];}
    }
    validateDiffuseProbeUniforms(out);return out;
}
void validateDiffuseProbeUniforms(const WorldDiffuseProbeUniforms& p) {
    for(const auto& v:p) for(float f:v) if(!std::isfinite(f)) throw std::invalid_argument("Nonfinite diffuse probe packet");
    if(p[0][3]==0) {
        if(p!=WorldDiffuseProbeUniforms{}) throw std::invalid_argument("Disabled diffuse probes must be zero");
        return;
    }
    if(p[0][3]!=1) throw std::invalid_argument("Invalid diffuse probe enable");
    DiffuseProbeGrid g;
    for(int a=0;a<3;++a) {
        if(p[2][a]<2||p[2][a]>32||std::floor(p[2][a])!=p[2][a]) throw std::invalid_argument("Invalid probe count");
        g.origin[a]=p[0][a];g.spacing[a]=p[1][a];g.counts[a]=static_cast<int>(p[2][a]);
    }
    g.fadeDistance=p[1][3];const auto count=validateDiffuseGrid(g);
    for(std::size_t n=0;n<count;++n) {
        const auto offset=3+n*kDiffuseProbeStride;
        if(p[offset][3]!=0&&p[offset][3]!=1) throw std::invalid_argument("Invalid probe validity");
        for(std::size_t f=0;f<6;++f) {
            const auto& m=p[offset+9+f];
            if(m[0]<0||m[1]<m[0]*m[0]-.01f) throw std::invalid_argument("Invalid probe moments");
        }
    }
}
glm::vec3 sampleDiffuseProbes(const WorldDiffuseProbeUniforms& p,glm::vec3 position,glm::vec3 normal) {
    if(p[0][3]==0) return {};
    const glm::vec3 origin(p[0][0],p[0][1],p[0][2]),spacing(p[1][0],p[1][1],p[1][2]);
    const glm::ivec3 counts{int(p[2][0]),int(p[2][1]),int(p[2][2])};
    const auto coord=(position-origin)/spacing;
    const auto clamped=glm::clamp(coord,glm::vec3(0),glm::vec3(counts-1));
    const auto outside=glm::abs((coord-clamped)*spacing);
    const float fade=1-std::clamp(std::max({outside.x,outside.y,outside.z})/p[1][3],0.f,1.f);
    const auto cell=glm::min(glm::ivec3(glm::floor(clamped)),counts-2);
    const auto fraction=clamped-glm::vec3(cell);
    glm::vec3 irradiance(0);float weightSum=0;
    for(int corner=0;corner<8;++corner) {
        const glm::ivec3 bits(corner&1,(corner>>1)&1,(corner>>2)&1),index=cell+bits;
        const auto n=static_cast<std::size_t>((index.z*counts.y+index.y)*counts.x+index.x);
        const auto offset=3+n*kDiffuseProbeStride;
        const auto delta=position-(origin+glm::vec3(index)*spacing);
        const float distance=glm::length(delta);
        const auto direction=distance>.0001f?delta/distance:normal;
        const auto& m=p[offset+9+static_cast<std::size_t>(sector(direction))];
        const float excess=std::max(0.f,distance-m[0]);
        const float variance=std::max(.0025f,m[1]-m[0]*m[0]);
        const float visibility=variance/(variance+excess*excess);
        const auto w=glm::mix(glm::vec3(1)-fraction,fraction,glm::vec3(bits));
        const float facing=std::max(.05f,.5f-.5f*glm::dot(normal,direction));
        const float weight=w.x*w.y*w.z*p[offset][3]*facing*facing*visibility*visibility*visibility;
        DiffuseSh sh;for(std::size_t i=0;i<9;++i) for(int c=0;c<3;++c) sh[i][c]=p[offset+i][c];
        irradiance+=evaluateDiffuseSh(sh,normal)*weight;weightSum+=weight;
    }
    // Do not renormalize arbitrarily tiny visibility into full light behind walls.
    return irradiance*(fade/std::max(weightSum,.05f));
}
nlohmann::json diffuseProbeTransportJson(const DiffuseProbeTransport& t) {
    validateTransport(t);const auto& s=t.settings;
    nlohmann::json lights=nlohmann::json::array();
    for(const auto& l:t.sources) lights.push_back({{"position",l.position},{"half_u",l.halfU},{"half_v",l.halfV},{"range",l.range}});
    return {{"schema","phlosion-diffuse-transport-v1"},{"scene_identity",t.sceneIdentity},
        {"grid",{{"origin",s.grid.origin},{"spacing",s.grid.spacing},{"counts",s.grid.counts},{"fade_distance",s.grid.fadeDistance}}},
        {"rays",s.rayCount},{"area_samples",{s.areaSamplesU,s.areaSamplesV}},{"max_distance",s.maxDistance},
        {"sources",lights},{"transfer",t.transfer},{"distance_moments",t.distanceMoments},{"validity",t.validity}};
}
DiffuseProbeTransport diffuseProbeTransportFromJson(const nlohmann::json& j) {
    if(j.at("schema")!="phlosion-diffuse-transport-v1") throw std::invalid_argument("Unsupported diffuse transport schema");
    DiffuseProbeTransport t;t.sceneIdentity=j.at("scene_identity");auto& s=t.settings;const auto& g=j.at("grid");
    g.at("origin").get_to(s.grid.origin);g.at("spacing").get_to(s.grid.spacing);g.at("counts").get_to(s.grid.counts);
    g.at("fade_distance").get_to(s.grid.fadeDistance);j.at("rays").get_to(s.rayCount);j.at("max_distance").get_to(s.maxDistance);
    s.areaSamplesU=j.at("area_samples").at(0);s.areaSamplesV=j.at("area_samples").at(1);
    for(const auto& l:j.at("sources")) {
        WorldRectLight light;l.at("position").get_to(light.position);l.at("half_u").get_to(light.halfU);
        l.at("half_v").get_to(light.halfV);l.at("range").get_to(light.range);light.radiance={1,1,1};t.sources.push_back(light);
    }
    j.at("transfer").get_to(t.transfer);j.at("distance_moments").get_to(t.distanceMoments);j.at("validity").get_to(t.validity);
    validateTransport(t);return t;
}
}
