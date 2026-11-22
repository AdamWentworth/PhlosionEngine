#pragma once
#include "engine/render/IWorldPlanarReflection.h"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <limits>

namespace engine::render {
// Upper-left pixel bounds, exclusive right/bottom. This limits work, not the
// capture resolution or projection. Singular/infinite views retain full coverage.
inline std::array<int,4> worldPlanarCaptureScissor(const WorldPlanarReflectionView& view) {
    const std::array<int,4> full{0,0,view.width,view.heightPixels};
    if(view.width<=0 || view.heightPixels<=0 || !std::isfinite(view.height) ||
       !std::isfinite(view.receiverTolerance) || view.receiverTolerance<=0) return full;
    glm::dmat4 reflected(glm::make_mat4(view.viewProjection.data()));
    glm::dmat4 mirror(1);mirror[1][1]=-1;mirror[3][1]=2.0*view.height;
    const auto matrix=reflected*mirror;
    const auto inverse=glm::inverse(matrix);
    std::array<glm::dvec3,8> corners{};
    for(unsigned i=0;i<8;++i) {
        auto p=inverse*glm::dvec4(i&1?1:-1,i&2?1:-1,i&4?1:-1,1);
        for(unsigned k=0;k<4;++k) if(!std::isfinite(p[k]))return full;
        if(p.w<=1e-12)return full;
        corners[i]=glm::dvec3(p)/p.w;
    }
    // Clip the primary frustum to the entire height slab accepted by the
    // planar material, then project its vertices onto the actual receiver plane.
    // A positive-w projective map reaches its extrema at these convex vertices.
    const double low=double(view.height)-view.receiverTolerance;
    const double high=double(view.height)+view.receiverTolerance;
    std::array<glm::dvec3,32> vertices{};
    unsigned count=0;
    for(const auto& p:corners)if(p.y>=low && p.y<=high)vertices[count++]=p;
    for(unsigned i=0;i<8;++i)for(unsigned axis=0;axis<3;++axis)if(!(i&(1u<<axis))) {
        const auto a=corners[i],b=corners[i|(1u<<axis)];
        const double delta=b.y-a.y;
        if(std::abs(delta)<1e-12)continue;
        for(double y:{low,high}) {
            const double t=(y-a.y)/delta;
            if(t>=0 && t<=1)vertices[count++]=a+(b-a)*t;
        }
    }
    // No possible receiver: one unused pixel keeps zero-area API edge cases out.
    if(!count)return {0,0,1,1};
    glm::dvec2 lower(std::numeric_limits<double>::max()),upper(-std::numeric_limits<double>::max());
    for(unsigned i=0;i<count;++i) {
        auto p=vertices[i];p.y=view.height;
        const auto clip=matrix*glm::dvec4(p,1);
        if(!std::isfinite(clip.w) || clip.w<=1e-8)return full;
        const glm::dvec2 uv(.5+.5*clip.x/clip.w,.5-.5*clip.y/clip.w);
        if(!std::isfinite(uv.x)||!std::isfinite(uv.y))return full;
        lower=glm::min(lower,uv);upper=glm::max(upper,uv);
    }
    // world_planar.glsl: unit-normal perturbation <= .025, roughness < .55,
    // nine-tap Gaussian radius .02*r*r*1.182426, edge clamp and bilinear guard.
    // Horizontal blur is aspect corrected, so both blur radii scale with height.
    const double blur=.02*.55*.55*1.182426*view.heightPixels;
    const glm::dvec2 extent(view.width,view.heightPixels);
    const glm::dvec2 edgeClamp=.5*extent/double(std::min(view.width,view.heightPixels));
    // The quarter-size local-haze producer samples scene depth. Its four-tap
    // upsample needs neighbouring coarse texels: retain eight scene pixels
    // around the reflection filter's support, including odd-size rounding.
    const glm::dvec2 pad=.025*extent+edgeClamp+glm::dvec2(blur+2+8);
    lower=glm::clamp(glm::floor(lower*extent-pad),glm::dvec2(0),extent);
    upper=glm::clamp(glm::ceil(upper*extent+pad),glm::dvec2(0),extent);
    if(upper.x<=lower.x || upper.y<=lower.y)return {0,0,1,1};
    return {int(lower.x),int(lower.y),int(upper.x),int(upper.y)};
}

// Scissors are target pixels, not normalized coordinates. The haze scratch
// target is smaller than the planar capture; round outwards and retain its
// neighbouring low-resolution taps. Main-view bounds remain unchanged.
inline std::array<int,4> worldPlanarHazeScissor(const std::array<int,4>& bounds,
    const WorldPlanarReflectionView& view,int width,int height) {
    return {std::max(0,bounds[0]*width/view.width-1),
            std::max(0,bounds[1]*height/view.heightPixels-1),
            std::min(width,(bounds[2]*width+view.width-1)/view.width+1),
            std::min(height,(bounds[3]*height+view.heightPixels-1)/view.heightPixels+1)};
}
}
