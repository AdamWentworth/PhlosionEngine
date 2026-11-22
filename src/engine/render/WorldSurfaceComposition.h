#pragma once
#include "engine/render/IWorldSurfaceBuffer.h"
namespace engine::render {
// Standard PBR and enrolled leaf materials share primary opaque composition.
// Shader exports independently confirm participation for this exact view.
inline bool worldSurfaceCompositionMaterial(const IRenderBackend::WorldTextureData* t,bool foliage=false) {
    return t && (t->materialMode==2 || foliage) && t->environment.enabled && !t->environment.skyDraw &&
        t->directionalShadowReceiver && t->depthTestEnabled && t->alphaMode<2 &&
        !t->blendMode && !t->dualSourceBlendEnabled && !t->characterInkingEnabled &&
        !t->projectedShadowEnabled && t->clipSpaceDepthBias==0 &&
        std::abs(t->cameraForwardX*t->cameraForwardX+t->cameraForwardY*t->cameraForwardY+
            t->cameraForwardZ*t->cameraForwardZ-1.f)<.005f;
}
inline bool worldSurfaceCompositionEnvironmentMatches(const WorldEnvironmentLighting& a,
                                                     const WorldEnvironmentLighting& b,bool reflection=true) {
    const auto x=packWorldEnvironment(a),y=packWorldEnvironment(b);
    return std::equal(x.begin(),x.begin()+12,y.begin()) && x[18][2]==y[18][2] && x[18][3]==y[18][3] &&
        (!reflection || packWorldReflectionResponse(a)==packWorldReflectionResponse(b)) &&
        packWorldPointLights(a)==packWorldPointLights(b) && packWorldRectLights(a)==packWorldRectLights(b);
}
inline bool worldSurfaceCompositionLightingMatches(const WorldClusteredLightingView& a,
                                                   const WorldClusteredLightingView& b) {
    return a.camera==b.camera && a.forward==b.forward &&
        worldSurfaceCompositionEnvironmentMatches(a.environment,b.environment);
}
inline bool worldSurfaceCompositionMaterial(const IRenderBackend::WorldTextureData* t,
                                           const WorldClusteredLightingView& lighting,bool foliage=false) {
    return worldSurfaceCompositionMaterial(t,foliage) &&
        std::array{t->cameraPosX,t->cameraPosY,t->cameraPosZ}==lighting.camera &&
        std::array{t->cameraForwardX,t->cameraForwardY,t->cameraForwardZ}==lighting.forward &&
        worldSurfaceCompositionEnvironmentMatches(t->environment,lighting.environment,
            !foliage || packWorldReflectionResponse(t->environment)[1][0]>0.f);
}
// Scope covers the identical primary geometry submission used for surface capture.
// Transparent/specialized draws still execute; later effects are outside the scope.
class WorldSurfaceComposition {
    IWorldSurfaceBuffer* surface_=nullptr;
public:
    WorldSurfaceComposition(IRenderBackend& renderer, bool enabled,
        const WorldContactUniforms& view, const WorldClusteredLightingView& lighting) {
        if(!enabled)return;
        surface_=dynamic_cast<IWorldSurfaceBuffer*>(&renderer);
        if(!surface_ || !surface_->beginWorldSurfaceComposition(view,lighting))
            throw std::runtime_error("Primary surface composition unavailable");
    }
    ~WorldSurfaceComposition(){if(surface_)surface_->endWorldSurfaceComposition();}
    WorldSurfaceComposition(const WorldSurfaceComposition&)=delete;
    WorldSurfaceComposition& operator=(const WorldSurfaceComposition&)=delete;
};
}
