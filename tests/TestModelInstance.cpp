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
        const std::uint32_t*,std::size_t,const WorldTextureData*,const float*,int,int) override {++draws;}
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
        require(api.render(api.context,&warm) && backend.draws>0, "Prepared model failed to render");
        engine::render::ModelRenderPluginApi legacy;
        require(createModelRenderProvider(6,&legacy) && legacy.abi==6, "ABI 6 clients lost compatibility");
        warm.prewarmOnly=true; const int previousDraws=backend.draws;
        require(legacy.render(legacy.context,&warm) && backend.draws>previousDraws,
            "ABI 6 provider read the new frame tail");
        legacy.destroy(legacy.context);
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
