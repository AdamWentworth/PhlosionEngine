#pragma once

#include <string>
#include <memory>
#include <unordered_map>
#include <vector>

#include "engine/render/OpenGLRenderBackend.h"
#include "engine/vfx/effects/shared/SharedAuthoredBatchVFX.h"
#include "engine/vfx/runtime/shared/SharedAuthoredVfxBatches.h"
#include "engine/vfx/runtime/shared/SharedAuthoredVfxHelpers.h"

class Camera3D;

namespace vfx::preview::authored {

namespace detail {

bool loadMeshForPreview(const std::string& modelPath,
                        vfx::runtime::authored_batches::MeshData& out,
                        std::string* outError = nullptr);

} // namespace detail

class SharedAuthoredVfxRenderer {
public:
    ~SharedAuthoredVfxRenderer();

    void setRenderBackend(IRenderBackend* backend);
    void onResize(int width, int height);

    void render(const SharedAuthoredBatchVFX& effect,
                const Camera3D& camera,
                int surfaceWidth,
                int surfaceHeight);

    struct BackendMeshCacheEntry {
        bool attemptedLoad = false;
        bool reportedFailure = false;
        vfx::runtime::authored_batches::MeshData mesh;
        std::string error;
    };

    struct TextureCacheEntry {
        bool attemptedLoad = false;
        bool valid = false;
        int width = 0;
        int height = 0;
        std::vector<unsigned char> rgba;
    };

private:
    vfx::runtime::authored_batches::MeshData* ensureBackendMeshLoaded(const std::string& modelPath);
    TextureCacheEntry* ensureBackendTextureLoaded(const std::string& texturePath,
                                                  bool flipVertical = false);
    static bool fillTextureViewFromEntry(const TextureCacheEntry* texture,
                                         vfx::runtime::authored_batches::TextureView& outView);
    bool fillTextureView(const SharedAuthoredBatchVFX::Config::DrawPass& pass,
                         const SharedAuthoredBatchVFX::Config& config,
                         const vfx::runtime::authored::TevState& tev,
                         vfx::runtime::authored_batches::TextureView& outView);

    IRenderBackend* activeBackend();

    IRenderBackend* externalBackend_ = nullptr;
    std::unique_ptr<OpenGLRenderBackend> fallbackBackend_;
    int surfaceWidth_ = 1;
    int surfaceHeight_ = 1;
    std::unordered_map<std::string, BackendMeshCacheEntry> backendMeshByModelPath_;
    std::unordered_map<std::string, TextureCacheEntry> backendTextureByPath_;
};

} // namespace vfx::preview::authored
