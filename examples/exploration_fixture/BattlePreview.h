#pragma once

#include "engine/camera/BattleCamera.h"
#include "engine/render/gltf/GltfSceneRenderer.h"

#include <array>
#include <nlohmann/json.hpp>

namespace engine::exploration_fixture {

inline constexpr float battleFixedDelta = 1.0f / 60.0f;

class BattlePreview {
public:
    BattlePreview();
    ~BattlePreview();
    BattlePreview(const BattlePreview&) = delete;
    BattlePreview& operator=(const BattlePreview&) = delete;

    void draw(IRenderBackend& renderer, int tick, int width, int height);
    void release();
    nlohmann::json imports() const;
    nlohmann::json state(int tick, std::array<int, 4> viewport) const;

private:
    void frameCamera(int tick, float aspect);

    std::shared_ptr<const render::gltf::scene::GltfSceneAsset> field_, wes_;
    render::gltf::scene::GltfScenePose fieldPose_;
    camera::BattleCameraRig camera_;
    camera::BattleCameraPose cameraPose_;
    std::string phase_ = "wide";
    IWorldResourceScope* scopes_ = nullptr;
    render::WorldResourceScopeId scope_ = 0;
};

void writeBattleHandshake(const std::string& path, const char* host,
                          const std::string& requested, const BattlePreview& preview,
                          IRenderBackend& renderer, int tick, int width, int height,
                          int renderFrame, std::array<int, 4> viewport);

} // namespace engine::exploration_fixture
