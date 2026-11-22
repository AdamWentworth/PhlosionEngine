#pragma once
#include <cstdint>
namespace engine::render {
// Temporal response classes are independent of lighting channels, BRDF and
// exact scene identity. Values are engine metadata, not source stencil IDs.
enum class WorldTemporalMaterial : std::uint32_t {
    Geometry=0, Character=1, Vehicle=2, ReducedHistory=3, Sky=4
};
inline bool validWorldTemporalMaterial(WorldTemporalMaterial value) {
    return static_cast<std::uint32_t>(value)<=static_cast<std::uint32_t>(WorldTemporalMaterial::Sky);
}
}
