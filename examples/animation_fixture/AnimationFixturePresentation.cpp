#include "AnimationFixturePresentation.h"

#include "engine/render/ModelPoseSampler.h"

#include <nlohmann/json.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

namespace engine::animation_fixture {
namespace {

using engine::render::animation::PoseSource;
using engine::render::animation::samplePose;
using engine::render::model_types::SkinData;

bool fail(std::string* outError, std::string message) {
    if (outError) {
        *outError = std::move(message);
    }
    return false;
}

std::array<float, 16> toArray(const glm::mat4& matrix) {
    std::array<float, 16> out{};
    const float* source = glm::value_ptr(matrix);
    std::copy(source, source + 16, out.begin());
    return out;
}

std::vector<float> flatten(const std::vector<glm::mat4>& matrices) {
    std::vector<float> out;
    out.reserve(matrices.size() * 16u);
    for (const glm::mat4& matrix : matrices) {
        const float* source = glm::value_ptr(matrix);
        out.insert(out.end(), source, source + 16);
    }
    return out;
}

engine::render::backend::WorldMeshVertex toWorldVertex(
    const FixtureVertexSpec& vertex) {
    engine::render::backend::WorldMeshVertex out;
    out.x = vertex.position.x;
    out.y = vertex.position.y;
    out.z = vertex.position.z;
    out.u = vertex.uv.x;
    out.v = vertex.uv.y;
    out.r = vertex.color.r;
    out.g = vertex.color.g;
    out.b = vertex.color.b;
    out.a = 1.0f;
    out.nx = 0.0f;
    out.ny = 0.0f;
    out.nz = 1.0f;
    out.joint0 = static_cast<float>(vertex.joints[0]);
    out.joint1 = static_cast<float>(vertex.joints[1]);
    out.joint2 = static_cast<float>(vertex.joints[2]);
    out.joint3 = static_cast<float>(vertex.joints[3]);
    out.weight0 = vertex.weights[0];
    out.weight1 = vertex.weights[1];
    out.weight2 = vertex.weights[2];
    out.weight3 = vertex.weights[3];
    out.tx = 1.0f;
    out.ty = 0.0f;
    out.tz = 0.0f;
    out.tw = 1.0f;
    return out;
}

const unsigned char kWhitePixel[4] = {255u, 255u, 255u, 255u};

std::string pointerText(const float* pointer) {
    char buffer[32] = {};
    std::snprintf(
        buffer,
        sizeof(buffer),
        "0x%llx",
        static_cast<unsigned long long>(
            reinterpret_cast<std::uintptr_t>(pointer)));
    return std::string(buffer);
}

} // namespace

bool buildFixturePose(FixtureCase fixtureCase,
                      FixturePose& outPose,
                      std::string* outError) {
    const FixtureRig& rig = fixtureRig();
    const FixtureCaseInfo& info = fixtureCaseInfo(fixtureCase);

    int clipIndex = -1;
    if (std::string(info.clipName) == "linear") {
        clipIndex = rig.linearClipIndex;
    } else if (std::string(info.clipName) == "step") {
        clipIndex = rig.stepClipIndex;
    }
    if (clipIndex < 0 || clipIndex >= static_cast<int>(rig.animations.size())) {
        return fail(
            outError,
            std::string("animation fixture case '") + info.name +
                "' references an unknown clip");
    }

    FixturePose pose;
    pose.fixtureCase = fixtureCase;
    pose.clipIndex = clipIndex;
    pose.sampleTimeSec = info.timeSec;

    const PoseSource source{
        rig.nodesDefault,
        rig.nodeChildren,
        rig.sceneRoots,
        rig.skins,
        rig.animations,
        rig.nodeNames};
    samplePose(
        source,
        info.timeSec,
        clipIndex,
        pose.nodeLocals,
        pose.nodeGlobals,
        true);

    const SkinData& skin = rig.skins[static_cast<std::size_t>(rig.skinIndex)];
    if (skin.joints.empty() ||
        skin.inverseBind.size() != skin.joints.size()) {
        return fail(outError, "animation fixture skin has inconsistent joints");
    }
    const glm::mat4 inverseMesh = glm::inverse(rig.meshTransform);
    std::vector<glm::mat4> matrices;
    matrices.reserve(skin.joints.size());
    for (std::size_t index = 0; index < skin.joints.size(); ++index) {
        const int jointNode = skin.joints[index];
        if (jointNode < 0 ||
            jointNode >= static_cast<int>(pose.nodeGlobals.size())) {
            matrices.push_back(glm::mat4(1.0f));
            continue;
        }
        matrices.push_back(
            inverseMesh * pose.nodeGlobals[static_cast<std::size_t>(jointNode)] *
            skin.inverseBind[index]);
    }
    pose.skinMatrices = flatten(matrices);

    outPose = std::move(pose);
    if (outError) {
        outError->clear();
    }
    return true;
}

FixturePaletteStorage::FixturePaletteStorage(std::size_t matrixCount)
    : values_(matrixCount * 16u, 0.0f) {}

void FixturePaletteStorage::setFrom(const FixturePose& pose) {
    // In-place value update: the allocation, and therefore the submitted
    // pointer, is unchanged for the whole run.
    values_ = pose.skinMatrices;
}

std::string FixturePaletteStorage::address() const {
    return pointerText(values_.data());
}

void FixturePaletteTransition::recordBindDraw(
    const FixturePaletteStorage& storage) {
    paletteAddress = storage.address();
    bindDrawAddress = paletteAddress;
}

void FixturePaletteTransition::recordPoseDraw(
    const FixturePaletteStorage& storage,
    const std::vector<float>& bindPalette) {
    paletteAddress = storage.address();
    poseDrawAddress = paletteAddress;
    const std::vector<float> current = storage.snapshot();
    contentsChangedAfterBind =
        current.size() == bindPalette.size() &&
        !std::equal(
            current.begin(), current.end(), bindPalette.begin(),
            [](float left, float right) {
                return std::abs(left - right) <= 1.0e-6f;
            });
}

std::array<float, 16> fixtureViewProjection(int surfaceWidth,
                                            int surfaceHeight) {
    const int width = std::max(1, surfaceWidth);
    const int height = std::max(1, surfaceHeight);
    const float aspect =
        static_cast<float>(width) / static_cast<float>(height);
    const glm::mat4 projection = glm::perspective(
        glm::radians(fixtureCameraFovDegrees()),
        aspect,
        fixtureCameraNearPlane(),
        fixtureCameraFarPlane());
    const glm::mat4 view = glm::lookAt(
        fixtureCameraPosition(),
        fixtureCameraTarget(),
        glm::vec3(0.0f, 1.0f, 0.0f));
    return toArray(projection * view);
}

bool renderAnimationFixture(const FixtureRig& rig,
                            const FixturePaletteStorage& palette,
                            IRenderBackend& renderer,
                            int surfaceWidth,
                            int surfaceHeight,
                            FixtureDrawStats* outStats,
                            std::string* outError) {
    if (surfaceWidth <= 0 || surfaceHeight <= 0) {
        return fail(outError, "animation fixture render requires a positive surface");
    }
    if (rig.vertices.size() < 3u || rig.indices.size() < 3u) {
        return fail(outError, "animation fixture rig has no drawable geometry");
    }

    const int width = std::max(1, surfaceWidth);
    const int height = std::max(1, surfaceHeight);
    const std::array<float, 16> viewProjection =
        fixtureViewProjection(width, height);
    FixtureDrawStats stats{};

    // Background first so the viewport is detectable inside editor chrome.
    {
        const glm::vec3 minimum(
            rig.backgroundExtentMin[0],
            rig.backgroundExtentMin[1],
            rig.backgroundExtentMin[2]);
        const glm::vec3 maximum(
            rig.backgroundExtentMax[0],
            rig.backgroundExtentMax[1],
            rig.backgroundExtentMax[2]);
        const glm::vec3 color(
            rig.backgroundColor[0], rig.backgroundColor[1], rig.backgroundColor[2]);
        const std::array<glm::vec3, 4> corners{
            glm::vec3(minimum.x, minimum.y, minimum.z),
            glm::vec3(maximum.x, minimum.y, minimum.z),
            glm::vec3(maximum.x, maximum.y, maximum.z),
            glm::vec3(minimum.x, maximum.y, maximum.z)};
        std::vector<engine::render::backend::WorldMeshVertex> vertices;
        vertices.reserve(corners.size());
        const std::array<std::array<float, 2>, 4> uvs{{
            {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}}};
        for (std::size_t index = 0; index < corners.size(); ++index) {
            engine::render::backend::WorldMeshVertex vertex;
            vertex.x = corners[index].x;
            vertex.y = corners[index].y;
            vertex.z = corners[index].z;
            vertex.u = uvs[index][0];
            vertex.v = uvs[index][1];
            vertex.r = color.r;
            vertex.g = color.g;
            vertex.b = color.b;
            vertex.a = 1.0f;
            vertex.nz = 1.0f;
            vertex.tx = 1.0f;
            vertex.tw = 1.0f;
            vertices.push_back(vertex);
        }
        const std::array<std::uint32_t, 6> indices{0u, 1u, 2u, 0u, 2u, 3u};
        engine::render::backend::WorldTextureData texture;
        texture.key = "fixture/anim/background";
        texture.cacheKey = "fixture/anim/background";
        texture.rgba = kWhitePixel;
        texture.width = 1;
        texture.height = 1;
        texture.textureSrgb = 1u;
        texture.alphaMode = 0u;
        texture.depthTestEnabled = 1u;
        texture.materialMode = 0u;
        renderer.drawWorldIndexedMeshTexturedCached(
            "fixture/anim/background",
            vertices.data(),
            vertices.size(),
            indices.data(),
            indices.size(),
            &texture,
            viewProjection.data(),
            width,
            height);
        ++stats.drawCalls;
        stats.triangles += 2;
    }

    // Bind vertices plus the caller-owned palette: the vertex data is never
    // deformed on the CPU.
    {
        std::vector<engine::render::backend::WorldMeshVertex> vertices;
        vertices.reserve(rig.vertices.size());
        for (const FixtureVertexSpec& spec : rig.vertices) {
            vertices.push_back(toWorldVertex(spec));
        }
        const std::size_t jointCount =
            rig.skins[static_cast<std::size_t>(rig.skinIndex)].joints.size();
        if (palette.matrixCount() != jointCount) {
            return fail(
                outError,
                "animation fixture palette storage does not match the skin joints");
        }
        engine::render::backend::WorldTextureData texture;
        texture.key = "fixture/anim/strip";
        texture.cacheKey = "fixture/anim/strip";
        texture.rgba = kWhitePixel;
        texture.width = 1;
        texture.height = 1;
        texture.textureSrgb = 1u;
        texture.alphaMode = 0u;
        texture.depthTestEnabled = 1u;
        texture.materialMode = 0u;
        texture.modelMatrix = toArray(rig.meshTransform);
        texture.gpuSkinning = 1u;
        texture.gpuSkinningMode = 0u;
        texture.skinMatrixCount = static_cast<std::uint32_t>(jointCount);
        texture.skinMatrices = palette.data();
        renderer.drawWorldIndexedMeshTexturedCached(
            "fixture/anim/strip",
            vertices.data(),
            vertices.size(),
            rig.indices.data(),
            rig.indices.size(),
            &texture,
            viewProjection.data(),
            width,
            height);
        ++stats.drawCalls;
        stats.triangles += static_cast<int>(rig.indices.size() / 3u);
        stats.skinningSubmitted = true;
    }

    if (outStats) {
        *outStats = stats;
    }
    if (outError) {
        outError->clear();
    }
    return true;
}

FixtureHandshake buildFixtureHandshake(std::string host,
                                       std::string requestedBackend,
                                       const IRenderBackend& renderer,
                                       const FixturePose& pose,
                                       const FixturePose& bindPose,
                                       const FixturePaletteTransition& transition,
                                       int surfaceWidth,
                                       int surfaceHeight,
                                       int bindFrames,
                                       int poseFrames) {
    const FixtureRig& rig = fixtureRig();
    const FixtureCaseInfo& info = fixtureCaseInfo(pose.fixtureCase);
    FixtureHandshake handshake;
    handshake.host = std::move(host);
    handshake.caseName = info.name;
    handshake.requestedBackend = std::move(requestedBackend);
    handshake.actualBackend = fixtureBackendName(renderer);
    handshake.gpu = renderer.activeGpuName();
    handshake.gpuDiscrete = renderer.activeGpuIsDiscrete();
    handshake.surfaceWidth = surfaceWidth;
    handshake.surfaceHeight = surfaceHeight;
    handshake.logicalFrame = info.logicalFrame;
    handshake.clipName = info.clipName;
    handshake.clipIndex = pose.clipIndex;
    handshake.sampleTimeSec = pose.sampleTimeSec;
    handshake.expectedAngleDegrees = info.expectedAngleDegrees;
    handshake.bindFrames = bindFrames;
    handshake.poseFrames = poseFrames;
    handshake.meshTransform = toArray(rig.meshTransform);
    handshake.palette = pose.skinMatrices;
    handshake.bindPalette = bindPose.skinMatrices;
    handshake.nodeGlobals = flatten(pose.nodeGlobals);
    handshake.paletteAddress = transition.paletteAddress;
    handshake.bindDrawPaletteAddress = transition.bindDrawAddress;
    handshake.poseDrawPaletteAddress = transition.poseDrawAddress;
    handshake.paletteContentsChanged = transition.contentsChangedAfterBind;
    for (const FixtureVertexSpec& vertex : rig.vertices) {
        handshake.vertexPositions.push_back(
            {vertex.position.x, vertex.position.y, vertex.position.z});
        handshake.vertexJoints.push_back(vertex.joints);
        handshake.vertexWeights.push_back(vertex.weights);
    }
    // Authored bind origins of the two joints, used by the guard to check that
    // submitted vertices are bound where the palette expects them.
    for (const NodeTRS& node : rig.nodesDefault) {
        handshake.jointBindOrigin.push_back({node.t.x, node.t.y, node.t.z});
    }
    return handshake;
}

bool writeFixtureHandshake(const std::string& path,
                           const FixtureHandshake& handshake,
                           std::string* outError) {
    if (path.empty()) {
        return fail(outError, "animation fixture handshake path must not be empty");
    }
    nlohmann::json document;
    document["schema"] = "phlosion-animation-fixture-handshake-v1";
    document["host"] = handshake.host;
    document["case"] = handshake.caseName;
    document["requested_backend"] = handshake.requestedBackend;
    document["actual_backend"] = handshake.actualBackend;
    document["gpu"] = handshake.gpu;
    document["gpu_discrete"] = handshake.gpuDiscrete;
    document["surface"] = {
        {"width", handshake.surfaceWidth},
        {"height", handshake.surfaceHeight}};
    document["animation"] = {
        {"clip", handshake.clipName},
        {"clip_index", handshake.clipIndex},
        {"sample_time_sec", handshake.sampleTimeSec},
        {"logical_frame", handshake.logicalFrame},
        {"expected_angle_degrees", handshake.expectedAngleDegrees}};
    document["presentation"] = {
        {"bind_frames", handshake.bindFrames},
        {"pose_frames", handshake.poseFrames},
        {"geometry_key", "fixture/anim/strip"},
        {"gpu_skinning", true},
        {"joint_count", handshake.palette.size() / 16u},
        {"skin_matrix_count", handshake.palette.size() / 16u},
        {"palette_address", handshake.paletteAddress},
        {"bind_draw_palette_address", handshake.bindDrawPaletteAddress},
        {"pose_draw_palette_address", handshake.poseDrawPaletteAddress},
        {"palette_contents_changed", handshake.paletteContentsChanged}};
    document["mesh_transform"] = handshake.meshTransform;
    document["palette"] = handshake.palette;
    document["bind_palette"] = handshake.bindPalette;
    document["node_globals"] = handshake.nodeGlobals;
    document["joint_bind_origin"] = handshake.jointBindOrigin;
    nlohmann::json vertices = nlohmann::json::array();
    for (std::size_t index = 0; index < handshake.vertexPositions.size();
         ++index) {
        vertices.push_back({
            {"position", handshake.vertexPositions[index]},
            {"joints", handshake.vertexJoints[index]},
            {"weights", handshake.vertexWeights[index]}});
    }
    document["vertices"] = std::move(vertices);

    try {
        const std::filesystem::path outputPath(path);
        if (!outputPath.parent_path().empty()) {
            std::filesystem::create_directories(outputPath.parent_path());
        }
        std::ofstream output(outputPath, std::ios::trunc);
        if (!output) {
            return fail(
                outError,
                "could not open animation fixture handshake path: " + path);
        }
        output << document.dump(2) << '\n';
        if (!output) {
            return fail(
                outError,
                "could not write animation fixture handshake path: " + path);
        }
    } catch (const std::exception& exception) {
        return fail(
            outError,
            std::string("animation fixture handshake write failed: ") +
                exception.what());
    }
    if (outError) {
        outError->clear();
    }
    return true;
}

const char* fixtureBackendName(const IRenderBackend& renderer) noexcept {
    const char* backend = renderer.backendId();
    return backend && backend[0] != '\0' ? backend : "unknown";
}

} // namespace engine::animation_fixture
