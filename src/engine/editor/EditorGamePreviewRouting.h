#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

namespace engine::editor {

struct GamePreviewRoute {
    std::string_view id;
    std::string_view sceneId;
};

std::optional<std::size_t> preferredGamePreviewRoute(
    std::span<const GamePreviewRoute> previews,
    std::string_view activeSceneId,
    std::string_view activePreviewId) noexcept;

bool shouldForwardGamePreviewInput(
    bool playModeActive,
    bool mouseWheelEvent) noexcept;

} // namespace engine::editor
