#pragma once

#include "AnimationFixtureCase.h"
#include "AnimationFixtureRig.h"

#include "engine/render/IRenderBackend.h"

#include <array>
#include <string>
#include <vector>

namespace engine::animation_fixture {

struct FixtureDrawStats {
    int drawCalls = 0;
    int triangles = 0;
    bool skinningSubmitted = false;
};

// One sampled pose plus its caller-owned palette. The palette is exactly
// inverse(meshTransform) * jointGlobal * inverseBind per joint, which is the
// space the native world skinning path consumes.
struct FixturePose {
    FixtureCase fixtureCase = FixtureCase::Bind;
    int clipIndex = -1;
    float sampleTimeSec = 0.0f;
    std::vector<engine::render::model_types::NodeTRS> nodeLocals;
    std::vector<glm::mat4> nodeGlobals;
    std::vector<float> skinMatrices;
};

// Samples the shared CPU pose sampler for one case and builds the palette.
bool buildFixturePose(FixtureCase fixtureCase,
                      FixturePose& outPose,
                      std::string* outError = nullptr);

// Caller-owned palette storage with one stable allocation. Hosts fill it with
// the bind palette and then overwrite the same buffer with the requested pose,
// so the pointer submitted to the renderer never changes while the contents do.
class FixturePaletteStorage {
public:
    explicit FixturePaletteStorage(std::size_t matrixCount = 0u);

    void setFrom(const FixturePose& pose);

    const float* data() const noexcept { return values_.data(); }
    std::size_t floatCount() const noexcept { return values_.size(); }
    std::size_t matrixCount() const noexcept { return values_.size() / 16u; }

    // Address of the shared allocation, recorded in the handshake so a swapped
    // buffer between the bind and requested draws is detectable.
    std::string address() const;
    std::vector<float> snapshot() const { return values_; }

private:
    std::vector<float> values_;
};

// Evidence recorded by a host around the bind -> requested transition.
struct FixturePaletteTransition {
    std::string paletteAddress;
    std::string bindDrawAddress;
    std::string poseDrawAddress;
    bool contentsChangedAfterBind = false;

    void recordBindDraw(const FixturePaletteStorage& storage);
    void recordPoseDraw(const FixturePaletteStorage& storage,
                        const std::vector<float>& bindPalette);
};

// Same camera and resolution contract for both hosts and every API.
std::array<float, 16> fixtureViewProjection(int surfaceWidth, int surfaceHeight);

bool renderAnimationFixture(const FixtureRig& rig,
                            const FixturePaletteStorage& palette,
                            IRenderBackend& renderer,
                            int surfaceWidth,
                            int surfaceHeight,
                            FixtureDrawStats* outStats = nullptr,
                            std::string* outError = nullptr);

struct FixtureHandshake {
    std::string host;
    std::string caseName;
    std::string requestedBackend;
    std::string actualBackend;
    std::string gpu;
    bool gpuDiscrete = false;
    int surfaceWidth = 0;
    int surfaceHeight = 0;
    int logicalFrame = 0;
    std::string clipName;
    int clipIndex = -1;
    float sampleTimeSec = 0.0f;
    float expectedAngleDegrees = 0.0f;
    int bindFrames = 0;
    int poseFrames = 0;
    std::array<float, 16> meshTransform{};
    std::vector<float> palette;
    std::vector<float> bindPalette;
    std::vector<float> nodeGlobals;
    std::string paletteAddress;
    std::string bindDrawPaletteAddress;
    std::string poseDrawPaletteAddress;
    bool paletteContentsChanged = false;
    std::vector<std::array<float, 3>> vertexPositions;
    std::vector<std::array<int, 4>> vertexJoints;
    std::vector<std::array<float, 4>> vertexWeights;
    std::vector<std::array<float, 3>> jointBindOrigin;
};

FixtureHandshake buildFixtureHandshake(std::string host,
                                       std::string requestedBackend,
                                       const IRenderBackend& renderer,
                                       const FixturePose& pose,
                                       const FixturePose& bindPose,
                                       const FixturePaletteTransition& transition,
                                       int surfaceWidth,
                                       int surfaceHeight,
                                       int bindFrames,
                                       int poseFrames);

bool writeFixtureHandshake(const std::string& path,
                           const FixtureHandshake& handshake,
                           std::string* outError = nullptr);

const char* fixtureBackendName(const IRenderBackend& renderer) noexcept;

} // namespace engine::animation_fixture
