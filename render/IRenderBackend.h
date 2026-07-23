#pragma once

#include "engine/render/IRenderBackendDebug.h"
#include "engine/render/IRenderBackendFrame.h"
#include "engine/render/IRenderBackendWorld.h"

class IRenderBackend : public IRenderBackendFrame,
                       public IRenderBackendWorld,
                       public IRenderBackendDebug {
public:
    using BackendFrameTimings = IRenderBackendFrame::BackendFrameTimings;
    using BackendFrameStats = IRenderBackendFrame::BackendFrameStats;

    using WorldIndexedSubmissionStats = IRenderBackendWorld::WorldIndexedSubmissionStats;
    using WorldMeshVertex = IRenderBackendWorld::WorldMeshVertex;
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

    using DebugQuad = IRenderBackendDebug::DebugQuad;
    using DebugLine = IRenderBackendDebug::DebugLine;
    using DebugTriangle = IRenderBackendDebug::DebugTriangle;
    using DebugSprite = IRenderBackendDebug::DebugSprite;

    virtual ~IRenderBackend() = default;
};
