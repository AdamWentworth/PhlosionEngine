#pragma once

#include "engine/render/IRenderBackendDebug.h"
#include "engine/render/IRenderBackendFrame.h"
#include "engine/render/IRenderBackendWorld.h"
#include "engine/render/WorldMaterialProfile.h"

#include <algorithm>
#include <stdexcept>

class IRenderBackend : public IRenderBackendFrame,
                       public IRenderBackendWorld,
                       public IRenderBackendDebug {
public:
  explicit IRenderBackend(const engine::render::WorldMaterialProfile &profile = {})
      : worldMaterialProfile_(profile) { worldMaterialProfile_.validate(); }

  // Call between frames. Native backends rebuild their world pipelines and
  // retain a copy, so project-plugin unloads cannot invalidate shader data.
  virtual void setWorldMaterialProfile(const engine::render::WorldMaterialProfile &profile) {
      if (!profile.empty()) throw std::runtime_error("This renderer does not support project material profiles.");
  }
  const engine::render::WorldMaterialProfile &worldMaterialProfile() const noexcept {
      return worldMaterialProfile_;
  }
    using BackendFrameTimings = IRenderBackendFrame::BackendFrameTimings;
    using BackendFrameStats = IRenderBackendFrame::BackendFrameStats;

    using WorldIndexedSubmissionStats = IRenderBackendWorld::WorldIndexedSubmissionStats;
    using WorldMeshVertex = IRenderBackendWorld::WorldMeshVertex;
    using WorldTextureMipLevel = IRenderBackendWorld::WorldTextureMipLevel;
    using WorldTextureData = IRenderBackendWorld::WorldTextureData;
    using WorldMeshInstance = IRenderBackendWorld::WorldMeshInstance;
    template <typename Tag>
    using WorldSceneHandle = IRenderBackendWorld::WorldSceneHandle<Tag>;
    using WorldSceneGeometryHandle = IRenderBackendWorld::WorldSceneGeometryHandle;
    using WorldSceneMaterialHandle = IRenderBackendWorld::WorldSceneMaterialHandle;
    using WorldSceneSkeletonLayoutHandle =
        IRenderBackendWorld::WorldSceneSkeletonLayoutHandle;
    using WorldSceneAnimationClipHandle =
        IRenderBackendWorld::WorldSceneAnimationClipHandle;
    using WorldSceneRenderObjectHandle =
        IRenderBackendWorld::WorldSceneRenderObjectHandle;
    using WorldSceneRenderInstanceHandle =
        IRenderBackendWorld::WorldSceneRenderInstanceHandle;
    using WorldSceneFastPathCaps = IRenderBackendWorld::WorldSceneFastPathCaps;
    using WorldSceneSourceMaterialFamily =
        IRenderBackendWorld::WorldSceneSourceMaterialFamily;
    using WorldSceneSourceVertex = IRenderBackendWorld::WorldSceneSourceVertex;
    using WorldSceneSourceTextureBinding =
        IRenderBackendWorld::WorldSceneSourceTextureBinding;
    using WorldSceneGeometry = IRenderBackendWorld::WorldSceneGeometry;
    using WorldSceneMaterial = IRenderBackendWorld::WorldSceneMaterial;
    using WorldSceneSkeletonLayout = IRenderBackendWorld::WorldSceneSkeletonLayout;
    using WorldSceneAnimationClip = IRenderBackendWorld::WorldSceneAnimationClip;
    using WorldSceneRenderObject = IRenderBackendWorld::WorldSceneRenderObject;
    using WorldSceneInstance = IRenderBackendWorld::WorldSceneInstance;
    using WorldSceneDrawClass = IRenderBackendWorld::WorldSceneDrawClass;
    using WorldSceneFrame = IRenderBackendWorld::WorldSceneFrame;
    using WorldSceneView = IRenderBackendWorld::WorldSceneView;
    using WorldTriangle = IRenderBackendWorld::WorldTriangle;
    static constexpr std::uint32_t WorldSceneSourceVertexSemanticNone =
        IRenderBackendWorld::WorldSceneSourceVertexSemanticNone;

    static bool worldSceneGeometrySourceSemanticsValid(
        const WorldSceneGeometry& geometry) noexcept {
        return IRenderBackendWorld::worldSceneGeometrySourceSemanticsValid(
            geometry);
    }

    using DebugQuad = IRenderBackendDebug::DebugQuad;
    using DebugLine = IRenderBackendDebug::DebugLine;
    using DebugTriangle = IRenderBackendDebug::DebugTriangle;
    using DebugSprite = IRenderBackendDebug::DebugSprite;

    // Transient, renderer-wide material inspection used by editor previews.
    // Zero renders the composed material; one through six select the raw
    // albedo, normal, roughness, metallic, AO, and emission/auxiliary inputs.
    // Backends must not persist this into project assets or ordinary game
    // state.
    virtual void setWorldMaterialDebugView(int view) noexcept {
        worldMaterialDebugView_ = std::clamp(view, 0, 7);
    }
    int worldMaterialDebugView() const noexcept {
        return worldMaterialDebugView_;
    }

    virtual ~IRenderBackend() = default;

  protected:
    engine::render::WorldMaterialProfile worldMaterialProfile_;

  private:
    int worldMaterialDebugView_ = 0;
};
