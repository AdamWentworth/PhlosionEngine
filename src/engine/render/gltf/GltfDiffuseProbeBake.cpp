#include "engine/render/WorldDiffuseProbes.h"
#include "engine/render/WorldIndirectLighting.h"
#include "engine/render/WorldLayeredSampling.h"
#include <optional>
#include <set>
#include <map>
#include <tuple>
#include <iostream>
#include "engine/render/gltf/GltfSceneAsset.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace engine::render {
namespace {
using namespace gltf::scene;
constexpr float pi=3.14159265358979323846f;
glm::vec3 vec(const std::array<float,3>& a) {return {a[0],a[1],a[2]};}
struct Triangle {
    glm::vec3 a,b,c,normal,lo,hi;
    const GltfScenePrimitive* primitive;
    std::array<std::uint32_t,3> indices;
};
struct Node {glm::vec3 lo,hi;std::size_t begin=0,end=0,left=0,right=0;};
struct Hit {float distance;std::size_t triangle=0;float u=0,v=0;bool found=false;mutable std::optional<WorldLayerSurface> layered;};
float linear(float value) {return value<=.04045f?value/12.92f:std::pow((value+.055f)/1.055f,2.4f);}
int wrap(int i,int size,int mode) {
    if(mode==33071) return std::clamp(i,0,size-1);
    if(mode==33648) {const int p=(i%(size*2)+size*2)%(size*2);return p<size?p:size*2-1-p;}
    return (i%size+size)%size;
}
glm::vec4 sample(const GltfSceneTexture& texture,glm::vec2 uv,bool srgb) {
    if(texture.empty()) return glm::vec4(1);
    const float x=uv.x*static_cast<float>(texture.width)-.5f,y=uv.y*static_cast<float>(texture.height)-.5f;
    const int ix=static_cast<int>(std::floor(x)),iy=static_cast<int>(std::floor(y));
    const auto texel=[&](int dx,int dy) {
        const auto offset=static_cast<std::size_t>((wrap(iy+dy,texture.height,texture.wrapT)*texture.width+
            wrap(ix+dx,texture.width,texture.wrapS))*4);
        glm::vec4 v;for(int c=0;c<4;++c) v[c]=static_cast<float>(texture.rgba[offset+static_cast<std::size_t>(c)])/255.f;
        if(srgb) for(int c=0;c<3;++c) v[c]=linear(v[c]);return v;
    };
    return glm::mix(glm::mix(texel(0,0),texel(1,0),x-std::floor(x)),
                    glm::mix(texel(0,1),texel(1,1),x-std::floor(x)),y-std::floor(y));
}
class Scene {
public:
    const GltfSceneAsset& asset;
    std::vector<Triangle> triangles;
    std::vector<std::size_t> order;
    std::vector<Node> nodes;
    std::vector<glm::mat3> normalTransforms,tangentTransforms;
    explicit Scene(const GltfSceneAsset& input):asset(input) {
        for(const auto& node:asset.nodes) {
            const auto m=glm::mat3(node.defaultGlobal);
            tangentTransforms.push_back(m);
            normalTransforms.push_back(std::abs(glm::determinant(m))>1e-10f?glm::transpose(glm::inverse(m)):glm::mat3(1));
        }
        std::set<int> lowerDetail;
        for(const auto& group:asset.lodGroups)for(std::size_t i=1;i<group.nodes.size();++i)lowerDetail.insert(group.nodes[i]);
        for(const auto& p:asset.primitives) {
            if(lowerDetail.contains(p.nodeIndex))continue;
            if(p.skinIndex>=0) continue;
            if(p.nodeIndex<0||static_cast<std::size_t>(p.nodeIndex)>=asset.nodes.size()||
               p.materialIndex<0||static_cast<std::size_t>(p.materialIndex)>=asset.materials.size()||p.indices.size()%3!=0)
                throw std::invalid_argument("Invalid static scene for diffuse bake");
            if(asset.materials[static_cast<std::size_t>(p.materialIndex)].alphaMode==2) continue;
            const auto& matrix=asset.nodes[static_cast<std::size_t>(p.nodeIndex)].defaultGlobal;
            for(std::size_t i=0;i<p.indices.size();i+=3) {
                Triangle t{};t.primitive=&p;
                glm::vec3 positions[3];
                for(std::size_t k=0;k<3;++k) {
                    t.indices[k]=p.indices[i+k];const auto& v=p.vertices.at(t.indices[k]);
                    positions[k]=glm::vec3(matrix*glm::vec4(v.x,v.y,v.z,1));
                    for(int a=0;a<3;++a) if(!std::isfinite(positions[k][a])) throw std::invalid_argument("Nonfinite bake geometry");
                }
                t.a=positions[0];t.b=positions[1];t.c=positions[2];
                const auto n=glm::cross(t.b-t.a,t.c-t.a);
                if(glm::dot(n,n)<1e-16f) continue;
                t.normal=glm::normalize(n)*(glm::determinant(glm::mat3(matrix))<0?-1.f:1.f);t.lo=glm::min(t.a,glm::min(t.b,t.c));t.hi=glm::max(t.a,glm::max(t.b,t.c));
                triangles.push_back(t);
            }
        }
        if(triangles.empty()) throw std::invalid_argument("Diffuse bake has no static opaque geometry");
        order.resize(triangles.size());std::iota(order.begin(),order.end(),0u);build(0,order.size());
    }
    const GltfSceneMaterial& material(const Triangle& t) const {return asset.materials[static_cast<std::size_t>(t.primitive->materialIndex)];}
    glm::vec2 uv(const Triangle& t,const Hit& h) const {
        const auto& a=t.primitive->vertices[t.indices[0]];const auto& b=t.primitive->vertices[t.indices[1]];
        const auto& c=t.primitive->vertices[t.indices[2]];
        return glm::vec2(a.u,a.v)*(1-h.u-h.v)+glm::vec2(b.u,b.v)*h.u+glm::vec2(c.u,c.v)*h.v;
    }
    const WorldLayerSurface* layered(const Triangle& t,const Hit& h) const {
        const auto index=material(t).layeredMaterialIndex;if(!index)return nullptr;
        if(h.layered)return &*h.layered;
        if(!asset.layeredMaterials)throw std::logic_error("Missing layered bake inputs");
        glm::vec3 n(0),tx(0);float handedness=0;
        const float weights[]{1-h.u-h.v,h.u,h.v};
        for(unsigned i=0;i<3;++i){
            const auto& v=t.primitive->vertices[t.indices[i]];
            n+=glm::vec3(v.nx,v.ny,v.nz)*weights[i];tx+=glm::vec3(v.tx,v.ty,v.tz)*weights[i];handedness+=v.tw*weights[i];
        }
        n=normalTransforms[t.primitive->nodeIndex]*n;n=glm::dot(n,n)>1e-10f?glm::normalize(n):t.normal;
        tx=tangentTransforms[t.primitive->nodeIndex]*tx;tx-=n*glm::dot(n,tx);
        handedness=(handedness<0?-1.f:1.f)*(glm::determinant(tangentTransforms[t.primitive->nodeIndex])<0?-1.f:1.f);
        if(glm::dot(tx,tx)<1e-10f){
            const auto& a=t.primitive->vertices[t.indices[0]];const auto& b=t.primitive->vertices[t.indices[1]];
            const auto& c=t.primitive->vertices[t.indices[2]];
            const glm::vec2 du(b.u-a.u,b.v-a.v),dv(c.u-a.u,c.v-a.v);
            const float det=du.x*dv.y-du.y*dv.x;
            if(std::abs(det)>1e-10f){
                tx=((t.b-t.a)*dv.y-(t.c-t.a)*du.y)/det;
                const auto ty=((t.c-t.a)*du.x-(t.b-t.a)*dv.x)/det;
                tx-=n*glm::dot(n,tx);handedness=glm::dot(glm::cross(n,tx),ty)<0?-1.f:1.f;
            }
        }
        h.layered=sampleWorldLayerMaterial(*asset.layeredMaterials,index,uv(t,h),
            t.a*(1-h.u-h.v)+t.b*h.u+t.c*h.v,n,glm::vec4(tx,handedness));
        return &*h.layered;
    }
    float metal(const Triangle& t,const Hit& h) const {
        if(const auto* value=layered(t,h))return value->metalness;
        const auto& m=material(t);return std::clamp(m.metallicFactor*sample(m.metallicRoughness,uv(t,h),false).b,0.f,1.f);
    }
    IndirectTerrainMaterial properties(const Triangle& t,const Hit& h) const {
        if(const auto* v=layered(t,h))return {v->roughness,v->specular,v->dominantWeight,v->selectedWeight,v->dominantLayer};
        const auto& m=material(t);IndirectTerrainMaterial v;
        v.roughness=std::clamp(m.roughnessFactor*sample(m.metallicRoughness,uv(t,h),false).g,0.f,1.f);return v;
    }
    glm::vec4 color(const Triangle& t,const Hit& h) const {
        if(const auto* value=layered(t,h))return glm::vec4(value->color,1);
        const auto& m=material(t);glm::vec4 vertex(0);
        const float weights[]{1-h.u-h.v,h.u,h.v};
        for(int i=0;i<3;++i) {const auto& v=t.primitive->vertices[t.indices[static_cast<std::size_t>(i)]];vertex+=glm::vec4(v.r,v.g,v.b,v.a)*weights[i];}
        const auto& f=m.baseColorFactor;
        return glm::clamp(vertex*glm::vec4(f[0],f[1],f[2],f[3])*sample(m.baseColor,uv(t,h),m.baseColor.textureSrgb!=0),glm::vec4(0),glm::vec4(1));
    }
    glm::vec3 shadingNormal(const Triangle& t,const Hit& h) const {
        if(const auto* value=layered(t,h))return value->normal;
        const auto& m=material(t);glm::vec3 n(0),tangent(0);float handedness=0;
        const float weights[]{1-h.u-h.v,h.u,h.v};
        for(unsigned i=0;i<3;++i) {
            const auto& v=t.primitive->vertices[t.indices[i]];
            n+=glm::vec3(v.nx,v.ny,v.nz)*weights[i];tangent+=glm::vec3(v.tx,v.ty,v.tz)*weights[i];handedness+=v.tw*weights[i];
        }
        n=normalTransforms[t.primitive->nodeIndex]*n;
        n=glm::dot(n,n)>1e-10f?glm::normalize(n):t.normal;
        if(!m.hasNormalMap||m.normal.empty())return n;
        tangent=tangentTransforms[t.primitive->nodeIndex]*tangent;tangent-=n*glm::dot(n,tangent);
        if(glm::dot(tangent,tangent)<1e-10f)return n;
        tangent=glm::normalize(tangent);
        const float orientation=glm::determinant(tangentTransforms[t.primitive->nodeIndex])<0?-1.f:1.f;
        const auto bitangent=glm::cross(n,tangent)*(handedness<0?-1.f:1.f)*orientation;
        auto mapped=glm::vec3(sample(m.normal,uv(t,h),false))*2.f-glm::vec3(1);
        mapped.x*=m.normalScale;mapped.y*=m.normalScale;
        const auto world=tangent*mapped.x+bitangent*mapped.y+n*mapped.z;
        return glm::dot(world,world)>1e-10f?glm::normalize(world):n;
    }
    Hit trace(glm::vec3 origin,glm::vec3 direction,float maximum,int facing=0) const {
        Hit hit{maximum};visit(0,origin,direction,hit,facing);return hit;
    }
private:
    std::size_t build(std::size_t begin,std::size_t end) {
        Node n; n.begin=begin;n.end=end;n.lo=glm::vec3(std::numeric_limits<float>::max());n.hi=-n.lo;
        for(auto i=begin;i<end;++i) {const auto& t=triangles[order[i]];n.lo=glm::min(n.lo,t.lo);n.hi=glm::max(n.hi,t.hi);}
        const auto index=nodes.size();nodes.push_back(n);
        if(end-begin>8) {
            const auto extent=n.hi-n.lo;const int axis=extent.x>extent.y?(extent.x>extent.z?0:2):(extent.y>extent.z?1:2);
            const auto mid=begin+(end-begin)/2;
            std::nth_element(order.begin()+static_cast<std::ptrdiff_t>(begin),order.begin()+static_cast<std::ptrdiff_t>(mid),
                order.begin()+static_cast<std::ptrdiff_t>(end),[&](auto a,auto b) {
                    return triangles[a].lo[axis]+triangles[a].hi[axis]<triangles[b].lo[axis]+triangles[b].hi[axis];});
            const auto left=build(begin,mid),right=build(mid,end);nodes[index].left=left;nodes[index].right=right;
        }
        return index;
    }
    static bool box(const Node& n,glm::vec3 o,glm::vec3 d,float maximum) {
        float near=0,far=maximum;
        for(int a=0;a<3;++a) {
            if(std::abs(d[a])<1e-9f) {if(o[a]<n.lo[a]||o[a]>n.hi[a]) return false;continue;}
            float lo=(n.lo[a]-o[a])/d[a],hi=(n.hi[a]-o[a])/d[a];if(lo>hi) std::swap(lo,hi);
            near=std::max(near,lo);far=std::min(far,hi);if(near>far) return false;
        }
        return true;
    }
    void visit(std::size_t index,glm::vec3 o,glm::vec3 d,Hit& hit,int facing) const {
        const auto& n=nodes[index];if(!box(n,o,d,hit.distance)) return;
        if(n.left!=0) {visit(n.left,o,d,hit,facing);visit(n.right,o,d,hit,facing);return;}
        for(auto i=n.begin;i<n.end;++i) {
            const auto triangle=order[i];const auto& t=triangles[triangle];
            if(facing&&glm::dot(t.normal,d)*float(facing)>=0)continue;
            const auto e1=t.b-t.a,e2=t.c-t.a,p=glm::cross(d,e2);
            const float det=glm::dot(e1,p);if(std::abs(det)<1e-9f) continue;
            const auto delta=o-t.a;const float u=glm::dot(delta,p)/det;if(u<0||u>1) continue;
            const auto q=glm::cross(delta,e1);const float v=glm::dot(d,q)/det;if(v<0||u+v>1) continue;
            const float distance=glm::dot(e2,q)/det;if(distance<.0001f||distance>=hit.distance) continue;
            const Hit next{distance,triangle,u,v,true};const auto& m=material(t);
            if(m.alphaMode==1&&color(t,next).a<m.alphaCutoff) continue;
            hit=next;
        }
    }
};
float rectangleIrradiance(const Scene& scene,const WorldRectLight& light,glm::vec3 position,glm::vec3 normal,
    const DiffuseProbeBakeSettings& settings) {
    const auto center=vec(light.position),u=vec(light.halfU),v=vec(light.halfV);
    const auto cross=glm::cross(u,v);const float area=4*glm::length(cross);const auto outward=glm::normalize(cross);
    const auto fromCenter=position-center;
    const float centerDistance2=glm::dot(fromCenter,fromCenter),range2=light.range*light.range;
    if(glm::dot(outward,fromCenter)<=0||centerDistance2>=range2) return 0;
    const float attenuation=std::max(0.f,1-centerDistance2*centerDistance2/(range2*range2));
    double sum=0;
    for(int x=0;x<settings.areaSamplesU;++x) for(int y=0;y<settings.areaSamplesV;++y) {
        const auto source=center+u*(2*(static_cast<float>(x)+.5f)/static_cast<float>(settings.areaSamplesU)-1)+
            v*(2*(static_cast<float>(y)+.5f)/static_cast<float>(settings.areaSamplesV)-1);
        const auto delta=source-position;const float distance2=glm::dot(delta,delta);
        if(distance2<.0001f) continue;
        const auto direction=delta/std::sqrt(distance2);
        const float incoming=std::max(0.f,glm::dot(normal,direction)),emitted=std::max(0.f,-glm::dot(outward,direction));
        if(incoming<=0||emitted<=0) continue;
        const auto start=position+normal*.004f,end=source+outward*.012f,segment=end-start;
        const float length=glm::length(segment);
        if(scene.trace(start,segment/length,std::max(0.f,length-.002f)).found) continue;
        sum+=static_cast<double>(incoming*emitted/distance2);
    }
    return static_cast<float>(sum)*area*attenuation*attenuation/static_cast<float>(settings.areaSamplesU*settings.areaSamplesV);
}
}
DiffuseProbeTransport bakeDiffuseProbes(const gltf::scene::GltfSceneAsset& asset,std::span<const WorldRectLight> lights,
    const DiffuseProbeBakeSettings& settings) {
    const auto count=validateDiffuseGrid(settings.grid);
    if(lights.empty()||lights.size()>16||settings.rayCount<64||settings.rayCount>65536||
       settings.areaSamplesU<1||settings.areaSamplesU>128||settings.areaSamplesV<1||settings.areaSamplesV>128||
       !std::isfinite(settings.maxDistance)||settings.maxDistance<1||settings.maxDistance>1000)
        throw std::invalid_argument("Invalid diffuse bake settings");
    for(auto light:lights) {
        WorldEnvironmentLighting e;e.enabled=true;light.radiance={1,1,1};e.rectLights[0]=light;
        if(packWorldRectLights(e)[0][3]<=0) throw std::invalid_argument("Invalid diffuse bake rectangle");
    }
    Scene scene(asset);DiffuseProbeTransport result;
    result.sceneIdentity=diffuseSceneIdentity(asset);result.settings=settings;result.sources.assign(lights.begin(),lights.end());
    for(auto& l:result.sources) l.radiance={1,1,1};
    result.transfer.resize(count*lights.size());result.distanceMoments.resize(count);result.validity.assign(count,1);
    const auto& g=settings.grid;
    for(std::size_t p=0;p<count;++p) {
        const int x=static_cast<int>(p)%g.counts[0],y=(static_cast<int>(p)/g.counts[0])%g.counts[1];
        const int z=static_cast<int>(p)/(g.counts[0]*g.counts[1]);
        const auto origin=vec(g.origin)+glm::vec3(x,y,z)*vec(g.spacing);
        std::array<int,6> counts{};std::array<std::array<double,2>,6> moments{};float nearest=settings.maxDistance;
        for(int ray=0;ray<settings.rayCount;++ray) {
            const float sy=1-2*(static_cast<float>(ray)+.5f)/static_cast<float>(settings.rayCount);
            const float radius=std::sqrt(std::max(0.f,1-sy*sy)),angle=static_cast<float>(ray)*2.39996323f;
            const glm::vec3 direction(radius*std::cos(angle),sy,radius*std::sin(angle));
            const auto abs=glm::abs(direction);const int axis=abs.x>abs.y?(abs.x>abs.z?0:2):(abs.y>abs.z?1:2);
            const auto sector=static_cast<std::size_t>(axis*2+(direction[axis]<0?1:0));
            const auto hit=scene.trace(origin,direction,settings.maxDistance);
            ++counts[sector];moments[sector][0]+=hit.distance;moments[sector][1]+=static_cast<double>(hit.distance)*hit.distance;
            if(!hit.found) continue;
            nearest=std::min(nearest,hit.distance);
            const auto& triangle=scene.triangles[hit.triangle];
            // World surfaces are rendered two-sided, including recovered meshes
            // with reversed winding. Match their visible-hemisphere convention.
            const auto normal=glm::dot(triangle.normal,-direction)<0?-triangle.normal:triangle.normal;
            const auto& material=scene.material(triangle);if(material.unlit) continue;
            const auto position=origin+direction*hit.distance;
            const float metallic=scene.metal(triangle,hit);
            const auto reflectance=glm::vec3(scene.color(triangle,hit))*(.96f*(1-metallic)/pi);
            const auto basis=diffuseShBasis(direction);
            for(std::size_t s=0;s<lights.size();++s) {
                const auto radiance=reflectance*rectangleIrradiance(scene,lights[s],position,normal,settings);
                for(std::size_t i=0;i<9;++i) {
                    const float convolution=i==0?pi:(i<4?2*pi/3:pi/4);
                    const float scale=basis[i]*convolution*4*pi/static_cast<float>(settings.rayCount);
                    for(int c=0;c<3;++c) result.transfer[p*lights.size()+s][i][c]+=radiance[c]*scale;
                }
            }
        }
        if(nearest<.025f) result.validity[p]=0;
        for(std::size_t s=0;s<6;++s) for(std::size_t k=0;k<2;++k)
            result.distanceMoments[p][s][k]=static_cast<float>(moments[s][k]/std::max(1,counts[s]));
    }
    return result;
}

IndirectTransport bakeIndirectTransport(const gltf::scene::GltfSceneAsset& asset,const IndirectBakeSettings& settings) {
    IndirectTransport result;result.settings=settings;result.sceneIdentity=indirectSceneIdentity(asset);
    const auto& dims=settings.counts;
    if(!std::isfinite(settings.surfelSpacing)||settings.surfelSpacing<.125f||settings.surfelSpacing>4||
       settings.faceResolution<4||settings.faceResolution>64||(settings.faceResolution&(settings.faceResolution-1)))
        throw std::invalid_argument("Indirect bake sample settings");
    std::size_t total=1;for(unsigned d:dims){if(d<2||d>65)throw std::invalid_argument("Indirect grid size");total*=d;}
    if(total>8192)throw std::invalid_argument("Indirect probe budget");
    Scene scene(asset);
    for(unsigned z=0;z<dims[2];++z)for(unsigned y=0;y<dims[1];++y)for(unsigned x=0;x<dims[0];++x) {
        IndirectProbe p;p.position=settings.origin+settings.spacing*glm::vec3(x,y,z);p.volume=settings.volume;result.probes.push_back(p);
    }
    buildIndirectTetrahedra(result);
    using Key=std::array<int,4>;
    struct Accum {IndirectSurfel surfel;unsigned samples=0;};
    std::map<Key,unsigned> brickIndices;
    std::vector<std::map<Key,Accum>> brickSamples;
    const auto nearestProbe=[&](glm::vec3 p){
        glm::ivec3 grid(glm::round((p-settings.origin)/settings.spacing));
        for(unsigned a=0;a<3;++a)grid[a]=std::clamp(grid[a],0,int(dims[a])-1);
        return unsigned((grid.z*dims[1]+grid.y)*dims[0]+grid.x);
    };
    const auto hitBrick=[&](const Hit& hit,glm::vec3 direction,glm::vec3 origin) {
        const auto& tri=scene.triangles[hit.triangle];const auto& material=scene.material(tri);
        auto normal=scene.shadingNormal(tri,hit);if(glm::dot(tri.normal,-direction)<0)normal=-normal;
        const auto position=origin+direction*hit.distance;
        const auto a=glm::abs(normal);int axis=a.x>a.y?(a.x>a.z?0:2):(a.y>a.z?1:2);
        const int face=axis*2+(normal[axis]<0?1:0);
        const glm::ivec3 brick(glm::floor(position/4.f));
        const Key key{brick.x,brick.y,brick.z,face};
        auto [it,inserted]=brickIndices.emplace(key,unsigned(brickIndices.size()));
        if(inserted){if(brickIndices.size()>32768)throw std::runtime_error("Indirect brick budget");result.bricks.push_back({glm::vec3(brick)*4.f+glm::vec3(2),0,0});brickSamples.emplace_back();}
        const auto quant=glm::ivec3(glm::floor(position/settings.surfelSpacing));
        auto& value=brickSamples[it->second][{quant.x,quant.y,quant.z,face}];auto& surfel=value.surfel;
        const auto uv=scene.uv(tri,hit);
        const float metal=scene.metal(tri,hit);
        glm::vec3 color(scene.color(tri,hit));
        const int materialIndex=tri.primitive->materialIndex;
        const auto factor=materialIndex<int(settings.materialEmissionFactors.size())?settings.materialEmissionFactors[materialIndex]:vec(material.emissiveFactor);
        glm::vec3 emission=glm::vec3(sample(material.emissive,uv,material.emissive.textureSrgb!=0))*factor;
        if(material.unlit){emission+=color;color=glm::vec3(0);}
        const auto count=float(value.samples);const auto weight=1.f/(count+1);
        surfel.position=(surfel.position*count+position)*weight;
        const auto accumulatedNormal=surfel.normal*count+normal;
        surfel.normal=glm::dot(accumulatedNormal,accumulatedNormal)>1e-10f?glm::normalize(accumulatedNormal):normal;
        surfel.albedo=(surfel.albedo*count+color*(1-metal))*weight;
        surfel.emission=(surfel.emission*count+emission)*weight;
        surfel.closestProbe=nearestProbe(position+normal*.125f);
        const int mi=tri.primitive->materialIndex;
        surfel.emissionChannel=mi<int(settings.materialEmissionChannels.size())?settings.materialEmissionChannels[mi]:-1;
        ++value.samples;
        return it->second;
    };
    std::vector<bool> buried(result.probes.size(),false);
    const unsigned side=settings.faceResolution;const float denominator=float(side*side);
    for(unsigned probeIndex=0;probeIndex<result.probes.size();++probeIndex) {
        auto& probe=result.probes[probeIndex];float backfaceFraction=0;
        for(unsigned face=0;face<6;++face) {
            std::map<unsigned,unsigned> hits;unsigned sky=0,valid=0,backHits=0;
            for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x) {
                const float u=2*(float(x)+.5f)/float(side)-1,v=2*(float(y)+.5f)/float(side)-1;
                glm::vec3 direction;
                switch(face){case 0:direction={1,v,u};break;case 1:direction={-1,v,-u};break;case 2:direction={u,1,v};break;
                    case 3:direction={u,-1,-v};break;case 4:direction={u,v,1};break;default:direction={-u,v,-1};break;}
                direction=glm::normalize(direction);
                const auto hit=scene.trace(probe.position,direction,settings.maxDistance);
                if(hit.found) {
                    ++hits[hitBrick(hit,direction,probe.position)];
                    ++valid;
                    if(glm::dot(scene.triangles[hit.triangle].normal,direction)>0) {
                        const auto front=scene.trace(probe.position,direction,settings.maxDistance,1);
                        if(!front.found||hit.distance<front.distance-.05f)++backHits;
                    }
                } else if(face!=3)++sky;
            }
            backfaceFraction+=valid?float(backHits)/float(valid):0.f;
            probe.first[face]=unsigned(result.factors.size());probe.sky[face]=float(sky)/denominator;
            // Discard sub-pixel influences, retaining their missing energy for the environment fallback.
            const unsigned threshold=std::max(1u,unsigned(std::ceil(6.f*denominator/4096.f)));
            for(const auto& [brick,n]:hits)if(n>=threshold)result.factors.push_back({brick,float(n)/denominator});
            probe.count[face]=unsigned(result.factors.size())-probe.first[face];
            if(probe.count[face]>768)throw std::runtime_error("Indirect directional factor budget");
        }
        buried[probeIndex]=backfaceFraction/6.f>.5f;
        if(probeIndex%128==0)std::cout<<"Indirect bake probes "<<probeIndex<<"/"<<result.probes.size()<<std::endl;
    }
    std::vector<int> remap(result.bricks.size(),-1);
    std::vector<IndirectBrick> retained;
    for(unsigned b=0;b<result.bricks.size();++b) {
        // A sparse directional brick is unstable under relighting; its influence
        // remains missing energy, filled by the bounded environment contribution.
        if(brickSamples[b].size()<12)continue;
        auto brick=result.bricks[b];brick.first=unsigned(result.surfels.size());
        for(const auto& [key,value]:brickSamples[b])result.surfels.push_back(value.surfel);
        brick.count=unsigned(result.surfels.size())-brick.first;
        remap[b]=int(retained.size());retained.push_back(brick);
    }
    result.bricks=std::move(retained);std::vector<IndirectFactor> retainedFactors;
    for(auto& probe:result.probes)for(unsigned face=0;face<6;++face) {
        const auto first=probe.first[face],count=probe.count[face];probe.first[face]=unsigned(retainedFactors.size());
        for(unsigned i=0;i<count;++i) {
            auto factor=result.factors[first+i];const int brick=remap[factor.brick];
            if(brick>=0){factor.brick=unsigned(brick);retainedFactors.push_back(factor);}
        }
        probe.count[face]=unsigned(retainedFactors.size())-probe.first[face];
    }
    result.factors=std::move(retainedFactors);
    if(result.surfels.size()>(1u<<21))throw std::runtime_error("Indirect surfel budget");
    for(auto& tetra:result.tetrahedra)for(unsigned a=0;a<4;++a) {
        const auto origin=result.probes[tetra.probes[a]].position;
        std::array<glm::vec3,3> face{};unsigned n=0;
        for(unsigned b=0;b<4;++b)if(a!=b)face[n++]=result.probes[tetra.probes[b]].position;
        const auto samples=indirectVisibilitySamples(face[0],face[1],face[2]);
        for(unsigned i=0;i<15;++i) {
            const auto ray=samples[i]-origin;const float distance=glm::length(ray);
            const auto hit=scene.trace(origin,ray/distance,distance);
            tetra.visibility[a][i]=std::uint8_t(std::clamp(std::floor(hit.distance/distance*255.f),0.f,255.f));
        }
        tetra.visibility[a][15]=buried[tetra.probes[a]]?1:0;
    }
    std::vector<std::vector<unsigned>> candidates(result.bricks.size());
    for(unsigned i=0;i<result.probes.size();++i)if(!buried[i])for(unsigned f=0;f<6;++f) {
        const auto& p=result.probes[i];
        for(unsigned j=0;j<p.count[f];++j)candidates[result.factors[p.first[f]+j].brick].push_back(i);
    }
    for(unsigned b=0;b<result.bricks.size();++b) {
        auto& c=candidates[b];std::sort(c.begin(),c.end());c.erase(std::unique(c.begin(),c.end()),c.end());
        const auto& brick=result.bricks[b];
        constexpr unsigned invalid=~0u;
        unsigned backup=nearestProbe(brick.center);float nearest=std::numeric_limits<float>::max();
        for(unsigned p:c) {
            const auto delta=result.probes[p].position-brick.center;const float distance=glm::dot(delta,delta);
            if(distance<nearest){nearest=distance;backup=p;}
        }
        for(unsigned i=0;i<brick.count;++i) {
            auto& surfel=result.surfels[brick.first+i];const auto origin=surfel.position+surfel.normal*.125f;
            float best=std::numeric_limits<float>::max();surfel.closestProbe=invalid;
            for(unsigned p:c) {
                const auto ray=result.probes[p].position-origin;const float distance=glm::length(ray);
                // Stable 4 m jitter avoids coherent feedback discontinuities between adjacent bricks.
                const auto h=(p*1664525u+(brick.first+i)*1013904223u);
                const float score=distance+(float(h&65535u)/65535.f*8.f-4.f);
                if(score>=best||distance<.0001f)continue;
                if(!scene.trace(origin,ray/distance,distance-.001f).found){surfel.closestProbe=p;best=score;}
            }
            if(surfel.closestProbe!=invalid)backup=surfel.closestProbe;
        }
        for(unsigned i=0;i<brick.count;++i) {
            auto& surfel=result.surfels[brick.first+i];
            if(surfel.closestProbe!=invalid)continue;
            if(i+1<brick.count)surfel.closestProbe=result.surfels[brick.first+i+1].closestProbe;
            if(surfel.closestProbe==invalid&&i)surfel.closestProbe=result.surfels[brick.first+i-1].closestProbe;
            if(surfel.closestProbe==invalid)surfel.closestProbe=backup;
        }
    }
    if(settings.terrainResolution) {
        const unsigned size=settings.terrainResolution;
        if(size<4||size>1024||(size&(size-1)))throw std::invalid_argument("Terrain bake size");
        const auto bounds=scene.nodes.front();
        result.terrain.resize(std::size_t(size)*size);result.terrainMaterials.resize(result.terrain.size());
        for(unsigned z=0;z<size;++z)for(unsigned x=0;x<size;++x) {
            auto& out=result.terrain[z*size+x];glm::vec3 normal(0),color(0);float height=0;unsigned hits=0;
            auto& properties=result.terrainMaterials[z*size+x];glm::vec4 propertiesSum(0);std::array<float,32> dominance{};
            const glm::vec2 center=settings.terrainOrigin+(glm::vec2(x,z)+.5f)*settings.terrainSpacing;
            for(unsigned tap=0;tap<16;++tap) {
                const glm::vec2 offset=(glm::vec2(tap%4,tap/4)+.5f)/4.f-.5f;
                const auto xy=center+offset*settings.terrainSpacing;
                const glm::vec3 origin(glm::clamp(xy.x,bounds.lo.x+.01f,bounds.hi.x-.01f),bounds.hi.y+1,
                    glm::clamp(xy.y,bounds.lo.z+.01f,bounds.hi.z-.01f));
                const auto hit=scene.trace(origin,{0,-1,0},bounds.hi.y-bounds.lo.y+2);
                if(!hit.found)continue;
                const auto& tri=scene.triangles[hit.triangle];auto n=scene.shadingNormal(tri,hit);if(n.y<0)n=-n;
                normal+=n;color+=glm::vec3(scene.color(tri,hit));height+=origin.y-hit.distance;++hits;
                const auto v=scene.properties(tri,hit);propertiesSum+=glm::vec4(v.roughness,v.specular,v.dominantWeight,v.selectedWeight);
                dominance[v.dominantLayer]+=v.dominantWeight;
            }
            if(hits) {
                out.normal=glm::dot(normal,normal)>1e-10f?glm::normalize(normal):glm::vec3(0,1,0);
                out.albedo=color/float(hits);out.height=height/float(hits);
                propertiesSum/=float(hits);properties={propertiesSum.x,propertiesSum.y,propertiesSum.z,propertiesSum.w,
                    unsigned(std::max_element(dominance.begin(),dominance.end())-dominance.begin())};
            }
            float sky=0;
            const glm::vec3 origin(center.x,out.height+.2f,center.y);
            for(unsigned i=0;i<16;++i) {
                const float y=(float(i)+.5f)/16.f,angle=float(i)*2.39996323f,r=std::sqrt(1-y*y);
                if(!scene.trace(origin,{r*std::cos(angle),y,r*std::sin(angle)},64).found)sky+=1.f/16.f;
            }
            out.sky=sky;
        }
    }
    validateIndirectTransport(result);return result;
}
}
