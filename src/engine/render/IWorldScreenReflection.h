#pragma once

#include "engine/render/IWorldContactVisibility.h"
#include <algorithm>

namespace engine::render {
using WorldScreenReflectionUniforms = std::array<std::array<float,4>,11>;

// Work budget is independent of output resolution. The default retains the
// exhaustive reference for existing engine clients.
struct WorldScreenReflectionSettings {
    int maxSteps=4096;
    float roughnessCutoff=.75f;
};

inline WorldScreenReflectionUniforms packWorldScreenReflection(const float* matrix,
    int width, int height, float maxDistance=40.f, float thickness=.12f, bool reuseOpaqueDepth=false, WorldScreenReflectionSettings settings={}) {
    if (!std::isfinite(maxDistance) || maxDistance<=0 || maxDistance>200 ||
        !std::isfinite(thickness) || thickness<=0 || thickness>1)
        throw std::invalid_argument("Invalid screen reflection trace");
    if(settings.maxSteps<8 || settings.maxSteps>4096 ||
       !std::isfinite(settings.roughnessCutoff) || settings.roughnessCutoff<.3f || settings.roughnessCutoff>.9f)
        throw std::invalid_argument("Invalid screen reflection quality");
    // Reuse the validated world-to-clip/inverse-clip matrix convention.
    const auto matrices=packWorldContactVisibility(matrix,width,height,1.f,.01f);
    WorldScreenReflectionUniforms out{};
    std::copy_n(matrices.begin(),8,out.begin());
    out[8]={float(width),float(height),maxDistance,-1};
    out[9]={thickness,.02f,0,reuseOpaqueDepth?1.f:0.f};
    out[10][2]=float(settings.maxSteps);out[10][3]=settings.roughnessCutoff;
    return out;
}

inline bool worldScreenReflectionCanReuseContactDepth(const WorldScreenReflectionUniforms& screen,
    const WorldContactUniforms& contact) {
    return screen[9][3]>.5f && contact[8][3]>0 && contact[9][3]>.5f &&
        screen[8][0]==contact[8][0] && screen[8][1]==contact[8][1] &&
        std::equal(screen.begin(),screen.begin()+8,contact.begin());
}

// Capture current-view opaque radiance/depth before drawing the final view.
// No history or recursive reflection: the source pass has screen tracing off.
// Callers redraw sky and geometry. By default both color and depth are cleared.
// reuseOpaqueDepth promises identical opaque/masked geometry, poses, clipping,
// camera and viewport in the contact, source and final passes. The backend may
// seed depth from these captures to reject hidden shading; color is still clear.
// Do not opt in when a source intentionally omits or moves an opaque object.
class IWorldScreenReflection {
public:
    virtual ~IWorldScreenReflection()=default;
    virtual bool beginWorldScreenReflection(const WorldScreenReflectionUniforms&)=0;
    virtual bool endWorldScreenReflection()=0;
    virtual void clearWorldScreenReflection()=0;
protected:
    WorldScreenReflectionUniforms worldScreenReflection_{};
};
}
