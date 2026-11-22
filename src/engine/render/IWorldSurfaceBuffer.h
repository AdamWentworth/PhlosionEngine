#pragma once
#include "engine/render/IWorldContactVisibility.h"
#include "engine/render/WorldMotionGpu.h"
#include "engine/render/WorldTemporalScene.h"
#include "engine/render/WorldTemporalReflection.h"
#include "engine/render/WorldClusteredLighting.h"
#include "engine/render/WorldProbeSurfaceCache.h"

namespace engine::render {
// Compact opaque/masked evaluated records: RGB10A2 sqrt-albedo, RGB10A2 normal/class,
// RGBA8 metal/F0/AO/roughness, R11G11B10F emission. Class 3 is motion-only.
// Class 0 is empty; classes 1/2 have opaque/masked evaluated attributes. Albedo
// alpha 0 permits standard PBR; alpha 1 marks specialized forward lighting; alpha 2
// enrolls standard primary composition. Source stencil
// identities and specialized layered-material records are separate work.
// Motion adds RGBA16F velocity/depth/validity and R32_UINT identity planes.
// Captures are local to the current scene-color view and frame. Transparency,
// sky and materials without a surface export remain on the forward path.
class IWorldSurfaceBuffer {
public:
    virtual ~IWorldSurfaceBuffer() = default;
    virtual bool captureWorldClusteredLighting() {return false;}
    virtual WorldClusteredLightingDiagnostic readWorldClusteredLighting() {return {};}
    virtual bool resolveWorldClusteredLighting(const WorldClusteredLightingView&) {return false;}
    virtual bool beginWorldTemporalScene(const WorldTemporalView&) {return false;}
    virtual void releaseWorldTemporalView(std::uint64_t) {}
    virtual bool resolveWorldTemporalScene() {return false;}
    virtual bool resolveWorldHeatHaze() {return false;}
    virtual bool resolveWorldTemporalReflection(const WorldTemporalReflectionView&) {return false;}
    virtual WorldTemporalFeedback worldTemporalFeedback() const {return {};}
    virtual bool captureWorldTemporalScene() {return false;}
    virtual WorldTemporalImage readWorldTemporalScene() {return {};}
    // Persistent static probe materials, separate from frame-owned primary surfaces.
    // Caller supplies dry material inputs. End publishes only after all six faces
    // of a revision finish. Clear aborts the current face; the old revision survives.
    virtual bool relightWorldProbeSurface(const WorldClusteredLightingView&,const std::array<std::array<float,4>,32>&) {return false;}
    virtual bool beginWorldProbeSurface(std::uint64_t,std::uint64_t,unsigned,const float*,int) {return false;}
    virtual bool endWorldProbeSurface() {return false;}
    // Selected static probe cores are consumed by relightWorldProbeSurface.
    // Their shared capture scratch is not a primary contact/cluster surface.
    virtual bool selectWorldProbeSurface(std::uint64_t,unsigned) {return false;}
    virtual std::uint64_t worldProbeSurfaceRevision(std::uint64_t) const {return 0;}
    // Release/reassign a slot only outside a recorded frame. Resolution changes
    // require release; material/geometry changes use a new revision.
    virtual bool releaseWorldProbeSurface(std::uint64_t) {return false;}
    virtual bool beginWorldSurfaceBuffer(const float* viewProjection, int width, int height) = 0;
    // Declare the standard lighting profile before capture; incompatible per-draw
    // lighting stays forward. A planar capture requires its exact reflected matrix
    // and extent; closing it invalidates its surfaces. Ordinary/probe captures do
    // not enroll composition.
    virtual bool beginWorldSurfaceCompositionCapture(const float*,int,int,const WorldClusteredLightingView&) {return false;}
    // Valid only during a surface capture. Empty samples clear draw enrollment.
    virtual bool setWorldSurfaceMotion(const WorldMotionMatrix&,std::span<const WorldMotionSample>) {return false;}
    // Qualification readback: queue in the active scene, collect after endFrame.
    virtual bool captureWorldSurfaceMotion() {return false;}
    virtual WorldMotionImage readWorldSurfaceMotion() {return {};}
    virtual bool endWorldSurfaceBuffer() = 0;
    virtual void clearWorldSurfaceBuffer() = 0;
    virtual bool resolveWorldSurfaceContact(const WorldContactUniforms&) = 0;
    // Reuse current-view opaque/masked depth before final forward composition.
    // Call after scratch passes; preserves scene color and sampled surface data.
    virtual bool restoreWorldSurfaceDepth(const WorldContactUniforms&) {return false;}
    // Compose captured standard PBR once, then suppress its identical geometry replay.
    // Current primary view/lighting must match capture; end before new/late geometry.
    virtual bool beginWorldSurfaceComposition(const WorldContactUniforms&,const WorldClusteredLightingView&) {return false;}
    virtual void endWorldSurfaceComposition() {}
    // Borrowed only while the current composition suppresses standard geometry.
    // Null during capture, after end/reset and in material debug views.
    virtual const WorldClusteredLightingView* worldSurfaceCompositionLighting() const {return nullptr;}
    // Backend scheduling preference; callers may still explicitly restore depth.
    virtual bool prefersWorldSurfaceDepthReuse() const {return false;}
    // Requires the same view and a completed current-frame reflection source.
    virtual bool resolveWorldSurfaceScreenReflection() = 0;
    // Diagnostic copy into scene radiance; normal scene presentation still applies.
    virtual bool showWorldSurfaceBuffer(unsigned plane) = 0;
};


// Scoped enrollment prevents a later unsupported draw from inheriting an ID.
class WorldSurfaceMotionDraw {
    IWorldSurfaceBuffer* surface_=nullptr;
public:
    WorldSurfaceMotionDraw(IRenderBackend& renderer,WorldMotionHistory* history,
                           std::span<const WorldMotionSample> samples) {
        if(history) {
            auto* surface=dynamic_cast<IWorldSurfaceBuffer*>(&renderer);
            if(surface && surface->setWorldSurfaceMotion(history->previousViewProjection(),samples))
                surface_=surface;
        }
    }
    ~WorldSurfaceMotionDraw() {
        if(surface_)surface_->setWorldSurfaceMotion(worldMotionIdentity,{});
    }
    WorldSurfaceMotionDraw(const WorldSurfaceMotionDraw&)=delete;
    WorldSurfaceMotionDraw& operator=(const WorldSurfaceMotionDraw&)=delete;
};

template<class Draw>
void drawWorldSurfaceContact(IRenderBackend& renderer,const float* matrix,int width,int height,Draw&& draw) {
    auto* surface=dynamic_cast<IWorldSurfaceBuffer*>(&renderer);
    if(!surface || !surface->beginWorldSurfaceBuffer(matrix,width,height))
        throw std::runtime_error("Shared surface capture unavailable");
    try {
        draw();
        if(!surface->endWorldSurfaceBuffer() ||
           !surface->resolveWorldSurfaceContact(packWorldContactVisibility(matrix,width,height,.8f,.015f)))
            throw std::runtime_error("Shared surface contact resolve failed");
    } catch(...) {
        surface->clearWorldSurfaceBuffer();
        if(auto* contact=dynamic_cast<IWorldContactVisibility*>(&renderer))contact->clearWorldContactVisibility();
        throw;
    }
}

inline bool surfaceContactViewMatches(const WorldContactUniforms& surface, const WorldContactUniforms& contact) {
    if (surface[8][0] != contact[8][0] || surface[8][1] != contact[8][1] ||
        contact[8][3] != -1 || contact[9][3] <= .5f) return false;
    for (int row=0; row<8; ++row) for (int col=0; col<4; ++col)
        if (!std::isfinite(contact[row][col]) || surface[row][col] != contact[row][col]) return false;
    return true;
}
}
