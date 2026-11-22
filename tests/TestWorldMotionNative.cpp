#include "engine/render/LegacyOpenGL.h"
#define SDL_MAIN_HANDLED
#include "engine/platform/Window.h"
#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/VulkanRenderBackend.h"
#include <SDL.h>
#include <cmath>
#include <iostream>
#include <memory>
using namespace engine::render;
namespace {
void check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
WorldMotionMatrix shift(float x,float y=0,float z=0) {
    auto m=worldMotionIdentity;m[12]=x;m[13]=y;m[14]=z;return m;
}
}
int main(int argc,char** argv)try {
    std::cout<<std::unitbuf;
    check(argc==2||argc==3,"Expected renderer [project root]");const std::string api=argv[1];
    check(api=="vulkan"||api=="d3d12","Invalid renderer");
    SDL_SetMainReady();struct Quit{~Quit(){SDL_Quit();}}quit;
    constexpr int width=320,height=240;
    Window window("Native motion",width,height,api=="vulkan"?Window::GraphicsApi::Vulkan:Window::GraphicsApi::Native,false,false);
    std::unique_ptr<IRenderBackend> renderer;
    if(api=="vulkan")renderer=std::make_unique<VulkanRenderBackend>(window.getSDLWindow(),width,height,false);
    else renderer=std::make_unique<D3D12RenderBackend>(window.getSDLWindow(),width,height,false);
    if(argc==3){std::cout<<"Loading project profile\n";renderer->setWorldMaterialProfile(loadWorldMaterialProfile(argv[2],"config/render/world_materials.json"));std::cout<<"Project profile loaded\n";}
    auto& surface=dynamic_cast<IWorldSurfaceBuffer&>(*renderer);
    constexpr std::array<std::uint32_t,6> indices{0,2,1,0,3,2};
    std::array<IRenderBackend::WorldMeshVertex,4> vertices{};
    for(int i=0;i<4;++i) {
        auto& v=vertices[i];v.x=i==0||i==3?-.35f:.35f;v.y=i<2?-.35f:.35f;
        v.u=i==0||i==3?0.f:1.f;v.v=i<2?0.f:1.f;v.r=v.g=v.b=v.a=1;v.nz=1;v.tx=v.tw=1;v.weight0=1;
    }
    constexpr std::array<unsigned char,4> white{255,255,255,255},hole{255,255,255,0};
    constexpr std::array<unsigned char,4> body{64,128,192,255},bodyNormal{192,128,255,255},
        bodyRoughness{255,117,255,255},bodyOcclusion{77,255,255,255};
    // Exercise actual render targets, not the CPU projection helper.
    for(const std::string mode:{"class-character","class-vehicle","class-reduced","class-first","cpu","cpu-cached","first","still","rigid","camera","combined","skin","skin-paired","skin-owned-prepared","skin-owned-prepared-paired","skin-owned-prepared-cached","skin-owned-prepared-paired-cached","skin-prepared","skin-prepared-paired","skin-prepared-cached","skin-prepared-paired-cached","skin-instances-prepared","skin-instances-prepared-paired","skin-rewrite","skin-rewrite-paired","skin-rewrite-cached","skin-rewrite-paired-cached","skin-rewrite-interleaved","skin-rewrite-interleaved-paired","skin-rewrite-interleaved-cached","skin-rewrite-interleaved-paired-cached","skin-instances","skin-instances-paired","skin-capacity","skin-capacity-paired-cached","masked","transparent","behind","instances","clear","unenrolled","wind","wind-tree","sss","sss-masked","sss-hole"}) {
        if((mode.starts_with("wind") || mode.starts_with("sss")) && argc!=3)continue;
        std::cout<<"Starting "<<mode<<'\n';
        for(auto& vertex:vertices){vertex.z=mode.starts_with("wind")?.5f:0.f;vertex.tx=1;vertex.ty=0;vertex.weight3=0;}
        IRenderBackend::WorldTextureData t;t.key=t.cacheKey="motion-white";t.rgba=white.data();t.width=t.height=1;
        t.materialMode=2;t.textureSrgb=0;
        WorldMotionPose previous;previous.model=previous.instance=worldMotionIdentity;
        if(mode.starts_with("class"))previous.temporalMaterial=mode=="class-vehicle"?WorldTemporalMaterial::Vehicle:
            mode=="class-reduced"?WorldTemporalMaterial::ReducedHistory:WorldTemporalMaterial::Character;
        auto currentVP=worldMotionIdentity,previousVP=worldMotionIdentity;
        std::array<float,64> palette{};std::copy(worldMotionIdentity.begin(),worldMotionIdentity.end(),palette.begin());
        std::copy(worldMotionIdentity.begin(),worldMotionIdentity.end(),palette.begin()+16);
        float expectedX=0,expectedY=0;
        if(mode.starts_with("cpu")) {
            for(const auto& vertex:vertices)previous.positions.push_back({vertex.x-.2f,vertex.y+.1f,vertex.z});
            expectedX=-.1f;expectedY=-.05f;
        }
        if(mode=="rigid"||mode=="combined"){t.modelMatrix=shift(.2f,.1f);expectedX-=.1f;expectedY+=.05f;}
        if(mode=="camera"||mode=="combined"){previousVP=shift(-.1f,-.2f);expectedX-=.05f;expectedY+=.1f;}
        if(mode.starts_with("skin")) {
            t.gpuSkinning=1;t.gpuSkinningMode=mode.find("paired")!=std::string::npos?1:0;t.skinMatrixCount=1;
            t.skinMatrices=palette.data();palette[12]=.2f;palette[13]=-.1f;
            previous.matrixCount=1;previous.skinningMode=t.gpuSkinningMode;
            previous.deformation.assign(worldMotionIdentity.begin(),worldMotionIdentity.end());
            if(t.gpuSkinningMode)previous.deformation.insert(previous.deformation.end(),worldMotionIdentity.begin(),worldMotionIdentity.end());
            expectedX=-.1f;expectedY=-.05f;
        }
        std::vector<unsigned char> noise;
        if(mode.starts_with("wind")) {
            palette={};palette[0]=1;palette[4]=1;palette[28]=.2f;palette[51]=1;
            previous.matrixCount=4;previous.deformation.assign(palette.begin(),palette.end());previous.deformation[28]=0;
            t.cameraPosX=t.cameraPosY=t.cameraPosZ=0;
            t.materialRect1V=t.materialRect1W=t.materialRect1H=0;
            t.materialMode=238;t.alphaMode=1;t.gpuSkinning=1;t.skinMatrixCount=4;t.skinMatrices=palette.data();
            noise.resize(512*512*4,128);t.normalKey="motion-wind-noise";t.normalRgba=noise.data();t.normalWidth=t.normalHeight=512;
            expectedX=-.0375f;
            if(mode=="wind-tree") {
                palette[6]=1;palette[28]=0;palette[37]=.2f;palette[47]=2;palette[51]=0;
                previous.deformation.assign(palette.begin(),palette.end());previous.deformation[37]=0;
                for(auto& v:vertices){v.tx=0;v.ty=1;v.weight0=1;v.weight3=1;}
                expectedX=0;expectedY=.1f*(128.f/255+.5f);
            }
        }
        if(mode.starts_with("sss")) {
            t.materialMode=33;t.normalScale=.8f;t.roughnessFactor=.7f;t.occlusionStrength=.6f;
            t.cameraPosX=t.cameraPosY=0;t.cameraPosZ=3;
            t.key=t.cacheKey="motion-sss";t.rgba=body.data();
            t.normalKey=t.normalCacheKey="motion-sss-normal";t.normalRgba=bodyNormal.data();t.normalWidth=t.normalHeight=1;
            t.metallicRoughnessKey=t.metallicRoughnessCacheKey="motion-sss-roughness";
            t.metallicRoughnessRgba=bodyRoughness.data();t.metallicRoughnessWidth=t.metallicRoughnessHeight=1;
            t.occlusionKey=t.occlusionCacheKey="motion-sss-occlusion";t.occlusionRgba=bodyOcclusion.data();t.occlusionWidth=t.occlusionHeight=1;
            if(mode!="sss")t.alphaMode=1;
            if(mode=="sss-hole"){t.key=t.cacheKey="motion-sss-hole";t.rgba=hole.data();}
        }
        if(mode=="masked"){t.key=t.cacheKey="motion-hole";t.rgba=hole.data();t.alphaMode=1;}
        if(mode=="transparent")t.alphaMode=2;
        if(mode=="behind")previousVP[15]=-1;
        std::array<WorldMotionSample,2> samples{{{&previous,0xfedcba98u,mode!="first"&&mode!="class-first"},{&previous,0x12345678u,true}}};
        if(mode.starts_with("skin-instances"))samples[1]=samples[0];
        renderer->beginFrame(0,0,0,1);renderer->beginWorldSceneColorPass(width,height);
        const bool prepared=mode.find("prepared")!=std::string::npos;
        auto& deformation=dynamic_cast<IWorldDeformationPreparation&>(*renderer);
        WorldDeformationSet owned;
        if(mode.find("owned")!=std::string::npos) {
            t.deformation=owned.publish(0,{palette.data(),std::size_t(t.skinMatrixCount)*(t.gpuSkinningMode?32u:16u)});
            previous.deformationVersion=owned.publish(1,previous.deformation);
        }
        if(prepared) {
            WorldSkinPalette current{palette.data(),std::size_t(t.skinMatrixCount)*(t.gpuSkinningMode?32u:16u)};
            WorldSkinPalette prior{previous.deformation};
            if(t.deformation){current=WorldSkinPalette(t.deformation);prior=WorldSkinPalette(previous.deformationVersion);}
            const std::array<WorldSkinPalette,3> all{current,prior,current};
            WorldDeformationBatch batch(*renderer);
            deformation.prepareWorldDeformations({all.data(),1});
            deformation.prepareWorldDeformations({all.data()+1,2});
            check(deformation.worldDeformationPreparationStats().preparedPalettes==0,"producer admission uploaded early");
            batch.commit();
            const auto stats=deformation.worldDeformationPreparationStats();
            check(stats.preparedPalettes==2 && stats.copyJoins==1 && stats.immediatePalettes==0,"palette preparation did not join/coalesce");
        }
        if(t.deformation)palette[12]=-3; // Borrowed producer memory may change after owned admission.
        check(surface.beginWorldSurfaceBuffer(currentVP.data(),width,height),"Motion capture unavailable");
        if(mode!="unenrolled")check(surface.setWorldSurfaceMotion(previousVP,{samples.data(),(mode=="instances" || mode.starts_with("skin-instances"))?2u:1u}),"Motion enrollment failed");
        if(mode=="clear")check(surface.setWorldSurfaceMotion(previousVP,{}),"Motion clear failed");
        if(mode.starts_with("skin-rewrite") || mode.starts_with("skin-capacity")) {
            const auto draw=[&] {
                if(mode.ends_with("cached"))
                    renderer->drawWorldIndexedMeshTexturedCached("motion-rewrite",vertices.data(),vertices.size(),indices.data(),indices.size(),&t,currentVP.data(),width,height);
                else renderer->drawWorldIndexedMeshTextured(vertices.data(),vertices.size(),indices.data(),indices.size(),&t,currentVP.data(),width,height);
            };
            // A temporary pose can reuse the same address within a frame.
            palette[12]=-2;draw();
            if(mode.starts_with("skin-capacity")) {
                for(unsigned i=0;i<258;++i) { palette[12]=4.f+float(i);draw(); }
                palette[12]=-2;draw(); // Retained upload survives uncached overflow draws.
            }
            if(mode.find("interleaved")!=std::string::npos) {
                auto other=palette;other[12]=2;t.skinMatrices=other.data();draw();
                t.skinMatrices=palette.data();draw(); // A/B/A reuses the first upload.
                other[12]=-3;t.skinMatrices=other.data();draw(); // B changes at the same address.
                t.skinMatrices=palette.data();
            }
            palette[12]=.2f;draw();
        } else if(mode.starts_with("skin-instances")) {
            std::array<IRenderBackend::WorldMeshInstance,2> instances{};
            auto offscreen=palette;offscreen[12]=-3;
            if(prepared) {
                const WorldSkinPalette p{offscreen.data(),std::size_t(t.skinMatrixCount)*(t.gpuSkinningMode?32u:16u)};
                deformation.prepareWorldDeformations({&p,1});
            }
            for(auto& instance:instances) {
                instance.modelMatrix=worldMotionIdentity;instance.skinMatrices=palette.data();
                instance.gpuSkinning=1;instance.skinMatrixCount=1;instance.gpuSkinningMode=t.gpuSkinningMode;
            }
            instances[0].skinMatrices=offscreen.data();
            auto instancedTexture=t;instancedTexture.gpuSkinning=0;
            instancedTexture.skinMatrices=nullptr;instancedTexture.skinMatrixCount=0;
            renderer->drawWorldIndexedMeshTexturedCachedInstanced("motion-skin-instances",vertices.data(),vertices.size(),indices.data(),indices.size(),&instancedTexture,
                instances.data(),instances.size(),currentVP.data(),width,height);
        } else if(mode=="instances") {
            std::array<IRenderBackend::WorldMeshInstance,2> instances{};
            instances[0].modelMatrix=shift(-.5f);instances[1].modelMatrix=shift(.5f);
            renderer->drawWorldIndexedMeshTexturedCachedInstanced("motion-quad",vertices.data(),vertices.size(),indices.data(),indices.size(),&t,
                instances.data(),instances.size(),currentVP.data(),width,height);
        } else if(mode.ends_with("cached"))
            renderer->drawWorldIndexedMeshTexturedCached("motion-prepared",vertices.data(),vertices.size(),indices.data(),indices.size(),&t,currentVP.data(),width,height);
        else renderer->drawWorldIndexedMeshTextured(vertices.data(),vertices.size(),indices.data(),indices.size(),&t,currentVP.data(),width,height);
        if(prepared)check(deformation.worldDeformationPreparationStats().immediatePalettes==0,"draw reuploaded prepared current/previous data");
        check(surface.endWorldSurfaceBuffer(),"Motion end failed");
        check(surface.captureWorldSurfaceMotion(),"Motion readback queue failed");
        renderer->endWorldSceneColorPass();renderer->endFrame();
        if(!renderer->handlesPresentation())window.swapBuffers();
        auto image=surface.readWorldSurfaceMotion();
        check(image.width==width&&image.height==height&&image.identity.size()==width*height,"Motion readback extent mismatch");
        check(image.identity[3*width+3]==0,"Background inherited identity");
        const auto verify=[&](int x,std::uint32_t id,float dx,float dy,bool valid) {
            const auto index=(height/2)*width+x;const auto& v=image.velocity[index];
            std::cout<<mode<<" x="<<x<<" id="<<image.identity[index]<<" velocity="<<v[0]<<","<<v[1]<<","<<v[2]<<","<<v[3]<<'\n';
            check(image.identity[index]==id,"Motion identity mismatch");
            check(v[3]==float(valid),"Motion validity mismatch");
            if(id)check(image.temporalMaterial[index]==std::uint8_t(previous.temporalMaterial),"Temporal material metadata lost");
            if(valid)check(std::abs(v[0]-dx)<.0003f&&std::abs(v[1]-dy)<.0003f&&std::abs(v[2]-(mode.starts_with("wind")?.75f:.5f))<.0003f,"Native motion differs from expected projection");
        };
        if(mode=="instances"){verify(width/4,0xfedcba98u,.25f,0,true);verify(width*3/4,0x12345678u,-.25f,0,true);}
        else {
            const bool empty=mode=="masked"||mode=="transparent"||mode=="clear"||mode=="unenrolled"||mode=="sss-hole";
            verify(width/2,empty?0:0xfedcba98u,expectedX,expectedY,!empty&&mode!="first"&&mode!="class-first"&&mode!="behind");
        }
        if(mode.starts_with("wind")) {
            const auto index=(height/2)*width+width/2;
            check(image.materialClass[index]==2,"Masked foliage did not export its evaluated surface");
            const float expected[]{0.f,.035f,1.f,.95f};
            for(unsigned c=0;c<4;++c)
                check(std::abs(float((image.material[index]>>(8*c))&255)/255-expected[c])<=.6f/255+.00001f,
                    "Foliage surface properties differ from the leaf shader");
            check((image.albedo[index]>>30)==1,"Leaf shading was mislabeled standard PBR");
            // Grass n tilts by -0.075 X; shrubs rotate T by the captured shimmer.
            const float a=mode=="wind"?-.075f:0.f,b=mode=="wind-tree"?.4f*(128.f/255+.5f):0.f;
            const float nl=std::sqrt(1+a*a),tl=std::sqrt(1+b*b),ml=std::sqrt(.65f*.65f*2+1);
            const float n[]{a/nl,0,1/nl},tangent[]{mode=="wind-tree"?b/tl:1/nl,mode=="wind-tree"?1/tl:0.f,-a/nl};
            const float bitangent[]{-n[2]*tangent[1],n[2]*tangent[0]-n[0]*tangent[2],n[0]*tangent[1]};
            float normal[3];
            for(int c=0;c<3;++c)normal[c]=-(.65f*tangent[c]+.65f*bitangent[c]+n[c])/ml;
            for(unsigned c=0;c<3;++c)
                check(std::abs(float((image.normal[index]>>(10*c))&1023)/1023-(normal[c]*.5f+.5f))<=.6f/1023+.00002f,
                    ("Foliage surface lost its two-sided mapped normal: channel "+std::to_string(c)+" packed "+std::to_string((image.normal[index]>>(10*c))&1023)+" expected "+std::to_string(normal[c]*.5f+.5f)).c_str());
        }
        if(mode.starts_with("sss")) {
            const auto index=(height/2)*width+width/2;
            check(image.materialClass[index]==(mode=="sss-hole"?0:mode=="sss-masked"?2:1),"Subsurface participation/alpha coverage");
            check((image.albedo[index]>>30)==(mode=="sss-hole"?0u:1u),"Specialized shading was mislabeled standard PBR");
            if(mode!="sss-hole") {
                const float expected[]{0.f,.04f,1.f+.6f*(77.f/255-1.f),.7f*117.f/255};
                for(unsigned c=0;c<4;++c)
                    check(std::abs(float((image.material[index]>>(8*c))&255)/255-expected[c])<=.6f/255+.00001f,"Subsurface evaluated material mismatch");
                const float x=129.f/255,y=1.f/255,z=std::sqrt(1-x*x-y*y);
                // Blue 255 denotes a two-channel map; its Z is reconstructed.
                const float normal[]{x,y,z};
                for(unsigned c=0;c<3;++c) {
                    check(std::abs(float((image.normal[index]>>(10*c))&1023)/1023-(normal[c]*.5f+.5f))<=.6f/1023+.00002f,("Subsurface mapped normal mismatch: channel "+std::to_string(c)+" packed "+std::to_string((image.normal[index]>>(10*c))&1023)+" expected "+std::to_string(normal[c]*.5f+.5f)).c_str());
                    check(std::abs(float((image.albedo[index]>>(10*c))&1023)/1023-std::sqrt(float(body[c])/255))<=.6f/1023+.00002f,"Subsurface evaluated albedo mismatch");
                }
            }
        }
        check(surface.readWorldSurfaceMotion().identity.empty(),"Readback was not consumed");
    }
    renderer->beginFrame(0,0,0,1);renderer->beginWorldSceneColorPass(width,height);
    check(surface.beginWorldSurfaceBuffer(worldMotionIdentity.data(),width,height)&&surface.endWorldSurfaceBuffer(),"Empty surface failed");
    renderer->endWorldSceneColorPass();
    check(!surface.captureWorldSurfaceMotion(),"Readback accepted outside scene");
    renderer->endFrame();
    if(!renderer->handlesPresentation())window.swapBuffers();
    renderer->shutdown();renderer.reset();std::cout<<api<<" native-motion PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
