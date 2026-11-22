#pragma once
#include "GltfSceneAsset.h"
#include <algorithm>
#include <cmath>
#include <span>
#include <limits>

namespace engine::render::gltf::scene {
inline std::array<float, 16> gltfSceneMultiplyMatrices(const std::array<float, 16>& left,
                                          const std::array<float, 16>& right) {
    std::array<float, 16> out{};
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += left[static_cast<std::size_t>(k) * 4u +
                            static_cast<std::size_t>(row)] *
                       right[static_cast<std::size_t>(column) * 4u +
                             static_cast<std::size_t>(k)];
            }
            out[static_cast<std::size_t>(column) * 4u +
                static_cast<std::size_t>(row)] = sum;
        }
    }
    return out;
}

struct GltfSceneBounds {
    std::array<float,3> boundsMin{},boundsMax{};
    bool valid=false;
};
inline bool gltfSceneBoundsOutsideView(const GltfSceneBounds& primitive,
                 const std::array<float,16>& clipFromLocal) {
    if(!primitive.valid)return false;
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(primitive.boundsMin[axis]) ||
            !std::isfinite(primitive.boundsMax[axis]) ||
            primitive.boundsMin[axis] > primitive.boundsMax[axis]) return false;
    }
    // A convex box is outside only if every corner lies beyond the SAME plane.
    // Homogeneous tests also retain boxes crossing the eye/near plane.
    unsigned commonOutside = 63u;
    for (int corner = 0; corner < 8; ++corner) {
        std::array<double, 4> clip{};
        for (int row = 0; row < 4; ++row) {
            clip[row] = clipFromLocal[12 + row];
            for (int axis = 0; axis < 3; ++axis) {
                const float position = (corner & (1 << axis))
                    ? primitive.boundsMax[axis] : primitive.boundsMin[axis];
                clip[row] += static_cast<double>(clipFromLocal[axis * 4 + row]) * position;
            }
            if (!std::isfinite(clip[row])) return false;
        }
        unsigned outside = 0u;
        for (int axis = 0; axis < 3; ++axis) {
            const double slack = 1e-5 * std::max({1.0, std::abs(clip[axis]), std::abs(clip[3])});
            if (clip[3] + clip[axis] < -slack) outside |= 1u << (axis * 2);
            if (clip[3] - clip[axis] < -slack) outside |= 1u << (axis * 2 + 1);
        }
        commonOutside &= outside;
        if (!commonOutside) return false;
    }
    return commonOutside != 0u;
}

inline bool gltfScenePrimitiveOutsideView(const GltfScenePrimitive& p,const std::array<float,16>& clip) {
    if(p.skinIndex>=0)return false;
    return gltfSceneBoundsOutsideView({p.boundsMin,p.boundsMax,p.boundsValid},clip);
}
// Compute once per immutable pose. The union of transformed bind boxes contains
// every normalized linear blend, including the shader's residual bind weight.
inline GltfSceneBounds gltfSceneSkinnedBounds(const GltfScenePrimitive& p,
        const glm::mat4& model,std::span<const glm::mat4> palette) {
    GltfSceneBounds out;
    if(!p.boundsValid || p.skinIndex<0 || palette.empty() || p.vertices.empty())return out;
    for(unsigned axis=0;axis<3;++axis)if(!std::isfinite(p.boundsMin[axis])||!std::isfinite(p.boundsMax[axis])||p.boundsMin[axis]>p.boundsMax[axis])return out;
    bool bind=false;
    for(const auto& v:p.vertices) {
        float sum=0;
        for(float w:{v.weight0,v.weight1,v.weight2,v.weight3}) {
            if(!std::isfinite(w)||w<0)return out;
            sum+=w;
        }
        if(sum>1.00001f)return out;
        bind|=sum<1.f;
        const std::array<float,4> joints{v.joint0,v.joint1,v.joint2,v.joint3},weights{v.weight0,v.weight1,v.weight2,v.weight3};
        for(unsigned i=0;i<4;++i)if(weights[i]>.00001f&&(!std::isfinite(joints[i])||joints[i]<0||joints[i]>=palette.size()))return out;
    }
    out.boundsMin.fill(std::numeric_limits<float>::max());
    out.boundsMax.fill(std::numeric_limits<float>::lowest());
    for(std::size_t joint=0;joint<palette.size()+(bind?1:0);++joint) {
        const auto matrix=joint==palette.size()?model:model*palette[joint];
        for(unsigned corner=0;corner<8;++corner) {
            const auto point=matrix*glm::vec4(
                (corner&1)?p.boundsMax[0]:p.boundsMin[0],
                (corner&2)?p.boundsMax[1]:p.boundsMin[1],
                (corner&4)?p.boundsMax[2]:p.boundsMin[2],1);
            for(unsigned axis=0;axis<3;++axis) {
                if(!std::isfinite(point[axis])||!std::isfinite(point.w)||std::abs(point.w-1)>1e-5)return {};
                out.boundsMin[axis]=std::min(out.boundsMin[axis],point[axis]);
                out.boundsMax[axis]=std::max(out.boundsMax[axis],point[axis]);
            }
        }
    }
    for(unsigned axis=0;axis<3;++axis) {
        const float pad=1e-4f*std::max({1.f,std::abs(out.boundsMin[axis]),std::abs(out.boundsMax[axis])});
        out.boundsMin[axis]-=pad;out.boundsMax[axis]+=pad;
    }
    out.valid=true;return out;
}

}
