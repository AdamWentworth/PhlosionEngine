#include "engine/render/RenderPhaseProfile.h"
#include "engine/render/LegacyOpenGL.h"
#define SDL_MAIN_HANDLED
#include "engine/platform/Window.h"
#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/VulkanRenderBackend.h"
#include <SDL.h>
#include <stb_image.h>
#include <glm/gtc/matrix_transform.hpp>
#include <filesystem>
#include <iostream>
#include <memory>
#include <set>
using namespace engine::render;
namespace {
void check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
void env(const char* name,const std::string& value){
#ifdef _WIN32
    _putenv_s(name,value.c_str());
#else
    setenv(name,value.c_str(),1);
#endif
}
void checkLists(const WorldClusteredLightingDiagnostic& data,const WorldContactUniforms& surface,const WorldClusteredLightingView& view,bool empty) {
    WorldClusteredLayout layout(data.width,data.height);check(data.words.size()==layout.words,"Diagnostic work size");
    const auto& w=data.words;
    check((w[0]+w[1]>0)!=empty,"Material tile classification missing or leaked");
    for(unsigned c=0;c<2;++c) {
        check(w[c]<=layout.x*layout.y,"Class list overflow");
        std::set<unsigned> tiles;
        for(unsigned i=0;i<w[c];++i){auto tile=w[layout.listOffset+c*layout.x*layout.y+i];check(tile<layout.x*layout.y&&tiles.insert(tile).second,"Duplicate/invalid class tile");}
    }
    // Independent double precision frustum/AABB oracle, including the coarse
    // parent reduction. Every recorded fine mask must equal a direct full list.
    glm::dmat4 inverse(0.0);
    for(int row=0;row<4;++row)for(int col=0;col<4;++col)inverse[col][row]=surface[4+row][col];
    const glm::dvec3 camera(view.camera[0],view.camera[1],view.camera[2]),forward(view.forward[0],view.forward[1],view.forward[2]);
    const auto ray=[&](double x,double y,double z){
        auto p=inverse*glm::dvec4(x*2-1,1-y*2,z,1);return glm::dvec3(p)/p.w;
    };
    const auto points=packWorldPointLights(view.environment);const auto rects=packWorldRectLights(view.environment);
    unsigned checked=0;
    for(unsigned z=0;z<32;++z)for(unsigned y=0;y<layout.y;++y)for(unsigned x=0;x<layout.x;++x) {
        if((x+7*y+13*z)%3!=0)continue;
        glm::dvec3 lo(1e30),hi(-1e30);
        for(unsigned corner=0;corner<8;++corner) {
            double u=std::min((x+(corner&1))*16u,unsigned(data.width))/double(data.width);
            double v=std::min((y+((corner>>1)&1))*16u,unsigned(data.height))/double(data.height);
            unsigned slice=z+((corner>>2)&1);double distance=slice==0?0:slice==32?1e7:std::exp2(slice*.5-2);
            auto a=ray(u,v,-.5),b=ray(u,v,.5);
            auto p=a+(b-a)*((distance-glm::dot(a-camera,forward))/glm::dot(b-a,forward));
            lo=glm::min(lo,p);hi=glm::max(hi,p);
        }
        unsigned expected=0;
        for(unsigned light=0;light<20;++light) {
            const auto p=light<4?points[2*light]:rects[(light-4)*4];
            const auto color=light<4?points[2*light+1]:rects[(light-4)*4+3];
            if(!view.environment.enabled||p[3]<=0||std::max({color[0],color[1],color[2]})<=0)continue;
            const unsigned authored=unsigned(color[3]),flags=(authored>>8)&255;
            if(!(authored&255)||!(flags&WorldLightSurfaces)||!(flags&(WorldLightDiffuse|WorldLightSpecular)))continue;
            double radius=p[3];
            if(light>=4&&rects[(light-4)*4+1][3]>.5)radius*=1.001;
            if(light>=4&&rects[(light-4)*4+1][3]>.5)for(unsigned row=1;row<=2;++row){
                const auto axis=rects[(light-4)*4+row];radius+=glm::length(glm::dvec3(axis[0],axis[1],axis[2]));
            }
            if(light>=4&&rects[(light-4)*4+1][3]>1.5) {
                const auto source=rects[65+(light-4)*3];radius=p[3]*1.001+source[1]*.5+source[0];
            }
            auto delta=glm::max(glm::max(lo-glm::dvec3(p[0],p[1],p[2]),glm::dvec3(p[0],p[1],p[2])-hi),glm::dvec3(0));
            if(glm::dot(delta,delta)<=radius*radius*1.00001)expected|=1u<<light;
        }
        const auto actual=w[layout.fineOffset+(z*layout.y+y)*layout.x+x];
        if(actual!=expected)throw std::runtime_error("Cluster culling mismatch at "+std::to_string(x)+","+std::to_string(y)+","+std::to_string(z)+" actual "+std::to_string(actual)+" expected "+std::to_string(expected));
        ++checked;
    }
    std::cout<<"checked "<<checked<<" fine clusters; material tiles "<<w[0]<<","<<w[1]<<"\n";
}
}
int main(int argc,char** argv)try{
    check(argc>=4&&argc<=6,"Expected API case output [project root] [reference image]");const std::string api=argv[1];std::string mode=argv[2];
    const bool native=mode.starts_with("native-");if(native)mode=mode.substr(7);
    const bool probeFlags=mode.starts_with("probe-flags-"),probeOracle=mode.starts_with("probe-oracle-");
    if(probeFlags)mode=mode.substr(12);
    if(probeOracle)mode=mode.substr(13);
    std::string feature;
    for(const char* name:{"channels","diffuse","specular","disabled","consumers","transparent"})
        if(mode.rfind(std::string(name)+"-",0)==0){feature=name;mode=mode.substr(feature.size()+1);break;}
    const bool oracle=mode=="oracle";if(oracle)mode="forward";
    const bool scene=mode.rfind("scene-",0)==0;if(scene)mode=mode.substr(6);
    const bool relight=mode.rfind("relight",0)==0;
    const bool probe=mode.rfind("probe",0)==0 || relight;
    const bool freshProbe=probe && mode.find("fresh")!=std::string::npos;
    const bool smallProbe=probe && mode.find("small")!=std::string::npos;
    const bool flightProbe=probe && mode.find("flight")!=std::string::npos;
    const bool abortProbe=probe && mode.find("abort")!=std::string::npos;
    check(api=="vulkan"||api=="d3d12","Invalid API");
    std::filesystem::create_directories(std::filesystem::path(argv[3]).parent_path());
    env("PHLOSION_BACKEND_SCREENSHOT_PATH",argv[3]);env("PHLOSION_BACKEND_SCREENSHOT_FRAME",probe?"8":"2");env("PHLOSION_BACKEND_SCREENSHOT_DEFER","0");
    SDL_SetMainReady();struct Quit{~Quit(){SDL_Quit();}}quit;
    int width=probe?256:320,height=probe?256:240;
    const int probeResolution=smallProbe?128:256;
    Window window("Clustered lighting",width,height,api=="vulkan"?Window::GraphicsApi::Vulkan:Window::GraphicsApi::Native,false,false);
    std::unique_ptr<IRenderBackend> renderer;
    if(api=="vulkan")renderer=std::make_unique<VulkanRenderBackend>(window.getSDLWindow(),width,height,false);
    else renderer=std::make_unique<D3D12RenderBackend>(window.getSDLWindow(),width,height,false);
    if(argc>=5 && std::string(argv[4])!="-")renderer->setWorldMaterialProfile(loadWorldMaterialProfile(argv[4],"config/render/world_materials.json"));
    auto& surface=dynamic_cast<IWorldSurfaceBuffer&>(*renderer);
    WorldClusteredLightingView lighting;lighting.camera={0,0,5};lighting.forward={0,0,-1};lighting.environment.enabled=true;
    lighting.environment.sunRadiance={0,0,0};
    for(unsigned i=0;i<4;++i){auto& p=lighting.environment.pointLights[i];p.position={float(i)-1.5f,.6f,1.5f};p.radius=3.f+float(i);p.intensity={.2f,.3f,.7f};}
    for(unsigned i=0;i<16;++i){auto& p=lighting.environment.rectLights[i];p.position={float(i%4)*1.1f-1.8f,float(i/4)*.8f-1.2f,1.f+float(i%3)};
        p.range=2.f+float(i%4);p.halfU={.4f,0,0};p.halfV={0,-.1f,0};p.radiance={.35f,.1f,.25f};p.response=mode=="ltc"&&i%2?WorldRectLightResponse::Ltc:WorldRectLightResponse::FiniteLine;}
    if(probeFlags||probeOracle) {
        for(unsigned i=1;i<4;i+=2) {
            if(probeFlags)lighting.environment.pointLights[i].participation&=~WorldLightProbes;
            else lighting.environment.pointLights[i].intensity={};
        }
        for(unsigned i=1;i<16;i+=2) {
            if(probeFlags)lighting.environment.rectLights[i].participation&=~WorldLightProbes;
            else lighting.environment.rectLights[i].radiance={};
        }
    }
    if(!feature.empty()) {
        for(unsigned i=0;i<4;++i)lighting.environment.pointLights[i].channels=1u<<(i%2);
        for(unsigned i=0;i<16;++i) {
            auto& p=lighting.environment.rectLights[i];p.channels=1u<<(i%2);
            p.response=i%2?WorldRectLightResponse::Ltc:WorldRectLightResponse::FiniteLine;
        }
        const auto flags=[&](auto& light,unsigned i) {
            if(feature=="diffuse")light.participation&=~WorldLightSpecular;
            if(feature=="specular")light.participation&=~WorldLightDiffuse;
            if(feature=="disabled")light.participation=0;
            if(feature=="consumers" && i%2)light.participation=WorldLightAll&~WorldLightSurfaces;
            if(feature=="transparent" && i%2)light.participation=WorldLightAll&~WorldLightTransparent;
        };
        for(unsigned i=0;i<4;++i)flags(lighting.environment.pointLights[i],i);
        for(unsigned i=0;i<16;++i)flags(lighting.environment.rectLights[i],i);
    }
    if(native) {
        for(unsigned i=0;i<4;++i) {
            auto& p=lighting.environment.pointLights[i];p.source.axis={0,0,-1};
            p.source.cone=i%2==1;p.source.innerAngle=40;p.source.outerAngle=140;p.source.softness=1.5f;
        }
        for(unsigned i=0;i<16;++i) {
            auto& r=lighting.environment.rectLights[i];r.response=WorldRectLightResponse::FiniteSource;
            r.source.radius=.1f;r.source.length=.6f;r.source.intensityScale=.16f;
            r.source.cone=i%3!=0;r.source.innerAngle=20;r.source.outerAngle=160;
        }
    }
    constexpr std::array<unsigned char,4> white{255,255,255,255},hole{255,255,255,0};
    IRenderBackend::WorldTextureData texture;texture.key=texture.cacheKey="cluster-white";texture.rgba=mode=="masked"?hole.data():white.data();texture.width=texture.height=1;
    if(feature=="transparent")texture.alphaMode=2;
    texture.textureSrgb=0;texture.materialMode=mode=="unsupported"?0:2;texture.alphaMode=feature=="transparent"?2:mode=="masked"?1:0;
    if(probe)texture.normalScale=0; // Numeric packing fixture uses an explicitly unmapped normal.
    texture.cameraPosX=texture.cameraPosY=0;texture.cameraPosZ=5;texture.cameraForwardX=texture.cameraForwardY=0;texture.cameraForwardZ=-1;texture.roughnessFactor=.65f;
    constexpr std::array<std::uint32_t,6> indices{0,2,1,0,3,2};
    std::array<IRenderBackend::WorldMeshVertex,4> vertices{};
    for(int i=0;i<4;++i){auto& p=vertices[i];p.x=i==0||i==3?-2.f:2.f;p.y=i<2?-1.5f:1.5f;p.nz=1;p.tx=1;p.tw=1;p.r=.2f;p.g=.4f;p.b=.7f;p.a=1;}
    std::vector<IRenderBackend::WorldSceneGeometry> geometries(1);
    auto& geometry=geometries[0];geometry.handle.id=1;geometry.geometryCacheKey="cluster-scene-quad";
    geometry.vertices=vertices.data();geometry.vertexCount=vertices.size();geometry.indices=indices.data();geometry.indexCount=indices.size();
    std::vector<IRenderBackend::WorldSceneMaterial> materials(1);
    auto& material=materials[0];material.handle.id=1;material.textureKey=material.textureCacheKey="cluster-scene-white";
    material.textureRgba=white.data();material.textureWidth=material.textureHeight=1;material.textureSrgb=0;
    material.materialMode=2;material.metallicFactor=0;material.roughnessFactor=.65f;
    std::vector<IRenderBackend::WorldSceneRenderObject> objects(1);
    objects[0].handle.id=objects[0].geometryHandle.id=objects[0].materialHandle.id=1;
    IRenderBackend::WorldSceneFrame sceneFrame;
    IRenderBackend::WorldSceneDrawClass drawClass;drawClass.objectHandle.id=1;drawClass.instances.resize(1);drawClass.instances[0].handle.id=1;
    sceneFrame.drawClasses.push_back(drawClass);sceneFrame.indirectCommandCount=1;
    int capturedProbeFaces=0;
    std::array<std::array<float,4>,32> emitterGains;emitterGains.fill({1,1,1,1});
    for(int frame=0;frame<(probe?10:4);++frame){
        if(mode=="extent"&&frame==1){width=401;height=257;SDL_SetWindowSize(window.getSDLWindow(),width,height);SDL_PumpEvents();renderer->onResize(width,height);}
        const auto projection=mode=="ortho"?glm::ortho(-3.f,3.f,-2.5f,2.5f,.1f,200.f):glm::perspective(glm::radians(60.f),float(width)/height,.1f,200.f);
        if(mode=="moving"){lighting.camera[0]=frame*.13f;lighting.environment.pointLights[0].position[0]=frame*.2f;}
        if(probe) {
            lighting.environment.pointLights[0].position[0]=frame*.2f;
            lighting.environment.pointLights[0].intensity={.2f,.3f+frame*.08f,.7f};
            lighting.environment.rectLights[0].radiance={.35f+frame*.07f,.1f,.25f};
        }
        if(relight) {
            lighting.environment.sunDirection={0,0,1};lighting.environment.sunRadiance={float(frame)*.08f,.15f,.07f};
            lighting.environment.ambientRadiance={.03f,.05f,.08f};
            if(mode.find("wet")!=std::string::npos){lighting.environment.surfaceWeatherEnabled=true;lighting.environment.surfaceWetness=float(frame)/9.f;}
            if(mode.find("emission")!=std::string::npos) {
                texture.emissiveFactorR=.5f;texture.emissiveFactorG=.1f;texture.emissiveFactorB=.03f;
                emitterGains[0]={float(frame)/9.f,float(frame)/9.f,float(frame)/9.f,1};
            }
            if(mode.find("haze")!=std::string::npos) {
                lighting.environment.hazeDensity=.05f*frame;
                lighting.environment.hazeRadiance={.1f,.3f,.4f};
            }
        }
        auto vp=projection*glm::lookAt(glm::make_vec3(lighting.camera.data()),glm::make_vec3(lighting.camera.data())+glm::vec3(0,0,-1),glm::vec3(0,1,0));
        auto packed=packWorldContactVisibility(glm::value_ptr(vp),width,height,.8f,.015f);
        if(mode=="off")lighting.environment.enabled=false;
        texture.environment=lighting.environment;texture.cameraPosX=lighting.camera[0];texture.metallicFactor=mode=="metal"?1.f:0.f;
        const auto draw=[&]{
            if(!scene) {
                if(feature.empty()){renderer->drawWorldIndexedMeshTextured(vertices.data(),4,indices.data(),6,&texture,glm::value_ptr(vp),width,height);return;}
                const auto original=texture;
                const unsigned consumer=feature=="transparent"?WorldLightTransparent:WorldLightSurfaces;
                for(unsigned section=0;section<3;++section) {
                    texture=original;texture.environment.lightChannels=section==2?0:1u<<section;
                    if(oracle) {
                        // Independent receiver oracle removes excluded sources before rendering;
                        // shader-side masks are then all enabled for those surviving sources.
                        for(auto& light:texture.environment.pointLights) {
                            if(!(light.channels&texture.environment.lightChannels)||!(light.participation&consumer))light.intensity={};
                            light.channels=255;light.participation|=consumer;
                        }
                        for(auto& light:texture.environment.rectLights) {
                            if(!(light.channels&texture.environment.lightChannels)||!(light.participation&consumer))light.radiance={};
                            light.channels=255;light.participation|=consumer;
                        }
                        texture.environment.lightChannels=255;
                    }
                    auto part=vertices;
                    for(auto& v:part)v.x=v.x/3.f+(float(section)-1.f)*4.f/3.f;
                    renderer->drawWorldIndexedMeshTextured(part.data(),4,indices.data(),6,&texture,glm::value_ptr(vp),width,height);
                }
                texture=original;return;
            }
            IRenderBackend::WorldSceneView view;view.geometries=&geometries;view.materials=&materials;view.renderObjects=&objects;
            view.registryGeneration=1;view.viewProjectionMatrix4x4=glm::value_ptr(vp);view.surfaceWidth=width;view.surfaceHeight=height;
            view.cameraWorldPos=lighting.camera;view.cameraForward=lighting.forward;view.cameraTarget={texture.cameraTargetX,texture.cameraTargetY,texture.cameraTargetZ};
            view.environment=texture.environment;renderer->submitWorldScene(sceneFrame,view);
        };
        renderer->beginFrame(0,0,0,1);renderer->beginWorldSceneColorPass(width,height);
        { RenderCpuPhaseScope prepare(*renderer,"fixture-cpu-prepare"); }
        check(!surface.captureWorldClusteredLighting(),"Stale cluster outputs leaked");
        const auto setProbeMaterial=[&](unsigned face,bool replacement) {
            for(auto& v:vertices) {v.r=(replacement?.6f:.2f)+face*.03f;v.g=.4f;v.b=.7f;}
        };
        if(relight) {
            if(freshProbe || frame==0)for(unsigned face=0;face<6;++face) {
                check(surface.beginWorldProbeSurface(41,100+frame,face,glm::value_ptr(vp),probeResolution),"Relight capture");
                renderer->drawWorldIndexedMeshTextured(vertices.data(),4,indices.data(),6,&texture,glm::value_ptr(vp),probeResolution,probeResolution);
                check(surface.endWorldProbeSurface(),"Relight surface close");++capturedProbeFaces;
            }
            check(surface.selectWorldProbeSurface(41,unsigned(frame%6)),"Relight select");
            check(surface.relightWorldProbeSurface(lighting,emitterGains),"Relight resolve");
            renderer->endWorldSceneColorPass();
        { RenderPhaseScope submit(*renderer,"fixture-submit"); }
        renderer->endFrame();
            if(!renderer->handlesPresentation())window.swapBuffers();
            continue;
        }
        if(probe) {
            check(!surface.selectWorldProbeSurface(999,0),"Missing probe selected");
            check(!surface.beginWorldProbeSurface(0,1,0,glm::value_ptr(vp),probeResolution),"Zero probe accepted");
            const auto capture=[&](unsigned face,std::uint64_t revision,bool replacement,bool abort=false) {
                auto faceVp=vp;faceVp[0][0]*=1.f+.08f*float(face);
                check(surface.beginWorldProbeSurface(41,revision,face,glm::value_ptr(faceVp),probeResolution),"Probe surface begin");
                check(!surface.selectWorldProbeSurface(41,0),"Partial active capture selected");
                check(!surface.releaseWorldProbeSurface(41),"Active capture released");
                setProbeMaterial(face,replacement);
                renderer->drawWorldIndexedMeshTextured(vertices.data(),4,indices.data(),6,&texture,glm::value_ptr(faceVp),probeResolution,probeResolution);
                ++capturedProbeFaces;
                if(abort)surface.clearWorldSurfaceBuffer();
                else check(surface.endWorldProbeSurface(),"Probe surface end");
            };
            if(freshProbe) {
                for(unsigned face=0;face<6;++face)capture(face,100+frame,!abortProbe && frame>=4);
            } else if(frame==0) {
                for(unsigned face=0;face<6;++face) {
                    capture(face,1,false);
                    check(surface.worldProbeSurfaceRevision(41)==(face==5?1:0),"Incomplete initial probe published");
                }
            } else if(frame==1) {
                for(unsigned face=0;face<3;++face)capture(face,2,true);
            } else if(frame==2)capture(3,2,true,true);
            else if(frame==3 && !abortProbe)capture(3,2,true);
            else if(frame==4 && !abortProbe) {capture(4,2,true);capture(5,2,true);}
            if(!freshProbe)check(surface.worldProbeSurfaceRevision(41)==(!abortProbe && frame>=4?2:1),"Probe generation publication");
            // Overwrite the ordinary frame surface to catch accidental aliasing.
            check(surface.beginWorldSurfaceBuffer(glm::value_ptr(vp),width,height) && surface.endWorldSurfaceBuffer(),"Interleaved view");
            check(surface.selectWorldProbeSurface(41,unsigned(frame%6)),"Stored probe selection");
            check(surface.worldProbeSurfaceRevision(41)!=0,"Selected retained capture lost publication");
            setProbeMaterial(unsigned(frame%6),!abortProbe && frame>=4);
            vp[0][0]*=1.f+.08f*float(frame%6);
            if(!flightProbe || frame==9)check(surface.captureWorldSurfaceMotion(),"Stored material readback");
            packed=packWorldContactVisibility(glm::value_ptr(vp),probeResolution,probeResolution,.8f,.015f);
        } else {
            check(surface.beginWorldSurfaceBuffer(glm::value_ptr(vp),width,height),"Surface begin");draw();check(surface.endWorldSurfaceBuffer(),"Surface end");
        }
        if(mode!="forward" && !probe) {
            check(surface.resolveWorldClusteredLighting(lighting),"Cluster dispatch");
            if(!flightProbe || frame==9)check(surface.captureWorldClusteredLighting(),"Cluster diagnostic");
        }
        // Remove every forward local-light input after the GPU resolve. The
        // consumer image must still match "cluster"; otherwise a profile can
        // silently ignore the resolved buffers and shade the full list again.
        if(mode=="consumer") {
            for(auto& light:texture.environment.pointLights)light.intensity={0,0,0};
            for(auto& light:texture.environment.rectLights)light.radiance={0,0,0};
        }
        const auto materialBefore=texture;
        const auto verticesBefore=vertices;
        if(mode=="stored") {
            // Deliberately poison final-draw material inputs after the producer.
            // Correct composition must consume the captured values instead.
            for(auto& v:vertices){v.r=.95f;v.g=.02f;v.b=.03f;}
            texture.roughnessFactor=.04f;texture.metallicFactor=1;
            texture.emissiveFactorR=4;texture.emissiveFactorG=3;texture.emissiveFactorB=2;
        }
        if(probe)check(surface.relightWorldProbeSurface(lighting,emitterGains),"Retained compact probe consumer");
        else draw();
        texture=materialBefore;vertices=verticesBefore;
        renderer->endWorldSceneColorPass();
        { RenderPhaseScope submit(*renderer,"fixture-submit"); }
        renderer->endFrame();
        if(!renderer->handlesPresentation())window.swapBuffers();
        if(scene && api=="vulkan") {
            IRenderBackend::BackendFrameStats stats;
            check(renderer->getLastFrameStats(stats) && stats.fastSceneMaterialTableBinds>0,"Indexed material path was not exercised");
        }
        if(mode!="forward" && !probe && (!flightProbe || frame==9))checkLists(surface.readWorldClusteredLighting(),packed,lighting,mode=="masked"||mode=="unsupported"||feature=="transparent");
        if(probe && (!flightProbe || frame==9)) {
            const auto data=surface.readWorldSurfaceMotion();
            check(data.width==probeResolution && data.height==probeResolution,"Stored probe extent");
            const auto pixel=(data.height/2)*data.width+data.width/2;
            const float expected=(!abortProbe && frame>=4?.6f:.2f)+float(frame%6)*.03f;
            const auto core=data.identity.at(pixel),geometryCore=data.identityMetadata.at(pixel);
            const auto expectedColor=std::uint32_t(std::floor(std::sqrt(expected)*255.f));
            check((core&255u)==expectedColor,"Probe face/generation compact albedo mismatch");
            if((geometryCore&65535u)!=(127u|(127u<<8u)))
                throw std::runtime_error("Probe normal core mismatch: "+std::to_string(geometryCore&65535u));
            std::cout<<"probe-core depth="<<(geometryCore>>19u)<<" sign="<<((geometryCore>>16u)&1u)<<"\n";
            const auto expectedDepth=std::uint32_t(std::floor(std::log1p(5.0)/7.0*8191.0));
            check((geometryCore>>19u)==expectedDepth && (geometryCore&393216u)==0,"Probe logarithmic depth/sky core mismatch");
            check(((geometryCore>>16u)&1u)==1,"Probe camera-space normal hemisphere");
            check(((core>>24u)&3u)==0 && (core>>29u)==5,"Probe metal/roughness quantization");
            check(data.identity.at(0)==0 && data.identityMetadata.at(0)==0,"Empty probe sky sentinel");
            std::cout<<"probe-core frame="<<frame<<" albedo="<<core<<" geometryCore="<<geometryCore<<"\n";
        }
    }
    if(probe) {
        check(surface.releaseWorldProbeSurface(41) && surface.worldProbeSurfaceRevision(41)==0,"Probe release failed");
        check(capturedProbeFaces==(freshProbe?60:relight?6:abortProbe?10:13),"Unexpected probe geometry capture count");
        std::cout<<"probe geometry faces "<<capturedProbeFaces<<"; local relights 10\n";
    }
    if(argc==6) {
        int w=0,h=0,c=0,rw=0,rh=0,rc=0;
        std::unique_ptr<unsigned char,decltype(&stbi_image_free)> image(stbi_load(argv[3],&w,&h,&c,3),stbi_image_free);
        std::unique_ptr<unsigned char,decltype(&stbi_image_free)> reference(stbi_load(argv[5],&rw,&rh,&rc,3),stbi_image_free);
        check(image && reference && w==rw && h==rh,"Missing or mismatched consumer reference");
        int maximum=0;
        for(int i=0;i<w*h*3;++i)maximum=std::max(maximum,std::abs(int(image.get()[i])-int(reference.get()[i])));
        check(maximum<=((mode=="consumer"||mode=="stored"||probe)?0:1),"Clustered/forward consumer image differs");
    }
    std::cout<<"PASS "<<api<<" "<<mode<<"\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
