#pragma once
#include "ExplorationSession.h"
#include "engine/render/AnimationStateMachine.h"
#include "engine/render/gltf/GltfSceneRenderer.h"

namespace engine::exploration_fixture {
const std::array<glm::vec3, 10>& colors();
glm::mat4 viewProjection(const camera::ExplorationCameraPose& pose, int width, int height);
class ExplorationPresentation {
public:
    explicit ExplorationPresentation(bool outskirt = false);
    ~ExplorationPresentation();
    void draw(IRenderBackend& renderer, const ExplorationSession& session, int width, int height);
    void release();
    nlohmann::json imports() const;
    nlohmann::json animationState() const;
private:
    std::array<std::shared_ptr<const render::gltf::scene::GltfSceneAsset>,10> assets_;
    std::array<render::gltf::scene::GltfScenePose,10> poses_;
    std::shared_ptr<const render::gltf::scene::GltfSceneAsset> outskirt_, wes_;
    render::gltf::scene::GltfScenePose outskirtPose_;
    render::animation::AnimationStateMachine wesMachine_;
    render::animation::AnimationStateFrame wesFrame_;
    int wesLastTick_ = 0;
    int wesLastReset_ = -1;
    bool outskirtMode_ = false;
    IWorldResourceScope* scopes_ = nullptr;
    render::WorldResourceScopeId scope_ = 0;
};
void writeHandshake(const std::string& path, const char* host, const std::string& requested,
    const ExplorationSession& session, const ExplorationPresentation& presentation, IRenderBackend& renderer,
    int width, int height, int renderFrame);
} // namespace engine::exploration_fixture
