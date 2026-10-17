#include "engine/render/WorldDiffuseProbes.h"
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
struct Hit {float distance;std::size_t triangle=0;float u=0,v=0;bool found=false;};
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
    explicit Scene(const GltfSceneAsset& input):asset(input) {
        for(const auto& p:asset.primitives) {
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
                t.normal=glm::normalize(n);t.lo=glm::min(t.a,glm::min(t.b,t.c));t.hi=glm::max(t.a,glm::max(t.b,t.c));
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
    glm::vec4 color(const Triangle& t,const Hit& h) const {
        const auto& m=material(t);glm::vec4 vertex(0);
        const float weights[]{1-h.u-h.v,h.u,h.v};
        for(int i=0;i<3;++i) {const auto& v=t.primitive->vertices[t.indices[static_cast<std::size_t>(i)]];vertex+=glm::vec4(v.r,v.g,v.b,v.a)*weights[i];}
        const auto& f=m.baseColorFactor;
        return glm::clamp(vertex*glm::vec4(f[0],f[1],f[2],f[3])*sample(m.baseColor,uv(t,h),m.baseColor.textureSrgb!=0),glm::vec4(0),glm::vec4(1));
    }
    Hit trace(glm::vec3 origin,glm::vec3 direction,float maximum) const {
        Hit hit{maximum};visit(0,origin,direction,hit);return hit;
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
    void visit(std::size_t index,glm::vec3 o,glm::vec3 d,Hit& hit) const {
        const auto& n=nodes[index];if(!box(n,o,d,hit.distance)) return;
        if(n.left!=0) {visit(n.left,o,d,hit);visit(n.right,o,d,hit);return;}
        for(auto i=n.begin;i<n.end;++i) {
            const auto triangle=order[i];const auto& t=triangles[triangle];
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
            const float metallic=std::clamp(material.metallicFactor*sample(material.metallicRoughness,scene.uv(triangle,hit),false).b,0.f,1.f);
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
}
