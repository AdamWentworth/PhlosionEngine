#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <thread>

namespace engine::camera {

struct BattleCameraTarget {
    glm::vec3 position{0.0f};
    float radius = 0.0f;
};

struct BattleCameraShot {
    float yawRadians = 0.0f;
    float pitchRadians = 0.2f;
    glm::vec3 focusOffset{0.0f};
    float verticalFovDegrees = 45.0f;
    float marginFraction = 0.1f;
    float minDistance = 1.0f;
    float maxDistance = 100.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
};

struct BattleCameraPose {
    glm::vec3 position{0.0f};
    glm::vec3 lookTarget{0.0f};
    glm::vec3 forward{0.0f, 0.0f, -1.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    glm::vec3 right{1.0f, 0.0f, 0.0f};
    glm::vec3 shakeOffset{0.0f};
    float yawRadians = 0.0f;
    float pitchRadians = 0.0f;
    float distance = 0.0f;
    float aspectRatio = 16.0f / 9.0f;
    float verticalFovDegrees = 45.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    float blendProgress = 1.0f;
};

struct BattleCameraShake {
    float amplitudeWorld = 0.1f;
    float frequencyHz = 8.0f;
    float durationSeconds = 0.3f;
    std::uint32_t seed = 0u;
};

// Fits one to eight target spheres into the shot's perspective frustum. All
// bounds, including the near/far planes and configured maximum distance, are
// enforced; an impossible framing request fails without changing `out`.
bool frameBattleTargets(const BattleCameraShot& shot,
                        std::span<const BattleCameraTarget> targets,
                        float aspectRatio,
                        BattleCameraPose& out,
                        std::string* error = nullptr);

// A per-instance, owner-thread camera sequence. Cuts and blend starts clear
// shake. Frame targets may move on each step; blends approach the current goal.
class BattleCameraRig {
public:
    BattleCameraRig();
    ~BattleCameraRig();
    BattleCameraRig(const BattleCameraRig&) = delete;
    BattleCameraRig& operator=(const BattleCameraRig&) = delete;
    BattleCameraRig(BattleCameraRig&&) = delete;
    BattleCameraRig& operator=(BattleCameraRig&&) = delete;

    bool pose(BattleCameraPose& out, std::string* error = nullptr) const;
    bool cut(const BattleCameraShot& shot,
             std::span<const BattleCameraTarget> targets,
             float aspectRatio,
             BattleCameraPose& out,
             std::string* error = nullptr);
    bool blendTo(const BattleCameraShot& shot,
                 std::span<const BattleCameraTarget> targets,
                 float aspectRatio,
                 float durationSeconds,
                 BattleCameraPose& out,
                 std::string* error = nullptr);
    bool step(std::span<const BattleCameraTarget> targets,
              float aspectRatio,
              float deltaSeconds,
              bool reducedMotion,
              BattleCameraPose& out,
              std::string* error = nullptr);
    bool startShake(const BattleCameraShake& shake,
                    std::string* error = nullptr);
    bool stopShake(BattleCameraPose& out, std::string* error = nullptr);
    void reset();

private:
    void checkOwner() const;
    const std::thread::id owner_;
    std::optional<BattleCameraShot> shot_;
    std::optional<BattleCameraPose> basePose_;
    std::optional<BattleCameraPose> pose_;
    std::optional<BattleCameraPose> blendSource_;
    float blendDuration_ = 0.0f;
    float blendElapsed_ = 0.0f;
    std::optional<BattleCameraShake> shake_;
    float shakeElapsed_ = 0.0f;
};

} // namespace engine::camera
