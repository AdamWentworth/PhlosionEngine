#pragma once

#include <array>
#include <cmath>
#include <stdexcept>

namespace engine::render {

using WorldPlanarReflectionUniforms = std::array<std::array<float, 4>, 6>;

// One horizontal receiver plane per view. Matrices use the world API's
// column-major, OpenGL clip convention on every backend.
struct WorldPlanarReflectionView {
    std::array<float, 16> viewProjection{};
    std::array<float, 3> cameraPosition{};
    float height = 0;
    float receiverTolerance = .12f;
    int width = 1;
    int heightPixels = 1;
};

inline WorldPlanarReflectionView makeWorldPlanarReflectionView(
    const float* matrix, std::array<float, 3> camera, float height, int width, int pixels) {
    if (!matrix || !std::isfinite(height) || width <= 0 || pixels <= 0 || width > 16384 || pixels > 16384)
        throw std::invalid_argument("Invalid planar reflection view");
    WorldPlanarReflectionView result;
    for (int i = 0; i < 16; ++i) {
        if (!std::isfinite(matrix[i])) throw std::invalid_argument("Nonfinite reflection matrix");
        result.viewProjection[i] = matrix[i];
    }
    for (const auto value : camera)
        if (!std::isfinite(value)) throw std::invalid_argument("Nonfinite reflection camera");
    // VP * reflectY(height). The reflected camera retains the original
    // projection; fragment clipping removes geometry below the receiver.
    for (int row = 0; row < 4; ++row) {
        result.viewProjection[4 + row] = -matrix[4 + row];
        result.viewProjection[12 + row] += 2 * height * matrix[4 + row];
    }
    result.cameraPosition = {camera[0], 2 * height - camera[1], camera[2]};
    result.height = height;
    result.width = (width + 1) / 2;
    result.heightPixels = (pixels + 1) / 2;
    return result;
}

inline WorldPlanarReflectionUniforms packWorldPlanarReflection(const WorldPlanarReflectionView& view) {
    if (view.width <= 0 || view.heightPixels <= 0 || view.width > 8192 || view.heightPixels > 8192 ||
        !std::isfinite(view.height) || !std::isfinite(view.receiverTolerance) || view.receiverTolerance <= 0)
        throw std::invalid_argument("Invalid planar reflection receiver");
    for (float value : view.viewProjection)
        if (!std::isfinite(value)) throw std::invalid_argument("Nonfinite reflection matrix");
    for (float value : view.cameraPosition)
        if (!std::isfinite(value)) throw std::invalid_argument("Nonfinite reflection camera");
    WorldPlanarReflectionUniforms result{};
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col)
            result[row][col] = view.viewProjection[col * 4 + row];
    result[4] = {view.height, view.receiverTolerance, .012f, -1};
    result[5] = {1, 1, 0, .5f / static_cast<float>(view.width < view.heightPixels ? view.width : view.heightPixels)};
    return result;
}

class IWorldPlanarReflection {
public:
    virtual ~IWorldPlanarReflection() = default;
    // Bracket a reflected sky/opaque scene at view.width x view.heightPixels,
    // after shadows and before the main view. end copies HDR radiance and
    // clears scene color/depth for the main view. Never nest this pass.
    virtual bool beginWorldPlanarReflection(const WorldPlanarReflectionView&) = 0;
    virtual bool endWorldPlanarReflection() = 0;
protected:
    WorldPlanarReflectionUniforms worldPlanarReflection_{};
    WorldPlanarReflectionView worldPlanarReflectionView_{};
};
}
