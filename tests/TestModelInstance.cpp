#include "engine/models/ModelInstance.h"
#include "engine/models/WorldIndexedResources.h"
#include "engine/render/IWorldSurfaceBuffer.h"
#include "engine/models/ModelTracks.h"
#include "engine/models/ModelRenderProvider.h"
#include "engine/models/ModelViewPreparation.h"
#include "engine/models/PhlosionModelObject.h"
#include <chrono>
#include <atomic>
#include <thread>
#include <limits>
#include "engine/render/ModelRenderCollection.h"
#include <optional>
#include <fstream>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <glm/gtc/matrix_transform.hpp>

namespace {
static_assert(!std::is_copy_constructible_v<engine::models::ModelInstance>);
static_assert(std::is_move_constructible_v<engine::models::ModelInstance>);
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
class WarmBackend : public IRenderBackend, public engine::render::IWorldSurfaceBuffer, public engine::render::IWorldDeformationPreparation {
public:
    std::thread::id uploadOwner=std::this_thread::get_id();
    std::function<void()> uploadCheck;
    unsigned uploadCalls=0;
    void beginWorldDeformationBatch() override {}
    void commitWorldDeformationBatch() override {}
    void cancelWorldDeformationBatch() noexcept override {}
    void prepareWorldDeformations(std::span<const engine::render::WorldSkinPalette>) override {
        require(std::this_thread::get_id()==uploadOwner,"GPU admission escaped owner thread");
        ++uploadCalls;if(uploadCheck)uploadCheck();
    }
    engine::render::WorldDeformationPreparationStats worldDeformationPreparationStats()const override{return {};}
    struct MotionRecord {
        engine::render::WorldMotionPose previous;
        bool valid=false;
        std::uint32_t id=0;
        std::vector<float> currentPalette;
        std::array<float,16> currentModel{};
    };
    std::vector<MotionRecord> motionRecords;
    std::optional<MotionRecord> enrolled;
    bool beginWorldSurfaceBuffer(const float*,int,int) override {return true;}
    bool endWorldSurfaceBuffer() override {return true;}
    void clearWorldSurfaceBuffer() override {}
    bool resolveWorldSurfaceContact(const engine::render::WorldContactUniforms&) override {return true;}
    bool resolveWorldSurfaceScreenReflection() override {return true;}
    bool showWorldSurfaceBuffer(unsigned) override {return true;}
    bool setWorldSurfaceMotion(const engine::render::WorldMotionMatrix&,
        std::span<const engine::render::WorldMotionSample> samples) override {
        enrolled.reset();
        if(!samples.empty()) {
            require(samples.size()==1 && samples[0].previous,"Invalid model motion enrollment");
            enrolled=MotionRecord{*samples[0].previous,samples[0].valid,samples[0].imageIdentity};
        }
        return true;
    }
    void recordMotion(const WorldTextureData& t) {
        if(!enrolled)return;
        auto record=*enrolled;record.currentModel=t.modelMatrix;
        if(t.gpuSkinning)record.currentPalette.assign(t.skinMatrices,
            t.skinMatrices+t.skinMatrixCount*(t.gpuSkinningMode ? 32 : 16));
        motionRecords.push_back(std::move(record));
    }
    std::array<float,16> lastProjection{};
    int textures=0,geometry=0,draws=0;
    engine::render::WorldEnvironmentLighting environment;
    bool instancing=false;
    int instancedDraws=0;
    bool supportsWorldIndexedMeshInstancing() const override {return instancing;}
    void drawWorldIndexedMeshTexturedCachedInstanced(const char*,const WorldMeshVertex*,std::size_t,
        const std::uint32_t*,std::size_t,const WorldTextureData* texture,
        const WorldMeshInstance*,std::size_t,const float*,int,int) override {
        ++draws; ++instancedDraws; environment=texture->environment;
    }

    const char* backendId() const override {return "recording";}
    void beginFrame(float,float,float,float) override {}
    void endFrame() override {}
    void onResize(int,int) override {}
    bool requiresOpenGLContext() const override {return false;}
    bool handlesPresentation() const override {return true;}
    void shutdown() override {}
    void prewarmWorldTextureData(const WorldTextureData*) override {++textures;}
    void prewarmWorldIndexedMeshCached(const char*,const WorldMeshVertex*,std::size_t,
        const std::uint32_t*,std::size_t) override {++geometry;}
    void drawWorldIndexedMeshTexturedCached(const char*,const WorldMeshVertex*,std::size_t,
        const std::uint32_t*,std::size_t,const WorldTextureData* texture,const float* matrix,int,int) override {++draws; environment=texture->environment;recordMotion(*texture);std::copy_n(matrix,16,lastProjection.begin());}
};
class SubmissionTraceBackend final : public WarmBackend {
public:
    struct Draw {
        std::string geometry,texture;
        std::vector<float> values;
        std::vector<std::uint32_t> indices;
        bool operator==(const Draw&)const=default;
    };
    std::vector<Draw> trace;
    std::array<std::uint32_t,9> stats{};
    unsigned viewPrepares=0;
    engine::render::backend::WorldPreparedViewToken prepareWorldViewData(const WorldTextureData&)override {
        ++viewPrepares;return {};
    }
    void recordWorldIndexedSubmissionStats(const WorldIndexedSubmissionStats& s)override {
        stats={s.opaqueDraws,s.blendDraws,s.cachedDraws,s.dynamicDraws,s.instancedDraws,
            s.outlineBatches,s.geometrySwitches,s.materialSwitches,s.textureSwitches};
    }
    void capture(const char* key,const WorldTextureData& t,const std::uint32_t* indices,
        std::size_t count,const WorldMeshInstance* instances,std::size_t instanceCount,
        const float* matrix,int width,int height) {
        Draw row;row.geometry=key?key:"";row.texture=t.key?t.key:"";
        row.values={t.cameraPosX,t.cameraPosY,t.cameraPosZ,t.cameraForwardX,t.cameraForwardY,t.cameraForwardZ,
            t.cameraTargetX,t.cameraTargetY,t.cameraTargetZ,t.vertexColorMulR,t.vertexColorMulG,t.vertexColorMulB,t.vertexColorMulA,
            t.emissiveFactorR,t.emissiveFactorG,t.emissiveFactorB,t.roughnessFactor,t.materialTimeSec,
            float(t.alphaMode),float(t.blendMode),float(t.materialMode),float(t.skinMatrixCount),
            float(t.gpuSkinningMode),float(instanceCount),float(width),float(height),
            t.rgba?float(t.rgba[0]):-1.f};
        row.values.insert(row.values.end(),t.modelMatrix.begin(),t.modelMatrix.end());
        row.values.insert(row.values.end(),t.environment.sunRadiance.begin(),t.environment.sunRadiance.end());
        row.values.insert(row.values.end(),matrix,matrix+16);
        if(t.gpuSkinning && t.skinMatrices)row.values.insert(row.values.end(),t.skinMatrices,
            t.skinMatrices+t.skinMatrixCount*(t.gpuSkinningMode?32:16));
        for(std::size_t i=0;i<instanceCount;++i) {
            row.values.insert(row.values.end(),instances[i].modelMatrix.begin(),instances[i].modelMatrix.end());
            row.values.push_back(instances[i].vertexColorMulA);
        }
        row.indices.assign(indices,indices+count);trace.push_back(std::move(row));recordMotion(t);
    }
    void drawWorldIndexedMeshTexturedCached(const char* key,const WorldMeshVertex*,std::size_t,
        const std::uint32_t* i,std::size_t n,const WorldTextureData* t,const float* m,int w,int h)override {
        capture(key,*t,i,n,nullptr,0,m,w,h);
    }
    void drawWorldIndexedMeshTextured(const WorldMeshVertex*,std::size_t,const std::uint32_t* i,
        std::size_t n,const WorldTextureData* t,const float* m,int w,int h)override {
        capture(nullptr,*t,i,n,nullptr,0,m,w,h);
    }
    void drawWorldIndexedMeshTexturedCachedInstanced(const char* key,const WorldMeshVertex*,std::size_t,
        const std::uint32_t* i,std::size_t n,const WorldTextureData* t,const WorldMeshInstance* instances,
        std::size_t count,const float* m,int w,int h)override {
        capture(key,*t,i,n,instances,count,m,w,h);
    }
};

class PreparedBackend : public WarmBackend, public engine::render::IWorldPreparedResources {
public:
    using Token=engine::render::WorldPreparedResourceToken;
    unsigned generation=1,geometryPrepares=0,materialPrepares=0,preparedDraws=0;
    std::uint64_t scope=7;
    bool reject=false;
    Token prepareWorldGeometry(const char*,const WorldMeshVertex*,std::size_t,const std::uint32_t*,std::size_t) override {
        ++geometryPrepares;return reject?Token{}:Token{this,generation,scope,1};
    }
    Token prepareWorldMaterial(const WorldTextureData&) override {
        ++materialPrepares;return reject?Token{}:Token{this,generation,scope,2};
    }
    bool worldPreparedGeometryValid(Token t)const override {
        return t.owner==this && t.generation==generation && t.scope==scope && t.index==1;
    }
    bool worldPreparedMaterialValid(Token t)const override {
        return t.owner==this && t.generation==generation && t.scope==scope && t.index==2;
    }
    bool drawWorldPreparedIndexed(Token g,Token m,const WorldTextureData& t,const float*,int,int)override {
        require(worldPreparedGeometryValid(g)&&worldPreparedMaterialValid(m),"Stale resource reached native draw");
        ++preparedDraws;++draws;environment=t.environment;recordMotion(t);return true;
    }
    engine::render::WorldPreparedResourceStats worldPreparedResourceStats()const override{return {};}
};
class ScopedPreparedBackend final : public PreparedBackend, public IWorldResourceScope {
public:
    bool active=false,failOnDraw=false,failed=false;
    engine::render::WorldResourceScopeId createWorldResourceScope(std::uint64_t)override{return scope;}
    bool beginWorldResourceScope(engine::render::WorldResourceScopeId id)override {
        if(active || id!=scope)return false;active=true;return true;
    }
    bool endWorldResourceScope()override {const bool was=active;active=false;return was;}
    bool requestWorldResourceScopeRelease(engine::render::WorldResourceScopeId)override{return true;}
    bool getWorldResourceScopeStats(engine::render::WorldResourceScopeId id,
        engine::render::WorldResourceScopeStats& out)const override {
        out.id=id;out.status=failed?engine::render::WorldResourceScopeStatus::BudgetExceeded:
            engine::render::WorldResourceScopeStatus::Ready;
        if(failed)out.error="fixture budget exceeded";return id==scope;
    }
    engine::render::WorldResourceStats getWorldResourceStats()const override{return {};}
    bool drawWorldPreparedIndexed(Token g,Token m,const WorldTextureData& t,const float* matrix,int w,int h)override {
        const auto drawn=PreparedBackend::drawWorldPreparedIndexed(g,m,t,matrix,w,h);
        if(failOnDraw)failed=true;return drawn;
    }
};
bool near(float a, float b) { return std::abs(a-b) < 0.0001f; }
struct CountingProfile : engine::models::ModelProfile {
    mutable unsigned samples=0,materials=0;
    void samplePose(const engine::models::MeshData& mesh,const engine::models::ModelFrame& frame,
        engine::models::PoseEval& pose) const override {
        ++samples;ModelProfile::samplePose(mesh,frame,pose);
    }
    void prepareMaterial(const engine::models::MeshData& mesh,const engine::models::PoseEval& pose,
        const engine::models::ModelFrame& frame,std::size_t submesh,
        const engine::models::batches::WorldIndexedBatch& source,
        engine::models::batches::WorldIndexedBatch& destination) const override {
        ++materials;ModelProfile::prepareMaterial(mesh,pose,frame,submesh,source,destination);
    }
};
engine::models::MeshData fixture() {
    using namespace engine::render::model_types;
    engine::models::MeshData mesh;
    mesh.assetCacheIdentity = "animated-lamp";
    mesh.vertices.resize(3);
    mesh.vertices[1].position.x = 1;
    mesh.vertices[2].position.y = 1;
    for (auto& vertex : mesh.vertices) vertex.w0 = 1;
    mesh.indices = {0,1,2}; mesh.triangleSubmesh = {0};
    mesh.triangleNodeIndex = {0}; mesh.triangleSkinIndex = {0};
    mesh.submeshMeshIndex = {0}; mesh.meshIndexToNode = {0};
    mesh.submeshMaterialModes = {2}; mesh.submeshBaseColors = {glm::vec4(1)};
    mesh.nodesDefault.resize(2); mesh.nodeNames = {"mesh", "bulb"};
    mesh.nodeParent = {-1,0}; mesh.nodeChildren = {{1},{}};
    mesh.nodeMesh = {0,-1}; mesh.nodeSkin = {0,-1}; mesh.sceneRoots = {0};
    mesh.bindNodeGlobals.assign(2,glm::mat4(1));
    mesh.skins.push_back({{1},{glm::mat4(1)}});
    AnimationClip clip;
    clip.name = "slide"; clip.durationSec = 1;
    clip.samplers.push_back({{0,1},{glm::vec4(0),glm::vec4(2,0,0,0)},"LINEAR"});
    clip.channels.push_back({0,1,ChannelPath::Translation});
    mesh.animations.push_back(clip);
    return mesh;
}
}
int main() {
    try {
        using namespace engine::models;
        ModelInstance a, b;
        std::string error;
        require(a.setMesh(fixture(), &error), error);
        require(b.setMesh(fixture(), &error), error);
        ModelFrame frame;
        frame.animationIndex = 0;
        frame.animationTime = .25f;
        frame.looping = false;
        frame.modelMatrix = glm::translate(glm::mat4(1), glm::vec3(3,0,0));
        glm::vec3 position;
        require(a.sampleNode(frame, "bulb", position, &error) && near(position.x,3.5f),
            "Fractional CPU attachment position is wrong: " + error);
        glm::mat4 transform;
        auto transformed=frame;
        transformed.modelMatrix=glm::translate(glm::mat4(1),glm::vec3(3,0,0)) *
            glm::rotate(glm::mat4(1),glm::radians(90.f),glm::vec3(0,1,0)) *
            glm::scale(glm::mat4(1),glm::vec3(2,3,4));
        require(a.sampleNodeTransform(transformed,"bulb",transform,&error) &&
            near(transform[3].x,3) && near(transform[3].z,-1) &&
            near(glm::length(glm::vec3(transform[0])),2) &&
            near(glm::length(glm::vec3(transform[1])),3),
            "Attachment matrix lost orientation, scale or animated translation");
        const auto preserved=transform;
        require(!a.sampleNodeTransform(transformed,"missing",transform,&error) && transform==preserved,
            "Failed attachment changed output");
        auto bright=frame;bright.tint={2,1,1,1};
        const auto* brightBatch=a.prepare(bright,2,&error);
        require(brightBatch && near((*brightBatch)[0].vertexColorMulR,2),"Double-intensity tint rejected");
        bright.tint.a=1.1f;require(!a.prepare(bright,2,&error),"HDR tint allowed invalid alpha");
        const auto* first = a.prepare(frame, 0, &error);
        require(first && first->size() == 1 && (*first)[0].gpuSkinning &&
            near((*first)[0].sharedSkinMatrices[12],.5f) && near((*first)[0].modelMatrix[12],3),
            "Render palette and CPU attachment disagree: " + error);
        const float* retained = (*first)[0].sharedSkinMatrices;
        frame.animationTime = .75f; frame.pairedSkinMatrices = true;
        const auto* second = a.prepare(frame, 1, &error);
        require(second && (*second)[0].gpuSkinningMode == 1 && (*second)[0].skinMatrices.empty() && (*second)[0].skinMatrixCount == 1 &&
            near((*second)[0].sharedSkinMatrices[12],1.5f) && near(retained[12],.5f),
            "Same-frame slots overwrite a previous palette");
        frame.animationTime = .9f;
        require(b.prepare(frame,0,&error) && near(retained[12],.5f), "Instances share mutable palettes");
        frame.animationTime = 9;
        require(a.sampleNode(frame,"bulb",position,&error) && near(position.x,5),
            "One-shot endpoint wrapped into the first pose");
        frame.looping = true;
        require(a.sampleNode(frame,"bulb",position,&error) && near(position.x,3),
            "Loop boundary did not restart the clip");
        const auto oldIdentity = a.mesh()->assetCacheIdentity;
        auto bad = fixture(); bad.submeshMaterialModes[0] = 255;
        require(!a.setMesh(bad,&error) && !error.empty() && a.mesh()->assetCacheIdentity == oldIdentity &&
            near(retained[12],.5f), "Unsupported profile destroyed the previous resource");
        bad = fixture(); bad.indices[1] = 50;
        require(!a.setMesh(bad,&error) && a.mesh()->assetCacheIdentity == oldIdentity,
            "Invalid geometry replaced the previous resource");
        const glm::vec3 sentinel(42);
        position = sentinel;
        require(!a.sampleNode(frame,"missing",position,&error) && position == sentinel,
            "Failed node query modified its result");
        require(a.setMesh(fixture(),&error) && a.mesh()->assetCacheIdentity != oldIdentity,
            "Replacement reused stale geometry identity");
        a.clear(); require(!a.mesh() && !a.prepare(frame,0,&error), "Unloaded instance still renders");
        MeshVisibilityTrack step{0,60,{0,10.0f/60},{1,0}};
        require(sampleVisibilityTrack(step, std::fmod(70.0f/60,1.0f)) == 0 &&
            sampleVisibilityTrack(step,.1f) == 1, "Source-frame visibility boundary drifted");
        {
            auto profile=std::make_shared<CountingProfile>();ModelInstance source(profile);
            auto mesh=fixture();AnimatedBillboard billboard;
            billboard.nodeIndex=1;billboard.size={.2f,.3f};
            billboard.texture.width=billboard.texture.height=1;billboard.texture.rgba={255,255,255,255};
            billboard.clipScales={{{{0,1},{1,1}}}};mesh.animatedBillboards.push_back(billboard);
            require(source.setMesh(std::move(mesh),&error),error);
            ModelFrame input;input.animationIndex=0;input.animationTime=.25f;
            ModelCollectedFrame collected;
            require(source.collect(input,collected,&error),error);
            source.clear();
            std::vector<batches::WorldIndexedBatch> front,side;
            require(collected.prepareView({0,0,5},{0,1,0},{0,0,1},front,&error),error);
            require(collected.prepareView({8,0,0},{0,1,0},{1,0,0},side,&error),error);
            require(profile->samples==1 && profile->materials==1,"Views repeated pose/material collection");
            require(front.size()==2 && side.size()==2 && near(front[0].sharedSkinMatrices[12],.5f) &&
                front[0].skinMatrices.empty() && side[0].skinMatrices.empty() &&
                front[0].sharedSkinMatrices==side[0].sharedSkinMatrices,"Collected palette lifetime/rebinding failed");
            require(front[0].sharedVertices[1].x==side[0].sharedVertices[1].x &&
                front[0].sortDepth!=side[0].sortDepth,"Collected geometry or camera sorting failed");
            require(near(front[1].vertices[0].nz,1) && near(side[1].vertices[0].nx,1),
                "Collected billboards did not face their own view");
            ModelViewPreparation views(collected);
            engine::render::WorldEnvironmentLighting environment;
            environment.enabled=true;environment.sunRadiance={1,2,3};
            int lightingBuilds=0;
            ModelLighting lighting{.prepare=[&](auto& batches,int preset,std::string& reason) {
                ++lightingBuilds;
                if(preset==99){reason="failed preparation";return false;}
                for(auto& batch:batches)batch.emissiveFactorR=float(preset)+batch.environment.sunRadiance[0];
                return true;
            }};
            const auto resolve=[&](glm::vec3 camera,int preset=1) {
                return views.resolve(camera,{0,1,0},{0,0,1},environment,lighting,preset,error);
            };
            const auto* firstView=resolve({0,0,5});
            WarmBackend planBackend;
            const auto* firstPlan=&firstView->submission(planBackend,true);
            require(firstPlan==&firstView->submission(planBackend,true) && views.submissionStats().plans==1,
                "Collected submission plan was rebuilt for the same policy");
            planBackend.instancing=true;
            require(firstPlan!=&firstView->submission(planBackend,true) && views.submissionStats().plans==2 &&
                firstPlan==&firstView->submission(planBackend,false),"Instancing policy shared an incompatible submission plan");
            require(firstView && firstView==resolve({0,0,5}) && lightingBuilds==1 &&
                views.stats.builds==1 && views.stats.reuses==1,"Identical native view repeated preparation");
            const auto* otherView=views.resolve({8,0,0},{0,1,0},{1,0,0},environment,lighting,1,error);
            require(otherView && firstView!=otherView && near(otherView->batches[1].vertices[0].nx,1) &&
                near(firstView->batches[1].vertices[0].nz,1),"Prepared views shared mutable billboard state");
            require(firstView==resolve({0,0,5}) && near(firstView->batches[0].sharedSkinMatrices[12],.5f),
                "Alternating views lost collected pose or cached storage");
            environment.sunRadiance[0]=7;
            const auto* relit=resolve({0,0,5});
            require(relit && relit!=firstView && relit->batches[0].emissiveFactorR==8 &&
                firstView->batches[0].emissiveFactorR==2,"Relighting reused or mutated an earlier view");
            const auto* changedPreset=resolve({0,0,5},2);
            require(changedPreset && changedPreset->batches[0].emissiveFactorR==9,
                "Lighting preset did not invalidate preparation");
            const auto builds=views.stats.builds;
            require(!resolve({0,0,5},99) && error=="failed preparation" &&
                views.stats.builds==builds,"Failed lighting was published");
            require(!resolve({std::numeric_limits<float>::quiet_NaN(),0,5}) &&
                views.stats.builds==builds,"Invalid camera was published");
            require(relit==resolve({0,0,5}) && error.empty(),"Failure destroyed an earlier view");
            for(unsigned i=0;i<ModelViewPreparation::capacity+2;++i)
                require(resolve({float(i+20),0,5})!=nullptr,"Bounded view replacement failed");
            require(resolve({0,0,5}) && views.stats.builds>builds,"Evicted view did not rebuild");
            ModelCollectedFrame newer;
            input.animationTime=.75f;
            require(source.setMesh(fixture(),&error) && source.collect(input,newer,&error),error);
            ModelViewPreparation newerViews(newer);
            const auto* fresh=newerViews.resolve({0,0,5},{0,1,0},{0,0,1},environment,lighting,1,error);
            require(fresh && near(fresh->batches[0].sharedSkinMatrices[12],1.5f) &&
                near(resolve({0,0,5})->batches[0].sharedSkinMatrices[12],.5f),
                "New collection reused old animation or damaged retained snapshot");
            input.animationTime=.25f;
            const float retainedX=side[1].vertices[0].x;
            require(!collected.prepareView({std::numeric_limits<float>::quiet_NaN(),0,0},{0,1,0},{0,0,1},side,&error) &&
                side[1].vertices[0].x==retainedX,"Invalid view modified a published result");
            require(source.setMesh(fixture(),&error),error);
            input.animationTime=-1;
            require(!source.collect(input,collected,&error) &&
                collected.prepareView({0,0,5},{0,1,0},{0,0,1},side,&error) &&
                near(side[0].sharedSkinMatrices[12],.5f),"Failed recollection destroyed a retained snapshot");
        }
        // Exercise the actual runtime ABI without any editor, source adapter or GPU.
        {engine::render::ModelRenderPluginApi old;
         require(!createModelRenderProvider(23,&old),"Provider accepted pre-preparation ABI 23");}
        namespace fs = std::filesystem;
        const auto root = fs::temp_directory_path() / ("phlosion_provider_" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        struct Cleanup {
            fs::path root;
            ~Cleanup() { std::error_code ec; fs::remove_all(root, ec); }
        } cleanup{root};
        ModelCookStats stats;
        auto model = fixture();
        model.submeshIndexOffset = {0}; model.submeshIndexCount = {3};
        require(cookModelObject("lamp", model, root.string(), "Model", stats, &error), error);
        const auto path = objectPathForModel("lamp", root.string());
        engine::render::ModelRenderPluginApi api;
        require(!createModelRenderProvider(5, &api) && !api.context,
            "Provider accepted an incompatible ABI");
        require(!createModelRenderProvider(7, &api) && !api.context,
            "Provider accepted pre-environment renderer ABI 7");
        require(!createModelRenderProvider(8, &api) && !api.context,
            "Provider accepted a frame without the environment tail");
        require(!createModelRenderProvider(9, &api) && !api.context,
            "Provider accepted the pre-celestial frame layout");
        require(!createModelRenderProvider(11, &api) && !api.context, "Provider accepted pre-shadow ABI 11");
        require(!createModelRenderProvider(12, &api) && !api.context, "Provider accepted pre-cloud ABI 12");
        require(!createModelRenderProvider(13, &api) && !api.context, "Provider accepted pre-rain ABI 13");
        require(!createModelRenderProvider(14, &api) && !api.context, "Provider accepted pre-surface ABI 14");
        require(!createModelRenderProvider(10, &api) && !api.context,
            "Provider accepted the pre-point-light frame layout");
        require(createModelRenderProvider(engine::render::modelRenderPluginAbi, &api), "Provider creation failed");
        struct Release {
            engine::render::ModelRenderPluginApi& api;
            ~Release() { api.destroy(api.context); }
        } release{api};
        WarmBackend backend;
        engine::render::ModelRenderFrame warm;
        warm.renderer=&backend; warm.assetId="lamp"; warm.phloPath=path.c_str();
        warm.animation="slide"; warm.animationTime=.25f;
        warm.width=640; warm.height=360; warm.cameraPosition[2]=5; warm.prewarmOnly=true;
        require(api.render(api.context,&warm) && backend.textures>0 && backend.geometry>0 && backend.draws==0,
            "Preparation must upload resources without drawing");
        const int preparedGeometry=backend.geometry;
        require(api.render(api.context,&warm) && backend.geometry==preparedGeometry*2 && backend.draws==0,
            "Repeated preparation unexpectedly draws");
        warm.prewarmOnly=false;
        warm.environment.enabled=true; warm.environment.sunRadiance={1,2,3};
        warm.environment.moonRadiance={.1f,.2f,.3f};warm.environment.starIntensity=.4f;
        warm.environment.pointLights[0]={{1,2,3},8,{4,5,6}};
        require(api.render(api.context,&warm) && backend.draws>0, "Prepared model failed to render");
        require(backend.environment.enabled && backend.environment.sunRadiance==warm.environment.sunRadiance,
            "Runtime provider dropped shared world lighting");
        require(backend.environment.moonRadiance==warm.environment.moonRadiance && backend.environment.starIntensity==.4f,
            "Runtime provider dropped celestial inputs");
        require(backend.environment.pointLights[0].intensity==std::array<float,3>{4,5,6},
            "Runtime provider dropped point lights");
        const auto unjittered=backend.lastProjection;
        warm.projectionOffset[0]=.002f;warm.projectionOffset[1]=-.003f;
        require(api.render(api.context,&warm),"Native jitter draw failed");
        for(unsigned col=0;col<4;++col) {
            require(near(backend.lastProjection[col*4],unjittered[col*4]+.002f*unjittered[col*4+3]) &&
                near(backend.lastProjection[col*4+1],unjittered[col*4+1]-.003f*unjittered[col*4+3]),
                "Native provider projection offset is not clip-space jitter");
        }
        warm.projectionOffset[0]=warm.projectionOffset[1]=0;
        warm.environment={};
        require(api.render(api.context,&warm) && !backend.environment.enabled,
            "World lighting leaked into a later inspection draw");
        warm.environment.enabled=true;
        // Actual provider -> prepared palettes -> sorted batches -> native packet.
        // A second scene object may share the asset without sharing history.
        engine::render::WorldMotionHistory history;
        engine::render::WorldMotionView view;
        view.identity=1;view.generation=1;view.width=640;view.height=360;
        warm.motionHistory=&history;warm.motionObject=41;
        warm.prewarmOnly=false;warm.animationTime=.25f;
        view.frame=1;
        {engine::render::WorldMotionCapture capture(history,view);
         require(api.render(api.context,&warm),"First native model motion failed");
         warm.motionObject=42;warm.position[0]=5;
         require(api.render(api.context,&warm),"Second model instance failed");
         capture.commit();}
        require(backend.motionRecords.size()==2 && !backend.motionRecords[0].valid &&
            !backend.motionRecords[1].valid && backend.motionRecords[0].id!=backend.motionRecords[1].id,
            "First visibility or per-object identity is wrong");
        const auto firstId=backend.motionRecords[0].id;
        // Auxiliary sampling overwrites the provider's pose slot, never history.
        warm.motionHistory=nullptr;warm.animationTime=.9f;
        require(api.render(api.context,&warm),"Auxiliary pose draw failed");
        warm.motionHistory=&history;warm.animationTime=.75f;view.frame=2;
        {engine::render::WorldMotionCapture capture(history,view);
         warm.motionObject=42;warm.position[0]=6;
         require(api.render(api.context,&warm),"Reordered second object failed");
         warm.motionObject=41;warm.position[0]=1;
         require(api.render(api.context,&warm),"Reordered first object failed");
         capture.commit();}
        const auto& moved=backend.motionRecords.back();
        require(moved.valid && moved.id==firstId && near(moved.previous.model[12],0) &&
            near(moved.currentModel[12],1) && near(moved.previous.deformation[12],.5f) &&
            near(moved.currentPalette[12],1.5f),"Owned previous native pose was overwritten or mismatched");
        require(!backend.enrolled,"Model draw leaked native enrollment");
        view.frame=3;
        {engine::render::WorldMotionCapture capture(history,view);
         const auto count=backend.motionRecords.size();
         warm.prewarmOnly=true;require(api.render(api.context,&warm),"Motion prewarm failed");
         warm.prewarmOnly=false;warm.translucent=true;require(api.render(api.context,&warm),"Motion trail failed");
         require(backend.motionRecords.size()==count,"Prewarm/trail promoted opaque motion");
         capture.commit();}
        warm.translucent=false;view.frame=4;
        {engine::render::WorldMotionCapture capture(history,view);
         require(api.render(api.context,&warm),"Reappearing model failed");capture.commit();}
        require(!backend.motionRecords.back().valid,"Visibility gap reused native model history");
        view.frame=5;view.reset=true;
        {engine::render::WorldMotionCapture capture(history,view);
         require(api.render(api.context,&warm),"Cut model failed");capture.commit();}
        require(!backend.motionRecords.back().valid,"Cut reused native model history");
        require(!api.render(api.context,&warm),"Motion outside a capture was accepted");
        for (auto oldAbi : {6u,19u,20u,21u,22u}) {
            engine::render::ModelRenderPluginApi incompatible;
            require(!createModelRenderProvider(oldAbi,&incompatible) && !incompatible.context,
                "Provider accepted an incompatible renderer vtable");
        }
        warm.motionHistory=nullptr;warm.motionObject=0;warm.position[0]=0;
        {
            auto profile=std::make_shared<CountingProfile>();
            engine::render::ModelRenderPluginApi collectedApi,other;
            require(createModelRenderProvider(engine::render::modelRenderPluginAbi,&collectedApi,profile) &&
                createModelRenderProvider(engine::render::modelRenderPluginAbi,&other),"Collected providers failed");
            Release releaseCollected{collectedApi},releaseOther{other};
            warm.animationTime=.25f;
            std::uint64_t id=77;
            require(!collectedApi.collect(collectedApi.context,&warm,&id) && id==77,"Inactive collection accepted");
            {
                engine::render::ModelRenderCollection family(collectedApi),otherFamily(other);
                require(!collectedApi.beginCollection(collectedApi.context),"Nested collection accepted");
                id=family.collect(warm);
                require(!other.renderCollected(other.context,id,&warm),"Foreign collection handle accepted");
                warm.animationTime=.9f;warm.position[0]=8;warm.phloPath="missing.phlo";
                warm.environment.sunRadiance={3,4,5};
                family.draw(id,warm);warm.cameraPosition[0]=2;family.draw(id,warm);
                require(profile->samples==1 && profile->materials==1 &&
                    backend.environment.sunRadiance==warm.environment.sunRadiance,
                    "Collected draw resampled or dropped live lighting");
                engine::render::WorldMotionHistory collectionHistory;
                engine::render::WorldMotionView collectionView;
                collectionView.identity=9;collectionView.generation=1;collectionView.width=640;collectionView.height=360;
                collectionView.frame=1;
                warm.motionHistory=&collectionHistory;warm.motionObject=10;
                {engine::render::WorldMotionCapture capture(collectionHistory,collectionView);
                 family.draw(id,warm);capture.commit();}
                require(near(backend.motionRecords.back().currentModel[12],0) &&
                    near(backend.motionRecords.back().currentPalette[12],.5f),
                    "Draw-time pose/placement replaced the collected snapshot");
                warm.motionHistory=nullptr;warm.phloPath=path.c_str();warm.translucent=true;
                auto ghost=family.collect(warm);warm.translucent=false;
                warm.motionHistory=&collectionHistory;collectionView.frame=2;
                const auto count=backend.motionRecords.size();
                {engine::render::WorldMotionCapture capture(collectionHistory,collectionView);
                 family.draw(ghost,warm);capture.commit();}
                require(backend.motionRecords.size()==count,"Collected ghost wrote opaque motion");
                warm.motionHistory=nullptr;
            }
            require(!collectedApi.renderCollected(collectedApi.context,id,&warm),"Expired handle accepted");
            {engine::render::ModelRenderCollection family(collectedApi);
             require(family.collect(warm)!=id && !collectedApi.renderCollected(collectedApi.context,id,&warm),
                "Handle reused in a later family");}
            warm.position[0]=0;warm.cameraPosition[0]=0;
        }

        // Reloading the same path at a frozen pose must invalidate cached shadows,
        // while already collected handles retain the version they actually sampled.
        {
            engine::render::ModelRenderPluginApi versioned;
            require(createModelRenderProvider(engine::render::modelRenderPluginAbi,&versioned),"Versioned provider creation failed");
            Release releaseVersioned{versioned};
            auto request=warm;request.phloPath=path.c_str();request.animationTime=0;request.motionHistory=nullptr;
            const auto stamp=std::filesystem::last_write_time(path);
            std::uint64_t oldHandle=0;
            {
                engine::render::ModelRenderCollection family(versioned);
                oldHandle=family.collect(request);
                const auto oldRevision=family.resourceRevision(oldHandle);
                require(oldRevision && family.resourceRevision(family.collect(request))==oldRevision,"Unchanged resource revision changed");
                std::filesystem::last_write_time(path,stamp+std::chrono::seconds(2));
                const auto replacement=family.collect(request);
                require(family.resourceRevision(replacement)!=oldRevision &&
                    family.resourceRevision(oldHandle)==oldRevision,"Same-path reload lost captured resource identity");
                require(versioned.collectedResourceRevision(versioned.context,~oldHandle)==0,"Unknown resource handle accepted");
            }
            require(versioned.collectedResourceRevision(versioned.context,oldHandle)==0,"Expired resource version remained queryable");
            std::filesystem::last_write_time(path,stamp);
        }

        // Joined native preparation must consume copied inputs, never render or
        // sample again, and must reject an unprepared/unadmitted draw.
        {
            auto profile=std::make_shared<CountingProfile>();
            std::atomic<int> callbacks{0},active{0};
            std::atomic<bool> failPreparation{true},throwPreparation{false};
            ModelLighting lighting{.prepare=[&](auto& batches,int preset,std::string&) {
                ++callbacks;++active;
                struct Exit{std::atomic<int>& n;~Exit(){--n;}} exit{active};
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                if(throwPreparation && preset==3)throw std::runtime_error("joined preparation exception");
                if(failPreparation)return false; // No supplied error must still fail.
                batches[0].emissiveFactorR=float(preset);
                return true;
            }};
            engine::render::ModelRenderPluginApi planned;
            require(createModelRenderProvider(engine::render::modelRenderPluginAbi,&planned,profile,lighting),
                "Planned provider creation failed");
            Release plannedRelease{planned};
            auto plannedView=warm;plannedView.phloPath=path.c_str();plannedView.translucent=false;
            plannedView.lightingPreset=3;plannedView.environment.sunRadiance={2,3,4};
            plannedView.cameraPosition[0]=0;plannedView.motionHistory=nullptr;
            engine::render::ModelRenderCollection family(planned);
            const auto objectA=family.collect(plannedView),objectB=family.collect(plannedView);
            const auto beforeDraws=backend.draws;
            family.plan(objectA,plannedView);family.plan(objectB,plannedView);family.plan(objectA,plannedView);
            auto changed=plannedView;changed.cameraPosition[0]=4;changed.lightingPreset=2;
            family.plan(objectA,changed);family.plan(objectB,changed);
            require(!planned.renderCollected(planned.context,objectA,&plannedView),"Draw accepted before join");
            require(!planned.prepareCollected(planned.context) && active==0,
                "Failed callback published readiness or left jobs running");
            failPreparation=false;throwPreparation=true;
            require(!planned.prepareCollected(planned.context) && active==0,
                "Thrown callback escaped the join");
            throwPreparation=false;
            bool failUpload=true;
            backend.uploadCheck=[&]{
                require(active==0,"GPU upload started before preparation jobs joined");
                if(failUpload)throw std::runtime_error("simulated upload failure");
            };
            require(!planned.prepareCollected(planned.context),"Upload failure published readiness");
            require(!planned.renderCollected(planned.context,objectA,&plannedView),"Draw consumed failed deformation preparation");
            failUpload=false;
            family.prepare();
            const auto built=callbacks.load();
            require(built>=4 && active==0 && backend.draws==beforeDraws && profile->samples==2,
                "Preparation drew, resampled or failed to join");
            family.prepare();require(callbacks==built,"Repeated preparation rebuilt views");
            const auto uploaded=backend.uploadCalls;
            plannedView.phloPath="no-longer-readable";plannedView.animationTime=20;plannedView.position[0]=100;
            family.draw(objectA,plannedView);family.draw(objectB,changed);
            plannedView.width+=31;plannedView.height+=17;plannedView.verticalFovDegrees=48;
            family.draw(objectA,plannedView);
            require(callbacks==built && profile->samples==2 && backend.uploadCalls==uploaded &&
                backend.environment.sunRadiance==plannedView.environment.sunRadiance,
                "Draw lazily prepared or froze live camera/material inputs");
            auto missing=plannedView;missing.cameraPosition[0]=7;
            require(!planned.renderCollected(planned.context,objectA,&missing) &&
                !planned.planCollected(planned.context,objectA,&missing),
                "Unadmitted or post-join view accepted");
            std::uint64_t rejected=0;
            require(!planned.collect(planned.context,&plannedView,&rejected),"Post-plan collection accepted");
            backend.uploadCheck={};
        }
        {
            engine::render::ModelRenderPluginApi bounded;
            require(createModelRenderProvider(engine::render::modelRenderPluginAbi,&bounded),"Bounded provider failed");
            Release boundedRelease{bounded};
            engine::render::ModelRenderCollection family(bounded);
            auto boundedView=warm;boundedView.phloPath=path.c_str();boundedView.motionHistory=nullptr;
            auto object=family.collect(boundedView);
            for(std::size_t i=0;i<ModelViewPreparation::capacity;++i) {
                boundedView.cameraPosition[0]=float(i);family.plan(object,boundedView);
            }
            boundedView.cameraPosition[0]=100;
            require(!bounded.planCollected(bounded.context,object,&boundedView),"Unbounded planned views accepted");
            family.prepare();boundedView.cameraPosition[0]=0;family.draw(object,boundedView);
        }

        {
            engine::render::ModelRenderPluginApi prewarm;
            require(createModelRenderProvider(engine::render::modelRenderPluginAbi,&prewarm),"Prewarm provider failed");
            Release prewarmRelease{prewarm};engine::render::ModelRenderCollection family(prewarm);
            auto prewarmView=warm;prewarmView.phloPath=path.c_str();prewarmView.motionHistory=nullptr;prewarmView.prewarmOnly=true;
            const auto before=backend.uploadCalls;
            const auto object=family.collect(prewarmView);family.plan(object,prewarmView);family.prepare();
            require(backend.uploadCalls==before,"Prewarm-only collection attempted a frame upload");
        }
        std::vector<batches::WorldIndexedBatch> submissions(2);
        for (auto& batch : submissions) {
            batch.geometryCacheKey="shared-static-triangle";
            batch.vertices.resize(3); batch.indices={0,1,2};
            batch.environment.enabled=true;
        }
        submissions[1].environment.sunRadiance={1,2,3};
        backend.instancing=true;
        const int beforeSeparate=backend.draws;
        const glm::mat4 identity(1);
        batches::submitWorldIndexedBatches(backend,submissions,&identity[0][0],640,360);
        require(backend.draws==beforeSeparate+2 && backend.instancedDraws==0,
            "Instancing merged different world environments");
        submissions[0].environment=submissions[1].environment;
        batches::submitWorldIndexedBatches(backend,submissions,&identity[0][0],640,360);
        require(backend.instancedDraws==1 && backend.environment.enabled,
            "Matching world environments no longer share instanced geometry");
        submissions[0].environment.sunDirection[0]=-0.0f;
        batches::submitWorldIndexedBatches(backend,submissions,&identity[0][0],640,360);
        require(backend.instancedDraws==2,
            "Equivalent signed-zero environment values did not share instancing");
        submissions[1].environment.pointLights[0]={{1,2,3},8,{4,5,6}};
        const int beforePointSeparate=backend.draws;
        batches::submitWorldIndexedBatches(backend,submissions,&identity[0][0],640,360);
        require(backend.draws==beforePointSeparate+2 && backend.instancedDraws==2,
            "Instancing merged different point lights");
        submissions[0].environment=submissions[1].environment;
        batches::submitWorldIndexedBatches(backend,submissions,&identity[0][0],640,360);
        require(backend.instancedDraws==3,"Matching point lights prevented instancing");
        submissions[1].directionalShadowCaster = 0;
        const int beforeShadowSeparate = backend.draws;
        batches::submitWorldIndexedBatches(backend,submissions,&identity[0][0],640,360);
        require(backend.draws==beforeShadowSeparate+2 && backend.instancedDraws==3,
            "Instancing merged incompatible shadow caster flags");
        submissions[0].directionalShadowCaster = 0;
        submissions[1].directionalShadowReceiver = 0;
        const int beforeReceiverSeparate = backend.draws;
        batches::submitWorldIndexedBatches(backend,submissions,&identity[0][0],640,360);
        require(backend.draws==beforeReceiverSeparate+2 && backend.instancedDraws==3,
            "Instancing merged incompatible shadow receiver flags");
        submissions[0].directionalShadowReceiver = 0;
        batches::submitWorldIndexedBatches(backend,submissions,&identity[0][0],640,360);
        require(backend.instancedDraws==4,"Matching shadow flags prevented instancing");


        submissions[1].environment.rectLights[0]={{0,4,0},12,{1,0,0},{0,0,.2f},{4,2,1}};
        const int beforeRectSeparate=backend.draws;
        batches::submitWorldIndexedBatches(backend,submissions,&identity[0][0],640,360);
        require(backend.draws==beforeRectSeparate+2 && backend.instancedDraws==4,
            "Instancing merged different rectangular emitters");
        submissions[0].environment=submissions[1].environment;
        batches::submitWorldIndexedBatches(backend,submissions,&identity[0][0],640,360);
        require(backend.instancedDraws==5,"Matching rectangle lights prevented instancing");

        {
            // Compare actual submitted descriptors and ordering, including owned
            // auto-instance storage, rather than duplicating preparation logic.
            std::vector<batches::WorldIndexedBatch> input(6);
            for(unsigned i=0;i<input.size();++i) {
                auto& inputBatch=input[i];inputBatch.vertices.resize(3);inputBatch.indices={0,1,2};
                inputBatch.geometryCacheKey="part-"+std::to_string(i);inputBatch.textureKey="texture-"+std::to_string(i);
                inputBatch.textureWidth=inputBatch.textureHeight=1;inputBatch.ownedTextureRgba={static_cast<unsigned char>(i+40),0,0,255};
                inputBatch.modelMatrix[12]=float(i);inputBatch.sortDepth=float(i);
                inputBatch.environment.enabled=true;inputBatch.environment.sunRadiance={1,2,3};
            }
            input[0].preserveSubmissionOrder=true;
            input[1].geometryCacheKey.clear();
            input[2].alphaMode=2;input[4].alphaMode=2;
            input[5]=input[3];input[5].modelMatrix[12]=5;
            // Stable texture identities permit the ordinary auto-instance group.
            input[3].textureCacheKey=input[5].textureCacheKey="stable-shared-texture";
            for(bool instanced:{false,true}) {
                SubmissionTraceBackend ordinary,cached;ordinary.instancing=cached.instancing=instanced;
                batches::PreparedWorldIndexedSubmission plan(cached,input,true);
                for(unsigned pass=0;pass<3;++pass) {
                    const float cameraPosition[]{float(pass),2,3},forward[]{0,0,-float(pass+1)},target[]{4,5,float(pass)};
                    auto matrix=identity;matrix[3][0]=float(pass)*0.1f;
                    ordinary.trace.clear();cached.trace.clear();
                    batches::submitWorldIndexedBatches(ordinary,input,&matrix[0][0],640+pass,360,
                        cameraPosition,forward,target);
                    plan.submit(cached,&matrix[0][0],640+pass,360,cameraPosition,forward,target,nullptr,0,nullptr);
                    require(ordinary.trace==cached.trace && ordinary.stats==cached.stats,
                        "Prepared native descriptors/order differ from ordinary submission");
                    require(plan.drawCount()==cached.trace.size(),"Prepared plan draw count mismatch");
                }
                require(ordinary.viewPrepares==cached.viewPrepares,"Prepared plan retained stale native view tokens");
                if(instanced) {
                    cached.instancing=false;bool rejected=false;
                    try{plan.submit(cached,&identity[0][0],640,360,nullptr,nullptr,nullptr,nullptr,0,nullptr);}
                    catch(const std::invalid_argument&){rejected=true;}
                    require(rejected,"Prepared instancing plan accepted an incompatible renderer");
                }
            }
        }
        {
            PreparedBackend resourceBackend;
            batches::WorldIndexedResources resources;resources.bind(&resourceBackend,resourceBackend.scope);
            std::vector<batches::WorldIndexedBatch> draws(1);
            auto& resourceBatch=draws[0];resourceBatch.geometryCacheKey="native-test";resourceBatch.vertices.resize(3);resourceBatch.indices={0,1,2};
            resourceBatch.environment.enabled=true;
            const auto submit=[&] {
                batches::submitWorldIndexedBatches(resourceBackend,draws,&identity[0][0],640,360,
                    nullptr,nullptr,nullptr,nullptr,0,&resources);
            };
            submit();resourceBatch.environment.sunRadiance={3,4,5};submit();
            require(resourceBackend.geometryPrepares==1 && resourceBackend.materialPrepares==1 && resourceBackend.preparedDraws==2 &&
                resourceBackend.environment.sunRadiance==resourceBatch.environment.sunRadiance,"Native resources rebuilt or froze live lighting");
            ++resourceBackend.generation;submit();
            require(resourceBackend.geometryPrepares==2 && resourceBackend.materialPrepares==2,"Stale native bindings were not rebuilt");
            std::array<unsigned char,4> pixel{255,255,255,255};
            resourceBatch.textureKey="material-a";resourceBatch.textureCacheKey=resourceBatch.textureKey;resourceBatch.textureRgba=pixel.data();resourceBatch.textureWidth=resourceBatch.textureHeight=1;
            submit();resourceBatch.textureKey="material-resourceBatch";resourceBatch.textureCacheKey=resourceBatch.textureKey;submit();
            resourceBatch.textureWrapS=33071;submit();
            require(resourceBackend.geometryPrepares==2 && resourceBackend.materialPrepares==5,"Texture/sampling override reused wrong material");
            const auto prepared=resourceBackend.preparedDraws;
            resourceBatch.textureKey.clear();resourceBatch.textureCacheKey.clear();submit();
            require(resourceBackend.preparedDraws==prepared && static_cast<unsigned>(resourceBackend.draws)==prepared+1,"Anonymous texture failed to use ordinary fallback");
            resourceBatch.textureRgba=nullptr;resourceBatch.textureWidth=resourceBatch.textureHeight=0;
            resourceBatch.geometryCacheKey.clear();submit();
            require(resourceBackend.preparedDraws==prepared,"Dynamic geometry used an immutable handle");
            resourceBatch.geometryCacheKey="native-test";++resourceBackend.generation;resourceBackend.reject=true;submit();
            require(resourceBackend.preparedDraws==prepared,"Allocation failure submitted a prepared fallback");
            resourceBackend.reject=false;++resourceBackend.scope;resources.bind(&resourceBackend,resourceBackend.scope);submit();
            require(resourceBackend.preparedDraws==prepared+1,"Scope replacement did not recover");
            PreparedBackend another;resources.bind(&another,another.scope);
            batches::submitWorldIndexedBatches(another,draws,&identity[0][0],640,360,
                nullptr,nullptr,nullptr,nullptr,0,&resources);
            require(another.geometryPrepares==1 && another.materialPrepares==1,"Renderer replacement reused foreign handles");
            // Replacing an invalid handle must still work when the bounded cache is full.
            engine::render::WorldMaterialBindingCache<unsigned> full;
            engine::render::WorldMaterialBindingView key;key.scope=7;
            for(unsigned i=0;i<full.capacity;++i) {
                auto name=std::to_string(i);key.textures[0]=name;full.insert(key,1);
            }
            key.textures[0]="0";full.insert(key,2);
            require(full.size()==full.capacity && *full.find(key)==2,"Full native cache refused a stale-token replacement");
            warm.resourceScope=7;
            require(!api.render(api.context,&warm),"Provider accepted an unsupported resource scope");
            ScopedPreparedBackend scoped;warm.renderer=&scoped;warm.resourceScope=scoped.scope;
            require(api.render(api.context,&warm) && scoped.preparedDraws>0 && !scoped.active,
                "Provider failed managed submission or left its resource scope open");
            scoped.failOnDraw=true;
            require(!api.render(api.context,&warm) && !scoped.active &&
                std::string(api.lastError(api.context)).find("fixture budget exceeded")!=std::string::npos,
                "Provider hid a resource allocation failure or leaked scope admission");
            warm.renderer=&backend;warm.resourceScope=0;
        }

        engine::render::ModelNodePoseRequest request;
        request.assetId = "lamp"; request.phloPath = path.c_str(); request.nodeName = "bulb";
        engine::render::ModelNodePoseResult result;
        require(api.sampleNodePose(api.context, &request, &result) && near(result.worldPosition[0], 0),
            "Static bind-pose query requires an animation");
        request.animation = "slide"; request.animationTime = .25f;
        require(api.sampleNodePose(api.context, &request, &result) && near(result.worldPosition[0], .5f),
            "Provider disagrees with the instance's fractional pose");
        request.animation = "missing";
        result.worldPosition[0] = 42;
        require(!api.sampleNodePose(api.context, &request, &result) && result.worldPosition[0] == 42 &&
            *api.lastError(api.context), "Failed provider query changed output or lost its error");
        request.animation = "slide";
        const auto broken = root / "broken.phlo";
        std::ofstream(broken) << "invalid";
        const auto brokenPath = broken.string();
        request.phloPath = brokenPath.c_str();
        require(!api.sampleNodePose(api.context, &request, &result), "Provider accepted a corrupt replacement");
        request.phloPath = path.c_str();
        require(api.sampleNodePose(api.context, &request, &result) && near(result.worldPosition[0], .5f) &&
            !*api.lastError(api.context), "Failed replacement damaged the previous resource or left a stale error");
        model.animations[0].samplers[0].outputs[1].x = 4;
        require(cookModelObject("other-lamp", model, root.string(), "Model", stats, &error), error);
        const auto replacement = objectPathForModel("other-lamp", root.string());
        request.phloPath = replacement.c_str();
        require(api.sampleNodePose(api.context, &request, &result) && near(result.worldPosition[0], 1),
            "Same-id provider replacement returned a stale pose");
        std::cout << "Model instance pose/palette/slot/replacement/profile/visibility contracts passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
