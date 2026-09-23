#include "ExplorationPresentation.h"
#include "engine/core/IAssetStore.h"
#include "engine/core/Environment.h"
#include "engine/render/Camera3D.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <cstring>
#include <map>

namespace engine::exploration_fixture {
using namespace render::gltf::scene;
namespace {
class BoxStore final : public IAssetStore {
public:
    std::map<std::string,std::vector<std::uint8_t>> files;
    bool readText(const std::string& path,std::string& out,std::string*) const override {
        auto f=files.find(path); if(f==files.end()) return false; out.assign(f->second.begin(),f->second.end()); return true;
    }
    bool readBytes(const std::string& path,std::vector<std::uint8_t>& out,std::string*) const override {
        auto f=files.find(path); if(f==files.end()) return false; out=f->second; return true;
    }
    bool exists(const std::string& path) const override { return files.contains(path); }
};
void require(bool ok, const std::string& error) { if (!ok) throw std::runtime_error(error); }
}
const std::array<glm::vec3,10>& colors() {
    static const std::array<glm::vec3,10> value{{{.16f,.20f,.26f},{.85f,.28f,.12f},{.24f,.34f,.52f},
        {.12f,.68f,.82f},{.68f,.18f,.75f},{.85f,.64f,.15f},{.12f,.32f,.22f},{.10f,.95f,.22f},
        {.95f,.12f,.48f},{.8f,.9f,.95f}}}; return value;
}
glm::mat4 viewProjection(const camera::ExplorationCameraPose& p,int width,int height) {
    Camera3D camera(p.verticalFovDegrees,static_cast<float>(width)/static_cast<float>(height),p.nearPlane,p.farPlane);
    camera.setPosition(p.position); camera.lookAt(p.lookTarget,p.up); return camera.getProjectionMatrix()*camera.getViewMatrix();
}
ExplorationPresentation::ExplorationPresentation(bool outskirt) : outskirtMode_(outskirt) {
    if (outskirtMode_) {
        const auto environment = engine::env::get("PHLOSION_OUTSKIRT_ENVIRONMENT_GLB");
        const auto character = engine::env::get("PHLOSION_OUTSKIRT_WES_GLB");
        require(environment && !environment->empty() && character && !character->empty(),
                "Outskirt preview requires PHLOSION_OUTSKIRT_ENVIRONMENT_GLB and PHLOSION_OUTSKIRT_WES_GLB");
        std::string error;
        require(loadGltfSceneAsset(*environment, outskirt_, &error), error);
        require(sampleGltfScenePose(outskirt_, 0, kBindPoseClip, outskirtPose_, &error), error);
        require(loadGltfSceneAsset(*character, wes_, &error), error);
        require(wes_->animations.size() >= 4, "Wes preview needs idle, walk and run clips");
        using namespace render::animation;
        const PoseSource source{wes_->nodesDefault, wes_->nodeChildren,
                                wes_->sceneRoots, wes_->skins, wes_->animations,
                                wes_->nodeNames};
        AnimationStateGraph graph;
        graph.initialState = "idle";
        graph.parameters = {{"speed", AnimationParameterType::Number, false, 0.0f}};
        graph.states = {{"idle", 1, true, {}}, {"walk", 2, true, {}},
                        {"run", 3, true, {}}};
        graph.transitions = {
            {"idle", "run", "speed", AnimationConditionKind::AtLeast, 3.0f, .15f},
            {"idle", "walk", "speed", AnimationConditionKind::AtLeast, .1f, .15f},
            {"walk", "run", "speed", AnimationConditionKind::AtLeast, 3.0f, .15f},
            {"walk", "idle", "speed", AnimationConditionKind::Below, .1f, .15f},
            {"run", "idle", "speed", AnimationConditionKind::Below, .1f, .15f},
            {"run", "walk", "speed", AnimationConditionKind::Below, 3.0f, .15f}};
        const bool configured = wesMachine_.configure(source, graph, &error);
        require(configured, error);
        return;
    }
    BoxStore store;
    std::vector<float> positions, normals;
    std::vector<std::uint32_t> indices;
    // Face-local bases give six outward CCW faces of the same unit cube.
    for (int face=0;face<6;++face) {
        const int axis=face/2; glm::vec3 n(0),u(0),v(0); n[axis]=(face%2==0)?1.0f:-1.0f;
        u[(axis+1)%3]=1; v=glm::cross(n,u);
        for(const glm::vec2 corner : {glm::vec2(-1,-1),glm::vec2(1,-1),glm::vec2(1,1),glm::vec2(-1,1)}) {
            const auto p=n+corner.x*u+corner.y*v; for(int a=0;a<3;++a) {positions.push_back(p[a]);normals.push_back(n[a]);}
        }
        for(std::uint32_t i : {0u,1u,2u,0u,2u,3u}) indices.push_back(static_cast<std::uint32_t>(face)*4+i);
    }
    auto& bytes=store.files["cube.bin"]; const auto posBytes=positions.size()*sizeof(float); const auto indexBytes=indices.size()*sizeof(std::uint32_t);
    bytes.resize(posBytes*2+indexBytes); std::memcpy(bytes.data(),positions.data(),posBytes);
    std::memcpy(bytes.data()+posBytes,normals.data(),posBytes);std::memcpy(bytes.data()+posBytes*2,indices.data(),indexBytes);
    for(std::size_t color=0;color<assets_.size();++color) {
        const auto c=colors()[color];
        nlohmann::json j={{"asset",{{"version","2.0"}}},{"scene",0},{"scenes",{{{"nodes",{0}}}}},
            {"nodes",{{{"mesh",0}}}},{"buffers",{{{"uri","cube.bin"},{"byteLength",bytes.size()}}}},
            {"bufferViews",{{{"buffer",0},{"byteOffset",0},{"byteLength",posBytes}},{{"buffer",0},{"byteOffset",posBytes},{"byteLength",posBytes}},{{"buffer",0},{"byteOffset",posBytes*2},{"byteLength",indexBytes}}}},
            {"accessors",{{{"bufferView",0},{"componentType",5126},{"count",24},{"type","VEC3"},{"min",{-1,-1,-1}},{"max",{1,1,1}}},
                {{"bufferView",1},{"componentType",5126},{"count",24},{"type","VEC3"}},{{"bufferView",2},{"componentType",5125},{"count",36},{"type","SCALAR"}}}},
            {"materials",{{{"pbrMetallicRoughness",{{"baseColorFactor",{c.x,c.y,c.z,1.0f}},{"metallicFactor",0},{"roughnessFactor",1}}}}}},
            {"meshes",{{{"primitives",{{{"attributes",{{"POSITION",0},{"NORMAL",1}}},{"indices",2},{"material",0}}}}}}}};
        const std::string path="cube-"+std::to_string(color)+".gltf",text=j.dump(); store.files[path]={text.begin(),text.end()};
        std::string error; require(loadGltfSceneAsset(store,path,assets_[color],&error),error);
        require(sampleGltfScenePose(assets_[color],0,kBindPoseClip,poses_[color],&error),error);
    }
}
ExplorationPresentation::~ExplorationPresentation() { release(); }
void ExplorationPresentation::release() {
    if (scopes_ && scope_) { if (!scopes_->requestWorldResourceScopeRelease(scope_)) std::terminate(); scope_=0; scopes_=nullptr; }
}
nlohmann::json ExplorationPresentation::imports() const {
    nlohmann::json j=nlohmann::json::array();
    if (outskirtMode_) {
        for (const auto& a : {outskirt_, wes_})
            j.push_back({{"identity", a->sourceIdentity}, {"primitives", a->stats.primitiveCount},
                         {"vertices", a->stats.vertexCount}, {"indices", a->stats.indexCount},
                         {"materials", a->stats.materialCount},
                         {"geometry_bytes", a->stats.retainedGeometryBytes},
                         {"clips", a->stats.clipCount}});
    } else {
        for(const auto& a:assets_) j.push_back({{"identity",a->sourceIdentity},{"primitives",a->primitives.size()},{"vertices",a->primitives.front().vertices.size()},{"geometry_bytes",a->stats.retainedGeometryBytes}});
    }
    return j;
}
nlohmann::json ExplorationPresentation::animationState() const {
    if (!outskirtMode_) return {};
    return {{"state", wesFrame_.state}, {"transitioning", wesFrame_.transitioning},
            {"base_clip", wesFrame_.pose.baseClipIndex},
            {"target_clip", wesFrame_.pose.targetClipIndex},
            {"weight", wesFrame_.pose.crossfadeWeight}, {"tick", wesLastTick_},
            {"reset", wesLastReset_}};
}
void ExplorationPresentation::draw(IRenderBackend& renderer,const ExplorationSession& s,int width,int height) {
    if(!scope_) { scopes_=dynamic_cast<IWorldResourceScope*>(&renderer); require(scopes_!=nullptr,"Managed world resources required"); scope_=scopes_->createWorldResourceScope(outskirtMode_ ? 256u*1024u*1024u : 16u*1024u*1024u); require(scope_!=0,"Could not create exploration GPU scope"); }
    require(dynamic_cast<IWorldResourceScope*>(&renderer)==scopes_,"Exploration renderer changed without release");
    // Opaque sky covers the whole offscreen surface, also on reset/resize.
    std::array<IRenderBackend::WorldMeshVertex,4> sky{};
    for(std::size_t i=0;i<4;++i) {
        auto& v=sky[i];v.x=(i==0||i==3)?-1.0f:1.0f;v.y=i<2?-1.0f:1.0f;v.z=1;
        v.r=.035f;v.g=.05f;v.b=.085f;v.a=1;v.nz=1;v.tx=1;
    }
    static const std::array<std::uint32_t,6> indices{0,1,2,0,2,3};
    static const std::array<unsigned char,4> white{255,255,255,255};
    IRenderBackend::WorldTextureData texture;texture.key="exploration-sky";texture.cacheKey=texture.key;
    texture.rgba=white.data();texture.width=1;texture.height=1;texture.materialMode=0;texture.depthTestEnabled=0;
    const glm::mat4 identity(1);
    require(scopes_->beginWorldResourceScope(scope_),"Sky scope admission failed");
    renderer.drawWorldIndexedMeshTexturedCached("exploration-sky",sky.data(),sky.size(),indices.data(),indices.size(),&texture,glm::value_ptr(identity),width,height);
    require(scopes_->endWorldResourceScope(),"Sky scope close failed");
    GltfSceneDrawOptions options; const auto vp=viewProjection(s.cameraPose(),width,height);
    std::copy_n(glm::value_ptr(vp),16,options.viewProjectionMatrix.begin()); options.surfaceWidth=width;options.surfaceHeight=height;
    const auto& p=s.cameraPose();options.cameraPosition={p.position.x,p.position.y,p.position.z};options.cameraTarget={p.lookTarget.x,p.lookTarget.y,p.lookTarget.z};options.cameraForward={p.forward.x,p.forward.y,p.forward.z};options.resourceScope=scope_;
    // Public color-coded course, using the existing unlit material mode.
    options.materialMode=0;
    if (outskirtMode_) {
        options.materialMode = 2;
        GltfSceneDrawStats stats;
        std::string error;
        require(drawGltfSceneAsset(renderer, *outskirt_, outskirtPose_, options, stats, &error), error);
        const glm::vec3 center = s.player().center;
        const glm::vec3 velocity = s.player().velocity;
        const float speed = glm::length(glm::vec2(velocity.x, velocity.z));
        if (wesLastReset_ < 0) {
            wesLastReset_ = s.resets();
        } else if (wesLastReset_ != s.resets() || s.ticks() < wesLastTick_) {
            wesMachine_.reset();
            wesFrame_ = {};
            wesLastReset_ = s.resets();
            wesLastTick_ = s.ticks();
        }
        if (wesFrame_.state.empty() || s.ticks() != wesLastTick_) {
            require(wesMachine_.setNumber("speed", speed, &error), error);
            const int elapsedTicks = std::max(0, s.ticks() - wesLastTick_);
            require(wesMachine_.advance(elapsedTicks * fixedDelta, wesFrame_, &error), error);
            wesLastTick_ = s.ticks();
        }
        GltfScenePose pose;
        require(sampleGltfSceneBlendedPose(wes_, wesFrame_.pose, pose, &error), error);
        const glm::mat4 placement = glm::translate(glm::mat4(1),
            glm::vec3(center.x, center.y - .9f, center.z));
        std::copy_n(glm::value_ptr(placement), 16, options.instanceTransform.begin());
        require(drawGltfSceneAsset(renderer, *wes_, pose, options, stats, &error), error);
        return;
    }
    auto draw=[&](glm::vec3 center,glm::vec3 half,float pitch,int color) {
        const glm::mat4 transform=glm::translate(glm::mat4(1),center)*glm::rotate(glm::mat4(1),pitch,glm::vec3(1,0,0))*glm::scale(glm::mat4(1),half);
        std::copy_n(glm::value_ptr(transform),16,options.instanceTransform.begin()); GltfSceneDrawStats stats;std::string error;
        require(drawGltfSceneAsset(renderer,*assets_[color],poses_[color],options,stats,&error),error);
    };
    for(const auto& box:course()) {
        // Sensor volume is authored in course(); its floor projection is the visible membership indicator.
        if(box.sensor) draw({box.center.x,.012f,box.center.z},{box.half.x,.012f,box.half.z},0,s.sensorActive()?7:6);
        else draw(box.center,box.half,box.pitch,box.color);
    }
    draw(s.npc().center,{.3f,.9f,.3f},0,9);
    draw(s.player().center,{.3f,.9f,.3f},0,s.sensorActive()?7:8);
}
void writeHandshake(const std::string& path,const char* host,const std::string& requested,const ExplorationSession& s,
    const ExplorationPresentation& presentation,IRenderBackend& renderer,int width,int height,int renderFrame) {
    if(path.empty())return;
    nlohmann::json j={{"schema","phlosion-exploration-v1"},{"host",host},{"requested_api",requested},{"api",renderer.backendId()},
        {"gpu",renderer.activeGpuName()},{"gpu_discrete",renderer.activeGpuIsDiscrete()},
        {"width",width},{"height",height},{"render_frame",renderFrame},{"frozen",s.ticks()==targetTick},
        {"state",s.state()},{"imports",presentation.imports()}};
    const auto animation = presentation.animationState();
    if (!animation.is_null()) j["animation"] = animation;
    std::ofstream out(path); if(!out)throw std::runtime_error("Cannot write exploration handshake");out<<j.dump(2);
}
} // namespace engine::exploration_fixture
