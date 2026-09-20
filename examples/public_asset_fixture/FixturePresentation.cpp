#include "FixturePresentation.h"

#include <nlohmann/json.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace engine::fixture {
namespace {

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

float motionProgress(const FixtureObject& object, int logicalFrame) {
    const int fromFrame = object.motion.fromFrame;
    const int toFrame = object.motion.toFrame;
    if (!object.motion.enabled || toFrame <= fromFrame) {
        return 0.0f;
    }
    const float frame = static_cast<float>(logicalFrame);
    return std::clamp(
        (frame - static_cast<float>(fromFrame)) /
            static_cast<float>(toFrame - fromFrame),
        0.0f,
        1.0f);
}

engine::render::backend::WorldMeshVertex toWorldVertex(
    const FixtureVertex& vertex) {
    engine::render::backend::WorldMeshVertex out;
    out.x = vertex.x;
    out.y = vertex.y;
    out.z = vertex.z;
    out.u = vertex.u;
    out.v = vertex.v;
    out.r = vertex.r;
    out.g = vertex.g;
    out.b = vertex.b;
    out.a = vertex.a;
    out.nx = vertex.nx;
    out.ny = vertex.ny;
    out.nz = vertex.nz;
    return out;
}

std::array<float, 2> projectNormalized(
    const glm::mat4& viewProjection,
    const glm::vec3& position) {
    const glm::vec4 clip = viewProjection * glm::vec4(position, 1.0f);
    const float w = std::abs(clip.w) > 1.0e-6f ? clip.w : 1.0f;
    const float ndcX = clip.x / w;
    const float ndcY = clip.y / w;
    return {
        std::clamp((ndcX + 1.0f) * 0.5f, 0.0f, 1.0f),
        std::clamp((1.0f - ndcY) * 0.5f, 0.0f, 1.0f)};
}

} // namespace

std::array<float, 16> fixtureViewProjection(
    const FixtureCamera& camera,
    int surfaceWidth,
    int surfaceHeight) {
    const int width = std::max(1, surfaceWidth);
    const int height = std::max(1, surfaceHeight);
    const float aspect =
        static_cast<float>(width) / static_cast<float>(height);
    const glm::mat4 projection = glm::perspective(
        glm::radians(camera.fovDegrees),
        aspect,
        camera.nearPlane,
        camera.farPlane);
    const glm::mat4 view = glm::lookAt(
        glm::vec3(camera.position[0], camera.position[1], camera.position[2]),
        glm::vec3(camera.target[0], camera.target[1], camera.target[2]),
        glm::vec3(0.0f, 1.0f, 0.0f));
    return toArray(projection * view);
}

std::array<float, 16> fixtureModelMatrix(
    const FixtureObject& object,
    int logicalFrame) {
    const float progress = motionProgress(object, logicalFrame);
    const glm::vec3 translation{
        object.motion.from[0] +
            (object.motion.to[0] - object.motion.from[0]) * progress,
        object.motion.from[1] +
            (object.motion.to[1] - object.motion.from[1]) * progress,
        object.motion.from[2] +
            (object.motion.to[2] - object.motion.from[2]) * progress};
    const glm::mat4 model =
        glm::translate(glm::mat4(1.0f), object.motion.enabled
                ? translation
                : glm::vec3(0.0f));
    return toArray(model);
}

std::map<std::string, FixtureRoleRect> computeFixtureRoleRects(
    const FixtureContent& content,
    int logicalFrame,
    int surfaceWidth,
    int surfaceHeight) {
    const glm::mat4 viewProjection = glm::make_mat4(
        fixtureViewProjection(content.camera, surfaceWidth, surfaceHeight).data());
    std::map<std::string, FixtureRoleRect> rects;
    for (const FixtureObject& object : content.objects) {
        if (object.vertices.empty()) {
            continue;
        }
        const glm::mat4 model = glm::make_mat4(
            fixtureModelMatrix(object, logicalFrame).data());
        float minimumX = 1.0f;
        float minimumY = 1.0f;
        float maximumX = 0.0f;
        float maximumY = 0.0f;
        for (const FixtureVertex& vertex : object.vertices) {
            const glm::vec4 world =
                model * glm::vec4(vertex.x, vertex.y, vertex.z, 1.0f);
            const std::array<float, 2> projected = projectNormalized(
                viewProjection,
                glm::vec3(world));
            minimumX = std::min(minimumX, projected[0]);
            minimumY = std::min(minimumY, projected[1]);
            maximumX = std::max(maximumX, projected[0]);
            maximumY = std::max(maximumY, projected[1]);
        }
        rects[object.role] = FixtureRoleRect{
            minimumX,
            minimumY,
            std::max(0.0f, maximumX - minimumX),
            std::max(0.0f, maximumY - minimumY)};
    }
    return rects;
}

bool renderFixture(
    const FixtureContent& content,
    IRenderBackend& renderer,
    int logicalFrame,
    int surfaceWidth,
    int surfaceHeight,
    FixtureDrawStats* outStats,
    std::string* outError) {
    if (surfaceWidth <= 0 || surfaceHeight <= 0) {
        return fail(outError, "fixture render requires a positive surface size");
    }
    if (content.findRole("background") == nullptr ||
        content.findRole("board") == nullptr ||
        content.findRole("occluder") == nullptr ||
        content.findRole("hidden") == nullptr ||
        content.findRole("blend") == nullptr ||
        content.findRole("marker") == nullptr) {
        return fail(outError, "fixture content is missing a required object role");
    }

    const int width = std::max(1, surfaceWidth);
    const int height = std::max(1, surfaceHeight);
    const std::array<float, 16> viewProjection =
        fixtureViewProjection(content.camera, width, height);

    std::vector<const FixtureObject*> ordered;
    ordered.reserve(content.objects.size());
    for (const FixtureObject& object : content.objects) {
        if (object.material.alphaMode != 2u) {
            ordered.push_back(&object);
        }
    }
    for (const FixtureObject& object : content.objects) {
        if (object.material.alphaMode == 2u) {
            ordered.push_back(&object);
        }
    }

    FixtureDrawStats stats{};
    for (const FixtureObject* object : ordered) {
        if (!object || object->vertices.empty() || object->indices.size() < 3u) {
            return fail(outError, "fixture object has no drawable geometry");
        }
        if (object->textureRgba.size() !=
            static_cast<std::size_t>(object->textureWidth) *
                static_cast<std::size_t>(object->textureHeight) * 4u) {
            return fail(
                outError,
                "fixture object '" + object->id +
                    "' has a texture size that does not match its pixels");
        }
        std::vector<engine::render::backend::WorldMeshVertex> vertices;
        vertices.reserve(object->vertices.size());
        for (const FixtureVertex& vertex : object->vertices) {
            vertices.push_back(toWorldVertex(vertex));
        }
        engine::render::backend::WorldTextureData texture;
        texture.key = object->textureKey.c_str();
        texture.cacheKey = object->textureCacheKey.c_str();
        texture.rgba = object->textureRgba.data();
        texture.width = object->textureWidth;
        texture.height = object->textureHeight;
        texture.textureSrgb = 1u;
        texture.alphaMode = object->material.alphaMode;
        texture.blendMode = object->material.blendMode;
        texture.depthTestEnabled = object->material.depthTest;
        texture.materialMode = object->material.materialMode;
        texture.alphaCutoff = object->material.alphaCutoff;
        texture.modelMatrix = fixtureModelMatrix(*object, logicalFrame);
        texture.materialTimeSec =
            static_cast<float>(logicalFrame) / 60.0f;
        texture.cameraPosX = content.camera.position[0];
        texture.cameraPosY = content.camera.position[1];
        texture.cameraPosZ = content.camera.position[2];
        texture.cameraTargetX = content.camera.target[0];
        texture.cameraTargetY = content.camera.target[1];
        texture.cameraTargetZ = content.camera.target[2];
        const glm::vec3 forward = glm::normalize(
            glm::vec3(
                content.camera.target[0] - content.camera.position[0],
                content.camera.target[1] - content.camera.position[1],
                content.camera.target[2] - content.camera.position[2]));
        texture.cameraForwardX = forward.x;
        texture.cameraForwardY = forward.y;
        texture.cameraForwardZ = forward.z;

        renderer.drawWorldIndexedMeshTexturedCached(
            object->geometryCacheKey.c_str(),
            vertices.data(),
            vertices.size(),
            object->indices.data(),
            object->indices.size(),
            &texture,
            viewProjection.data(),
            width,
            height);
        ++stats.drawCalls;
        if (object->material.alphaMode == 2u) {
            ++stats.blendDraws;
        }
        stats.triangles += static_cast<int>(object->triangleCount());
    }

    if (outStats) {
        *outStats = stats;
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

FixtureHandshake buildFixtureHandshake(
    std::string host,
    std::string requestedBackend,
    const IRenderBackend& renderer,
    const FixtureSession& session,
    int logicalFrame,
    int surfaceWidth,
    int surfaceHeight) {
    const FixtureContent& content = session.activeContent();
    FixtureHandshake handshake;
    handshake.host = std::move(host);
    handshake.caseName = std::string(fixtureCaseName(session.fixtureCase()));
    handshake.requestedBackend = std::move(requestedBackend);
    handshake.actualBackend = fixtureBackendName(renderer);
    handshake.gpu = renderer.activeGpuName();
    handshake.gpuDiscrete = renderer.activeGpuIsDiscrete();
    handshake.logicalFrame = logicalFrame;
    handshake.surfaceWidth = surfaceWidth;
    handshake.surfaceHeight = surfaceHeight;
    handshake.activeRoot = content.rootAssetId;
    handshake.activeContentId = content.contentId;
    handshake.activeContentHash = content.contentHash;
    handshake.archiveFileCount = content.archiveFileCount;
    handshake.activationCount = session.activationCount();
    handshake.triangleCount = content.triangleCount();
    handshake.preparationComplete = session.preparationComplete();
    handshake.transition = session.transition();
    handshake.roleRects = computeFixtureRoleRects(
        content,
        logicalFrame,
        surfaceWidth,
        surfaceHeight);
    return handshake;
}

bool writeFixtureHandshake(
    const std::string& path,
    const FixtureHandshake& handshake,
    std::string* outError) {
    if (path.empty()) {
        return fail(outError, "fixture handshake path must not be empty");
    }
    nlohmann::json document;
    document["schema"] = "phlosion-public-asset-fixture-handshake-v1";
    document["host"] = handshake.host;
    document["case"] = handshake.caseName;
    document["requested_backend"] = handshake.requestedBackend;
    document["actual_backend"] = handshake.actualBackend;
    document["gpu"] = handshake.gpu;
    document["gpu_discrete"] = handshake.gpuDiscrete;
    document["logical_frame"] = handshake.logicalFrame;
    document["surface"] = {
        {"width", handshake.surfaceWidth},
        {"height", handshake.surfaceHeight}};
    document["active"] = {
        {"root", handshake.activeRoot},
        {"content_id", handshake.activeContentId},
        {"content_hash", handshake.activeContentHash},
        {"archive_file_count", handshake.archiveFileCount},
        {"activation_count", handshake.activationCount},
        {"triangles", handshake.triangleCount}};
    document["preparation_complete"] = handshake.preparationComplete;
    document["transition"] = {
        {"attempted", handshake.transition.attempted},
        {"succeeded", handshake.transition.succeeded},
        {"failed", handshake.transition.failed},
        {"requested_root", handshake.transition.requestedRoot},
        {"outcome", handshake.transition.outcome},
        {"error", handshake.transition.error}};
    nlohmann::json objects = nlohmann::json::object();
    for (const auto& [role, rect] : handshake.roleRects) {
        objects[role] = {
            {"rect", {rect[0], rect[1], rect[2], rect[3]}}};
    }
    document["objects"] = std::move(objects);

    try {
        const std::filesystem::path outputPath(path);
        if (!outputPath.parent_path().empty()) {
            std::filesystem::create_directories(outputPath.parent_path());
        }
        std::ofstream output(outputPath, std::ios::trunc);
        if (!output) {
            return fail(
                outError,
                "could not open fixture handshake path: " + path);
        }
        output << document.dump(2) << '\n';
        if (!output) {
            return fail(
                outError,
                "could not write fixture handshake path: " + path);
        }
    } catch (const std::exception& exception) {
        return fail(
            outError,
            std::string("fixture handshake write failed: ") + exception.what());
    }
    if (outError) {
        outError->clear();
    }
    return true;
}

} // namespace engine::fixture
