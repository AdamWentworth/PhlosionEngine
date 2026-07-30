#pragma once

#include <cstdint>

namespace engine::render::world_blend {

enum class Mode : std::uint8_t {
    Alpha = 0u,
    Additive = 1u,
    Premultiplied = 2u,
};

struct State {
    Mode mode = Mode::Alpha;
    bool enabled = false;
    bool depthTestEnabled = true;
    bool dualSourceEnabled = false;
};

constexpr Mode sanitizeMode(std::uint8_t mode) noexcept {
    return mode == static_cast<std::uint8_t>(Mode::Additive)
        ? Mode::Additive
        : mode == static_cast<std::uint8_t>(Mode::Premultiplied)
            ? Mode::Premultiplied
            : Mode::Alpha;
}

constexpr State resolve(std::uint8_t alphaMode,
                        std::uint8_t blendMode,
                        bool depthTestEnabled,
                        bool dualSourceRequested,
                        bool dualSourceAvailable) noexcept {
    State state;
    state.enabled = alphaMode == 2u;
    state.depthTestEnabled = depthTestEnabled;
    state.mode = sanitizeMode(blendMode);
    state.dualSourceEnabled =
        state.enabled && dualSourceRequested && dualSourceAvailable;

    // The authored dual-source contract has alpha and additive equations.
    // A premultiplied request therefore resolves to its alpha equation, matching
    // the established OpenGL default branch.
    if (state.dualSourceEnabled && state.mode == Mode::Premultiplied) {
        state.mode = Mode::Alpha;
    }
    return state;
}

} // namespace engine::render::world_blend
