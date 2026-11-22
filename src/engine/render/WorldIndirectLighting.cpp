#include "engine/assets/ResourceBank.h"
#include "WorldIndirectLighting.h"
#include "engine/render/WorldLayeredMaterial.h"
#include "WorldIndirectTransportCodec.h"
#include "WorldDiffuseProbes.h"
#include "engine/render/gltf/GltfSceneAsset.h"
#include <sstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <glm/gtc/packing.hpp>
#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <set>

namespace engine::render {
namespace {
bool finite(glm::vec3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
glm::vec3 v3(const IndirectRow& r){return {r[0],r[1],r[2]};}
IndirectRow row(glm::vec3 v,float w=0){return {v.x,v.y,v.z,w};}
std::size_t gridCount(const IndirectBakeSettings& s) {
    std::size_t n=1;
    if(!finite(s.origin)||!finite(s.spacing)||!std::isfinite(s.maxDistance)||s.maxDistance<=0||
       !std::isfinite(s.fadeDistance)||s.fadeDistance<=0||s.volume>1||
       !std::isfinite(s.surfelSpacing)||s.surfelSpacing<.125f||s.surfelSpacing>4)
        throw std::invalid_argument("Invalid indirect grid");
    for(unsigned a=0;a<3;++a) {
        if(s.counts[a]<2||s.counts[a]>65||s.spacing[a]<=0||s.spacing[a]>64)throw std::invalid_argument("Indirect grid extent");
        n*=s.counts[a];
    }
    if(n>8192||s.faceResolution<4||s.faceResolution>64||(s.faceResolution&(s.faceResolution-1)))
        throw std::invalid_argument("Indirect bake capacity");
    if(s.terrainResolution&&(s.terrainResolution<4||s.terrainResolution>1024||(s.terrainResolution&(s.terrainResolution-1))))
        throw std::invalid_argument("Indirect terrain resolution");
    if(!std::isfinite(s.terrainSpacing)||s.terrainSpacing<=0||!std::isfinite(s.terrainOrigin.x)||!std::isfinite(s.terrainOrigin.y))
        throw std::invalid_argument("Indirect terrain extent");
    if(s.materialEmissionChannels.size()!=s.materialEmissionFactors.size()||s.materialEmissionChannels.size()>65536)
        throw std::invalid_argument("Indirect emission metadata");
    for(auto channel:s.materialEmissionChannels)if(channel< -1||channel>=32)throw std::invalid_argument("Indirect emission channel");
    for(auto factor:s.materialEmissionFactors)for(unsigned a=0;a<3;++a)
        if(!std::isfinite(factor[a])||factor[a]<0||factor[a]>64512)throw std::invalid_argument("Indirect emission factor");
    return n;
}
float packed(std::uint32_t v){return std::bit_cast<float>(v);}
}
std::string indirectSceneIdentity(const gltf::scene::GltfSceneAsset& asset) {
    const auto geometry=diffuseSceneIdentity(asset);std::uint64_t hash=14695981039346656037ull;
    const auto add=[&](const void* p,std::size_t n){const auto* b=static_cast<const unsigned char*>(p);for(std::size_t i=0;i<n;++i)hash=(hash^b[i])*1099511628211ull;};
    add(geometry.data(),geometry.size());
    if(asset.layeredMaterials)add(asset.layeredMaterials->identity.data(),asset.layeredMaterials->identity.size());
    for(const auto& m:asset.materials) {
        add(m.emissiveFactor.data(),sizeof(m.emissiveFactor));add(&m.emissive.contentHash,sizeof(m.emissive.contentHash));
        add(&m.emissive.wrapS,sizeof(int));add(&m.emissive.wrapT,sizeof(int));
        add(&m.normal.contentHash,sizeof(m.normal.contentHash));add(&m.normal.wrapS,sizeof(int));add(&m.normal.wrapT,sizeof(int));
        add(&m.normalScale,sizeof(m.normalScale));add(&m.hasNormalMap,sizeof(m.hasNormalMap));
    }
    std::ostringstream out;out<<std::hex<<std::setw(16)<<std::setfill('0')<<hash;return out.str();
}
void validateIndirectControls(const IndirectControls& c) {
    for(float x:{c.multibounce,c.saturation,c.compensation,c.emissiveScale,c.localScale,c.skyScale,
                 c.shadowDistanceWeight,c.normalWeight,c.probeDistanceWeight,c.visibilityBias,c.threshold})
        if(!std::isfinite(x)||x<0)throw std::invalid_argument("Invalid indirect lighting control");
    if(!std::isfinite(c.reflectionDimming)||c.reflectionDimming<0||c.reflectionDimming>1)throw std::invalid_argument("Indirect reflection dimming");
    for(auto x:c.distantRange)if(!std::isfinite(x))throw std::invalid_argument("Distant range");
    for(auto x:c.distantControls)if(!std::isfinite(x)||x<0)throw std::invalid_argument("Distant controls");
    if(c.distantControls[1]>1||c.distantControls[3]>1)throw std::invalid_argument("Distant energy");
    if(c.multibounce>1||c.saturation<.05f||c.saturation>1||c.compensation>.3f||c.threshold<=0)
        throw std::invalid_argument("Indirect energy/visibility control range");
}
void validateIndirectTransport(const IndirectTransport& t) {
    const auto count=gridCount(t.settings);
    if(t.sceneIdentity.empty()||t.sceneIdentity.size()>256||t.probes.size()!=count||t.bricks.size()>32768||
       t.surfels.size()>(1u<<21)||t.factors.size()>count*6*768||t.tetrahedra.size()>32768)
        throw std::invalid_argument("Indirect transport capacity/identity");
    if(t.terrain.size()!=std::size_t(t.settings.terrainResolution)*t.settings.terrainResolution)throw std::invalid_argument("Indirect terrain size");
    for(const auto& s:t.terrain) {
        if(!finite(s.normal)||!finite(s.albedo)||std::abs(glm::length(s.normal)-1)>.001f||!std::isfinite(s.height)||
           !std::isfinite(s.sky)||s.sky<0||s.sky>1)throw std::invalid_argument("Indirect terrain sample");
        for(float c:{s.albedo.x,s.albedo.y,s.albedo.z})if(c<0||c>1)throw std::invalid_argument("Indirect terrain albedo");
    }
    if(!t.terrainMaterials.empty()&&t.terrainMaterials.size()!=t.terrain.size())throw std::invalid_argument("Indirect terrain material count");
    for(const auto& s:t.terrainMaterials) {
        for(float v:{s.roughness,s.specular,s.dominantWeight,s.selectedWeight})
            if(!std::isfinite(v)||v<0||v>1)throw std::invalid_argument("Indirect terrain material range");
        if(s.dominantLayer>=32)throw std::invalid_argument("Indirect terrain dominant layer");
    }
    const auto& dims=t.settings.counts;
    if(t.acceleration.size()!=std::size_t(dims[0]-1)*(dims[1]-1)*(dims[2]-1)||t.tetrahedra.empty())
        throw std::invalid_argument("Indirect acceleration extent");
    std::size_t expected=0;
    for(const auto& b:t.bricks) {
        if(!finite(b.center)||!b.count||b.first!=expected||std::size_t(b.first)+b.count>t.surfels.size())
            throw std::invalid_argument("Indirect brick ownership");
        expected+=b.count;
    }
    if(expected!=t.surfels.size())throw std::invalid_argument("Unowned indirect surfels");
    for(const auto& s:t.surfels) {
        if(!finite(s.position)||!finite(s.normal)||!finite(s.albedo)||!finite(s.emission)||
           std::abs(glm::length(s.normal)-1)>.001f||s.closestProbe>=count||s.channels>255||
           s.emissionChannel< -1||s.emissionChannel>=32)
            throw std::invalid_argument("Indirect surfel");
        for(int a=0;a<3;++a)if(s.albedo[a]<0||s.albedo[a]>1||s.emission[a]<0||s.emission[a]>64512)
            throw std::invalid_argument("Indirect material energy");
    }
    for(const auto& p:t.probes) {
        if(!finite(p.position)||p.volume!=t.settings.volume)throw std::invalid_argument("Indirect probe position/volume");
        for(int a=0;a<6;++a) {
            if(std::size_t(p.first[a])+p.count[a]>t.factors.size()||p.count[a]>768||
               !std::isfinite(p.sky[a])||p.sky[a]<0||p.sky[a]>1)throw std::invalid_argument("Indirect factors/sky");
            double energy=p.sky[a];
            for(unsigned n=0;n<p.count[a];++n) {
                const auto& f=t.factors[p.first[a]+n];
                if(f.brick>=t.bricks.size()||!std::isfinite(f.weight)||f.weight<=0||f.weight>1)
                    throw std::invalid_argument("Indirect factor");
                energy+=f.weight;
            }
            if(energy>1.001)throw std::invalid_argument("Indirect factor energy exceeds hemisphere");
        }
    }
    for(const auto& tet:t.tetrahedra) {
        std::set<unsigned> unique;
        for(unsigned n=0;n<4;++n) {
            if(tet.probes[n]>=count||!unique.insert(tet.probes[n]).second||
               tet.neighbor[n]< -1||tet.neighbor[n]>=int(t.tetrahedra.size()))
                throw std::invalid_argument("Indirect tetra ownership");
        }
        const auto anchor=t.probes[tet.probes[3]].position;
        const glm::mat3 basis(t.probes[tet.probes[0]].position-anchor,t.probes[tet.probes[1]].position-anchor,
                              t.probes[tet.probes[2]].position-anchor);
        if(std::abs(glm::determinant(basis))<1e-6f)throw std::invalid_argument("Degenerate indirect tetra");
        for(unsigned a=0;a<3;++a) {
            if(!finite(tet.inverse[a]))throw std::invalid_argument("Invalid inverse tetra");
            for(unsigned b=0;b<3;++b)
                if(std::abs(glm::dot(tet.inverse[a],basis[b])-(a==b?1.f:0.f))>.001f)
                    throw std::invalid_argument("Stale inverse tetra");
        }
    }
    for(unsigned ti=0;ti<t.tetrahedra.size();++ti)for(unsigned face=0;face<4;++face) {
        const auto& tet=t.tetrahedra[ti];const int other=tet.neighbor[face];if(other<0)continue;
        if(other==int(ti))throw std::invalid_argument("Self-referencing indirect tetra");
        std::array<unsigned,3> shared{};unsigned n=0;for(unsigned a=0;a<4;++a)if(a!=face)shared[n++]=tet.probes[a];
        std::sort(shared.begin(),shared.end());bool reciprocal=false;
        for(unsigned f=0;f<4;++f)if(t.tetrahedra[other].neighbor[f]==int(ti)) {
            std::array<unsigned,3> opposite{};n=0;for(unsigned a=0;a<4;++a)if(a!=f)opposite[n++]=t.tetrahedra[other].probes[a];
            std::sort(opposite.begin(),opposite.end());reciprocal|=shared==opposite;
        }
        if(!reciprocal)throw std::invalid_argument("Nonreciprocal indirect tetra");
    }
    for(unsigned cell=0;cell<t.acceleration.size();++cell) {
        const int start=t.acceleration[cell];
        if(start<0||start>=int(t.tetrahedra.size()))throw std::invalid_argument("Indirect acceleration index");
        const glm::uvec3 index(cell%(dims[0]-1),(cell/(dims[0]-1))%(dims[1]-1),cell/((dims[0]-1)*(dims[1]-1)));
        const auto low=t.settings.origin+glm::vec3(index)*t.settings.spacing;
        const auto high=low+t.settings.spacing;
        for(auto probe:t.tetrahedra[start].probes) {
            const auto p=t.probes[probe].position;
            for(unsigned a=0;a<3;++a)if(p[a]<low[a]-.001f||p[a]>high[a]+.001f)
                throw std::invalid_argument("Indirect acceleration points outside its cell");
        }
    }
}
glm::vec3 indirectAxisSample(const IndirectAxes& a,glm::vec3 n) {
    glm::vec3 sum(0);
    for(unsigned i=0;i<3;++i)sum+=a[i*2+(n[i]<0?1:0)]*(n[i]*n[i]);
    return sum;
}
glm::vec4 indirectBarycentric(const IndirectTransport& t,const IndirectTetra& tet,glm::vec3 p) {
    p-=t.probes[tet.probes[3]].position;
    const glm::vec3 b(glm::dot(tet.inverse[0],p),glm::dot(tet.inverse[1],p),glm::dot(tet.inverse[2],p));
    return {b,1-b.x-b.y-b.z};
}
std::array<glm::vec3,15> indirectVisibilitySamples(glm::vec3 a,glm::vec3 b,glm::vec3 c) {
    std::array<glm::vec3,15> result{};unsigned i=0;
    for(unsigned y=0;y<=4;++y)for(unsigned x=0;x<=4-y;++x)
        result[i++]=(a*float(4-x-y)+b*float(x)+c*float(y))*.25f;
    return result;
}
float indirectVisibleDistance(glm::vec3 p,std::span<const std::uint8_t,16> depths,
        glm::vec3 origin,glm::vec3 a,glm::vec3 b,glm::vec3 c,float bias) {
    if(depths[15])return 100;
    const auto ray=p-origin,n=glm::cross(b-a,c-a);
    const float denom=glm::dot(n,ray);
    if(glm::dot(ray,ray)<1e-10f)return 0;
    if(std::abs(denom)<1e-10f)return 100;
    const auto projection=origin+ray*(glm::dot(n,a-origin)/denom);
    const glm::vec3 v=b-a,w=c-a,q=projection-a;
    const float vv=glm::dot(v,v),ww=glm::dot(w,w),vw=glm::dot(v,w),det=vv*ww-vw*vw;
    glm::vec2 uv((ww*glm::dot(q,v)-vw*glm::dot(q,w))/det,(vv*glm::dot(q,w)-vw*glm::dot(q,v))/det);
    uv=glm::max(uv,glm::vec2(0));uv/=std::max(1.f,uv.x+uv.y);
    // Clamp boundary coordinates inward so the last row never reads beyond the 15 samples.
    uv*=.999999f;
    const unsigned x=unsigned(uv.x*4),y=unsigned(uv.y*4),base=(11-y)*y/2+x,stride=5-y;
    std::array<unsigned,3> idx{base+1,base+stride,base};
    if((uv.x*4-x)+(uv.y*4-y)>1)idx={base+stride,base+1,base+stride+1};
    const auto samples=indirectVisibilitySamples(a,b,c);
    glm::vec3 points[3];
    for(unsigned i=0;i<3;++i)points[i]=origin+(samples[std::min(idx[i],14u)]-origin)*(float(depths[std::min(idx[i],14u)])/255.f);
    const auto cross=glm::cross(points[1]-points[0],points[2]-points[0]);
    if(glm::dot(cross,cross)<1e-16f)return 100;
    const auto surfaceNormal=glm::normalize(cross);
    const float plane=glm::dot(surfaceNormal,ray);
    if(std::abs(plane)<1e-10f)return 100;
    const float fraction=glm::dot(surfaceNormal,points[0]-origin)/plane;
    const float distance=glm::length(ray);
    const float offset=glm::clamp(glm::dot(surfaceNormal,ray/distance),0.f,1.f)*bias-bias;
    return glm::clamp(distance-offset-std::abs(fraction)*distance,0.f,100.f);
}
IndirectEvaluation evaluateIndirectTransport(const IndirectTransport& t,std::span<const IndirectAxes> colors,
        glm::vec3 p,glm::vec3 normal,const IndirectControls& controls) {
    validateIndirectControls(controls);
    if(colors.size()!=t.probes.size()||!finite(p)||!finite(normal)||glm::dot(normal,normal)<1e-10f)
        throw std::invalid_argument("Indirect receiver");
    normal=glm::normalize(normal);IndirectEvaluation result;
    const auto& s=t.settings;const auto grid=(p-s.origin)/s.spacing;
    glm::ivec3 cell;
    for(unsigned a=0;a<3;++a){if(grid[a]<0||grid[a]>float(s.counts[a]-1))return result;cell[a]=std::min(int(grid[a]),int(s.counts[a])-2);}
    int index=t.acceleration[(cell.z*(s.counts[1]-1)+cell.y)*(s.counts[0]-1)+cell.x],last=-1;
    glm::vec4 bary(0);
    for(unsigned step=0;step<16;++step) {
        result.steps=step+1;
        if(index<0||index>=int(t.tetrahedra.size()))return result;
        bary=indirectBarycentric(t,t.tetrahedra[index],p);
        unsigned smallest=0;for(unsigned a=1;a<4;++a)if(bary[a]<bary[smallest])smallest=a;
        const int next=t.tetrahedra[index].neighbor[smallest];
        if(bary[smallest]>=-1e-6f||(next>=0&&next==last)){result.tetra=index;break;}
        last=index;index=next;
    }
    if(result.tetra<0)return result;
    const auto& tet=t.tetrahedra[result.tetra];glm::vec4 score;
    for(unsigned a=0;a<4;++a) {
        const auto origin=t.probes[tet.probes[a]].position;
        std::array<glm::vec3,3> face{};unsigned f=0;for(unsigned b=0;b<4;++b)if(a!=b)face[f++]=t.probes[tet.probes[b]].position;
        const auto delta=origin-p;const float d=glm::length(delta);
        const float visibility=indirectVisibleDistance(p,tet.visibility[a],origin,face[0],face[1],face[2],controls.visibilityBias);
        score[a]=visibility*controls.shadowDistanceWeight-(d>1e-6f?glm::dot(normal,delta/d):0)*controls.normalWeight+d*controls.probeDistanceWeight;
    }
    const float best=std::min({score.x,score.y,score.z,score.w});float sum=.0001f;
    for(unsigned a=0;a<4;++a) {
        const float w=glm::clamp(bary[a],0.f,1.f)*glm::clamp(1-std::abs(score[a]-best)/controls.threshold,0.f,1.f);
        result.radiance+=indirectAxisSample(colors[tet.probes[a]],normal)*w;sum+=w;
    }
    result.radiance/=sum;result.weight=1;
    return result;
}
void buildIndirectTetrahedra(IndirectTransport& t) {
    const auto& s=t.settings;const auto count=gridCount(s);
    if(t.probes.size()!=count)throw std::invalid_argument("Probe lattice missing");
    t.tetrahedra.clear();t.acceleration.clear();
    constexpr unsigned perm[6][3]{{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
    const auto id=[&](glm::uvec3 p){return (p.z*s.counts[1]+p.y)*s.counts[0]+p.x;};
    for(unsigned z=0;z+1<s.counts[2];++z)for(unsigned y=0;y+1<s.counts[1];++y)for(unsigned x=0;x+1<s.counts[0];++x) {
        t.acceleration.push_back(int(t.tetrahedra.size()));const glm::uvec3 start(x,y,z);
        for(const auto& order:perm) {
            IndirectTetra tet;glm::uvec3 v=start;tet.probes[0]=id(v);
            for(unsigned n=0;n<3;++n){++v[order[n]];tet.probes[n+1]=id(v);}
            const auto anchor=t.probes[tet.probes[3]].position;
            const auto inverse=glm::inverse(glm::mat3(t.probes[tet.probes[0]].position-anchor,t.probes[tet.probes[1]].position-anchor,t.probes[tet.probes[2]].position-anchor));
            for(unsigned n=0;n<3;++n)tet.inverse[n]={inverse[0][n],inverse[1][n],inverse[2][n]};
            for(auto& vis:tet.visibility){vis.fill(255);vis[15]=0;}t.tetrahedra.push_back(tet);
        }
    }
    std::map<std::array<unsigned,3>,std::pair<unsigned,unsigned>> faces;
    for(unsigned n=0;n<t.tetrahedra.size();++n)for(unsigned opposite=0;opposite<4;++opposite) {
        std::array<unsigned,3> key{};unsigned i=0;
        for(unsigned a=0;a<4;++a)if(a!=opposite)key[i++]=t.tetrahedra[n].probes[a];
        std::sort(key.begin(),key.end());
        auto [it,inserted]=faces.emplace(key,std::pair(n,opposite));
        if(!inserted){const auto [other,face]=it->second;t.tetrahedra[n].neighbor[opposite]=int(other);t.tetrahedra[other].neighbor[face]=int(n);}
    }
}
IndirectGpuData packIndirectTransport(const IndirectTransport& t) {
    validateIndirectTransport(t);IndirectGpuData result;auto& r=result.rows;r.resize(9);
    result.probeOffset=unsigned(r.size());
    for(const auto& p:t.probes){r.push_back(row(p.position,float(p.volume)));for(unsigned a=0;a<6;++a)r.push_back({float(p.first[a]),float(p.count[a]),p.sky[a],0});}
    result.tetraOffset=unsigned(r.size());
    for(const auto& tet:t.tetrahedra) {
        r.push_back({float(tet.probes[0]),float(tet.probes[1]),float(tet.probes[2]),float(tet.probes[3])});
        r.push_back({float(tet.neighbor[0]),float(tet.neighbor[1]),float(tet.neighbor[2]),float(tet.neighbor[3])});
        for(const auto& inv:tet.inverse)r.push_back(row(inv));r.push_back(row(t.probes[tet.probes[3]].position));
        for(const auto& v:tet.visibility)for(unsigned i=0;i<4;++i)r.push_back({v[i*4]/255.f,v[i*4+1]/255.f,v[i*4+2]/255.f,v[i*4+3]/255.f});
    }
    result.accelerationOffset=unsigned(r.size());for(int i:t.acceleration)r.push_back({float(i),0,0,0});
    result.surfelOffset=unsigned(r.size());
    for(const auto& s:t.surfels) {
        r.push_back(row(s.position,float(s.closestProbe)));
        const auto n=glm::packSnorm4x8(glm::vec4(s.normal,0));
        const auto albedo=glm::packUnorm4x8(glm::vec4(glm::sqrt(s.albedo),0));
        const auto emission=glm::packF3x9_E1x5(s.emission);
        r.push_back({packed(n),packed(albedo),packed(emission),float(unsigned(s.emissionChannel+1)|(s.channels<<6)|(s.permanentShadow?1u<<14:0))});
    }
    result.brickOffset=unsigned(r.size());
    for(const auto& b:t.bricks){r.push_back(row(b.center,float(b.first)));r.push_back({float(b.count),0,0,0});}
    result.factorOffset=unsigned(r.size());for(const auto& f:t.factors)r.push_back({float(f.brick),f.weight,0,0});
    result.terrainOffset=unsigned(r.size());
    for(const auto& v:t.terrain){r.push_back(row(v.normal,v.height));r.push_back(row(v.albedo,v.sky));}
    const auto materialOffset=unsigned(r.size());
    for(std::size_t i=0;i<t.terrain.size();++i){
        const auto v=t.terrainMaterials.empty()?IndirectTerrainMaterial{}:t.terrainMaterials[i];
        r.push_back({v.roughness,v.specular,v.dominantWeight,v.selectedWeight});
        r.push_back({float(v.dominantLayer),0,0,0});
    }
    result.probeOutput=0;result.brickOutput=unsigned(t.probes.size()*6);
    result.shadowOutput=result.brickOutput+unsigned(t.bricks.size());
    result.ambientOutput=result.shadowOutput+unsigned(t.surfels.size());
    result.terrainOutput=result.ambientOutput+30+68;
    result.outputRows=result.terrainOutput+unsigned(t.terrain.size()*3);
    const auto& s=t.settings;
    r[0]={float(t.probes.size()),float(t.bricks.size()),float(t.surfels.size()),float(t.tetrahedra.size())};
    r[1]={float(result.probeOffset),float(result.tetraOffset),float(result.accelerationOffset),float(result.surfelOffset)};
    r[2]={float(result.brickOffset),float(result.factorOffset),float(result.brickOutput),float(result.shadowOutput)};
    r[3]=row(s.origin,s.fadeDistance);r[4]=row(s.spacing,float(s.volume));
    r[5]={float(s.counts[0]),float(s.counts[1]),float(s.counts[2]),float(result.ambientOutput)};
    r[6]={float(result.outputRows),float(r.size()),0,0};
    r[7]={float(result.terrainOffset),float(s.terrainResolution),float(result.terrainOutput),s.terrainSpacing};
    r[8]={s.terrainOrigin.x,s.terrainOrigin.y,float(materialOffset),0};
    return result;
}
PreparedIndirectTransport::PreparedIndirectTransport(std::shared_ptr<const IndirectTransport> source)
    : source_(std::move(source)) {
    if(!source_)throw std::invalid_argument("Missing indirect transport");
    data_=packIndirectTransport(*source_);
}
const IndirectGpuData& PreparedIndirectTransport::dataFor(
    const std::shared_ptr<const IndirectTransport>& source) const {
    if(source!=source_)throw std::invalid_argument("Prepared indirect transport does not match source");
    return data_;
}
std::vector<unsigned> scheduleIndirectLighting(std::span<const IndirectScheduleCandidate> candidates,unsigned budget,unsigned capacity) {
    if(!budget||!capacity||capacity>8||candidates.size()>256)throw std::invalid_argument("Indirect schedule budget");
    std::vector<unsigned> resident;std::set<std::uint64_t> identities;
    const auto distance=[&](unsigned i) {
        const auto& u=*candidates[i].update;const auto& s=u.transport->settings;
        glm::vec3 low=s.origin,high=low+s.spacing*glm::vec3(s.counts[0]-1,s.counts[1]-1,s.counts[2]-1);
        glm::vec3 camera(u.camera[0],u.camera[1],u.camera[2]),delta=camera-glm::clamp(camera,low,high);
        return glm::dot(delta,delta);
    };
    for(unsigned i=0;i<candidates.size();++i) {
        const auto* u=candidates[i].update;
        if(!u||!u->transport||!u->identity||!identities.insert(u->identity).second)throw std::invalid_argument("Indirect schedule identity");
        gridCount(u->transport->settings);
        for(auto c:u->camera)if(!std::isfinite(c))throw std::invalid_argument("Indirect schedule camera");
        for(auto c:u->viewProjection)if(!std::isfinite(c))throw std::invalid_argument("Indirect schedule projection");
        resident.push_back(i);
    }
    std::stable_sort(resident.begin(),resident.end(),[&](unsigned a,unsigned b){return distance(a)<distance(b);});
    if(resident.size()>capacity)resident.resize(capacity);
    const auto visible=[&](unsigned index) {
        const auto& c=candidates[index];const auto& u=*c.update;
        if(u.reset||c.generation<5||u.viewProjection==std::array<float,16>{})return true;
        const auto& s=u.transport->settings;const auto low=s.origin-glm::vec3(16);
        const auto high=s.origin+s.spacing*glm::vec3(s.counts[0]-1,s.counts[1]-1,s.counts[2]-1)+glm::vec3(16);
        std::array<unsigned,6> outside{};
        for(unsigned corner=0;corner<8;++corner) {
            const glm::vec3 p(corner&1?high.x:low.x,corner&2?high.y:low.y,corner&4?high.z:low.z);
            glm::vec4 clip(0);
            for(unsigned row=0;row<4;++row)clip[row]=u.viewProjection[row]*p.x+u.viewProjection[4+row]*p.y+u.viewProjection[8+row]*p.z+u.viewProjection[12+row];
            for(unsigned axis=0;axis<3;++axis){outside[axis*2]+=clip[axis]<-clip.w;outside[axis*2+1]+=clip[axis]>clip.w;}
        }
        return std::none_of(outside.begin(),outside.end(),[](unsigned n){return n==8;});
    };
    resident.erase(std::remove_if(resident.begin(),resident.end(),[&](unsigned i){return !visible(i);}),resident.end());
    std::stable_sort(resident.begin(),resident.end(),[&](unsigned a,unsigned b) {
        const auto& x=candidates[a];const auto& y=candidates[b];
        const auto ageX=x.update->reset?0:x.lastRelight,ageY=y.update->reset?0:y.lastRelight;
        return ageX<ageY;
    });
    if(resident.size()>budget)resident.resize(budget);
    return resident;
}
void writeIndirectTransport(const std::filesystem::path& path,const IndirectTransport& t) {
    validateIndirectTransport(t);nlohmann::json j;
    auto vec=[](glm::vec3 v){return std::array{v.x,v.y,v.z};};
    j["schema"]="phlosion-indirect-transport-v3";j["scene"]=t.sceneIdentity;
    const auto& s=t.settings;j["grid"]={vec(s.origin),vec(s.spacing),s.counts,s.faceResolution,s.volume,s.maxDistance,s.fadeDistance,s.surfelSpacing};
    j["emission_channels"]=s.materialEmissionChannels;
    j["emission_factors"]=nlohmann::json::array();for(auto v:s.materialEmissionFactors)j["emission_factors"].push_back(vec(v));
    j["terrain_grid"]={s.terrainResolution,std::array{s.terrainOrigin.x,s.terrainOrigin.y},s.terrainSpacing};
    j["terrain"]=indirect_codec::encode(t.terrain,32);
    j["terrain_materials"]=indirect_codec::encode(t.terrainMaterials,20);
    j["surfels"]=indirect_codec::encode(t.surfels,61);
    j["bricks"]=indirect_codec::encode(t.bricks,20);
    j["probes"]=indirect_codec::encode(t.probes,88);
    j["factors"]=indirect_codec::encode(t.factors,8);
    j["tetrahedra"]=indirect_codec::encode(t.tetrahedra,132);
    j["acceleration"]=indirect_codec::encode(t.acceleration,4);
    const auto bytes=nlohmann::json::to_cbor(j);const auto temp=path.string()+".tmp";
    {std::ofstream out(temp,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));if(!out)throw std::runtime_error("Cannot write indirect transport");}
    std::filesystem::rename(temp,path);
}
static IndirectTransport decodeIndirectTransport(const std::vector<std::uint8_t>&);
IndirectTransport readIndirectTransport(const std::filesystem::path& path) {
    engine::assets::ResourceLoadContext context;
    return decodeIndirectTransport(context.read(path,128ull<<20));
}
std::shared_ptr<const IndirectTransport> acquireIndirectTransport(const std::filesystem::path& path) {
    auto* bank=engine::assets::currentResourceBank();
    if(!bank)return std::make_shared<const IndirectTransport>(readIndirectTransport(path));
    const auto canonical=engine::assets::canonicalResourcePath(path);
    return bank->acquire<IndirectTransport>("indirect:"+canonical,
        [canonical](engine::assets::ResourceLoadContext& context){
            auto value=std::make_shared<const IndirectTransport>(decodeIndirectTransport(context.read(canonical,128ull<<20)));
            const auto vectorBytes=[](const auto& v){return v.capacity()*sizeof(typename std::decay_t<decltype(v)>::value_type);};
            const auto bytes=sizeof(IndirectTransport)+vectorBytes(value->terrain)+vectorBytes(value->terrainMaterials)+
                vectorBytes(value->surfels)+vectorBytes(value->bricks)+vectorBytes(value->probes)+vectorBytes(value->factors)+
                vectorBytes(value->tetrahedra)+vectorBytes(value->acceleration)+vectorBytes(value->settings.materialEmissionChannels)+
                vectorBytes(value->settings.materialEmissionFactors)+value->sceneIdentity.capacity();
            return engine::assets::ResourceValue<IndirectTransport>{value,bytes};
        });
}
static IndirectTransport decodeIndirectTransport(const std::vector<std::uint8_t>& bytes) {
    auto j=nlohmann::json::from_cbor(bytes);const bool extended=j.at("schema")=="phlosion-indirect-transport-v3";
    const bool blocks=extended||j.at("schema")=="phlosion-indirect-transport-v2";
    if(!blocks && j.at("schema")!="phlosion-indirect-transport-v1")throw std::runtime_error("Unknown indirect transport format");
    auto vec=[](const nlohmann::json& a){auto v=a.get<std::array<float,3>>();return glm::vec3(v[0],v[1],v[2]);};
    IndirectTransport t;t.sceneIdentity=j.at("scene").get<std::string>();auto& s=t.settings;auto& g=j.at("grid");
    s.origin=vec(g.at(0));s.spacing=vec(g.at(1));g.at(2).get_to(s.counts);g.at(3).get_to(s.faceResolution);g.at(4).get_to(s.volume);
    g.at(5).get_to(s.maxDistance);g.at(6).get_to(s.fadeDistance);g.at(7).get_to(s.surfelSpacing);
    if(j.contains("terrain_grid")) {
        auto& d=j.at("terrain_grid");s.terrainResolution=d.at(0).get<unsigned>();
        auto xy=d.at(1).get<std::array<float,2>>();s.terrainOrigin={xy[0],xy[1]};s.terrainSpacing=d.at(2).get<float>();
    }
    if(j.contains("emission_channels"))j.at("emission_channels").get_to(s.materialEmissionChannels);
    if(j.contains("emission_factors"))for(const auto& v:j.at("emission_factors"))s.materialEmissionFactors.push_back(vec(v));
    const auto count=gridCount(s);
    if(blocks) {
        indirect_codec::decode(j.at("terrain"),t.terrain,32,std::size_t(s.terrainResolution)*s.terrainResolution);
        if(extended)indirect_codec::decode(j.at("terrain_materials"),t.terrainMaterials,20,t.terrain.size());
        indirect_codec::decode(j.at("surfels"),t.surfels,61,1u<<21);
        indirect_codec::decode(j.at("bricks"),t.bricks,20,32768);
        indirect_codec::decode(j.at("probes"),t.probes,88,count);
        indirect_codec::decode(j.at("factors"),t.factors,8,count*6*768);
        indirect_codec::decode(j.at("tetrahedra"),t.tetrahedra,132,32768);
        indirect_codec::decode(j.at("acceleration"),t.acceleration,4,count);
        validateIndirectTransport(t);return t;
    }
    for(auto [name,limit]:std::array<std::pair<const char*,std::size_t>,6>{{
        {"surfels",1u<<21},{"bricks",32768},{"probes",count},{"factors",count*6*768},{"tetrahedra",32768},{"acceleration",count}}})
        if(j.at(name).size()>limit)throw std::runtime_error("Cooked indirect count limit");
    if(j.contains("terrain")) {
        if(j.at("terrain").size()!=std::size_t(s.terrainResolution)*s.terrainResolution)throw std::runtime_error("Cooked terrain count");
        for(const auto& v:j.at("terrain"))t.terrain.push_back({vec(v.at(0)),vec(v.at(1)),v.at(2).get<float>(),v.at(3).get<float>()});
    }
    for(const auto& v:j.at("surfels")){IndirectSurfel a;a.position=vec(v.at(0));a.normal=vec(v.at(1));a.albedo=vec(v.at(2));a.emission=vec(v.at(3));v.at(4).get_to(a.closestProbe);v.at(5).get_to(a.channels);v.at(6).get_to(a.emissionChannel);v.at(7).get_to(a.permanentShadow);t.surfels.push_back(a);}
    for(const auto& v:j.at("bricks"))t.bricks.push_back({vec(v.at(0)),v.at(1).get<unsigned>(),v.at(2).get<unsigned>()});
    for(const auto& v:j.at("probes")){IndirectProbe a;a.position=vec(v.at(0));v.at(1).get_to(a.first);v.at(2).get_to(a.count);v.at(3).get_to(a.sky);v.at(4).get_to(a.volume);t.probes.push_back(a);}
    for(const auto& v:j.at("factors"))t.factors.push_back({v.at(0).get<unsigned>(),v.at(1).get<float>()});
    for(const auto& v:j.at("tetrahedra")){IndirectTetra a;v.at(0).get_to(a.probes);v.at(1).get_to(a.neighbor);for(unsigned i=0;i<3;++i)a.inverse[i]=vec(v.at(2).at(i));v.at(3).get_to(a.visibility);t.tetrahedra.push_back(a);}
    j.at("acceleration").get_to(t.acceleration);validateIndirectTransport(t);return t;
}
}
