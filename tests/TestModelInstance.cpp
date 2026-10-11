#include "engine/models/ModelInstance.h"
#include "engine/models/ModelTracks.h"
#include "engine/models/ModelRenderProvider.h"
#include "engine/models/PhlosionModelObject.h"
#include <chrono>
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
class WarmBackend final : public IRenderBackend {
public:
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
        const std::uint32_t*,std::size_t,const WorldTextureData* texture,const float*,int,int) override {++draws; environment=texture->environment;}
};
bool near(float a, float b) { return std::abs(a-b) < 0.0001f; }
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
        const auto* first = a.prepare(frame, 0, &error);
        require(first && first->size() == 1 && (*first)[0].gpuSkinning &&
            near((*first)[0].skinMatrices[12],.5f) && near((*first)[0].modelMatrix[12],3),
            "Render palette and CPU attachment disagree: " + error);
        const float* retained = (*first)[0].skinMatrices.data();
        frame.animationTime = .75f; frame.pairedSkinMatrices = true;
        const auto* second = a.prepare(frame, 1, &error);
        require(second && (*second)[0].gpuSkinningMode == 1 && (*second)[0].skinMatrices.size() == 32 &&
            near((*second)[0].skinMatrices[12],1.5f) && near(retained[12],.5f),
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
        // Exercise the actual runtime ABI without any editor, source adapter or GPU.
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
        warm.environment={};
        require(api.render(api.context,&warm) && !backend.environment.enabled,
            "World lighting leaked into a later inspection draw");
        warm.environment.enabled=true;
        engine::render::ModelRenderPluginApi legacy;
        require(createModelRenderProvider(6,&legacy) && legacy.abi==6, "ABI 6 clients lost compatibility");
        warm.prewarmOnly=true; const int previousDraws=backend.draws;
        require(legacy.render(legacy.context,&warm) && backend.draws>previousDraws,
            "ABI 6 provider read the new frame tail");
        require(!backend.environment.enabled,"ABI 6 read the environment tail");
        legacy.destroy(legacy.context);
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
