#include "engine/editor/EditorGamePreviewRouting.h"

namespace engine::editor {

std::optional<std::size_t> preferredGamePreviewRoute(
    std::span<const GamePreviewRoute> previews,
    std::string_view activeSceneId,
    std::string_view activePreviewId) noexcept {
    std::optional<std::size_t> activePreview;
    std::optional<std::size_t> firstScenePreview;
    std::optional<std::size_t> firstApplicationPreview;

    for (std::size_t index = 0u;
         index < previews.size();
         ++index) {
        const auto& preview = previews[index];
        if (!activePreviewId.empty() &&
            preview.id == activePreviewId) {
            activePreview = index;
        }
        if (!activeSceneId.empty() &&
            preview.sceneId == activeSceneId &&
            !firstScenePreview) {
            firstScenePreview = index;
        }
        if (preview.sceneId.empty() &&
            !firstApplicationPreview) {
            firstApplicationPreview = index;
        }
    }

    if (activePreview &&
        !activeSceneId.empty() &&
        previews[*activePreview].sceneId == activeSceneId) {
        return activePreview;
    }
    if (firstScenePreview) {
        return firstScenePreview;
    }
    if (activePreview) {
        return activePreview;
    }
    if (firstApplicationPreview) {
        return firstApplicationPreview;
    }
    if (!previews.empty()) {
        return 0u;
    }
    return std::nullopt;
}

bool shouldForwardGamePreviewInput(
    bool playModeActive,
    bool mouseWheelEvent) noexcept {
    return playModeActive || mouseWheelEvent;
}

} // namespace engine::editor
