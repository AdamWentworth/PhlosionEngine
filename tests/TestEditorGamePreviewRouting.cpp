#include "engine/editor/EditorGamePreviewRouting.h"

#include <array>
#include <string>

bool test_editor_game_preview_routing_contract(
    std::string& outFail) {
    using engine::editor::GamePreviewRoute;
    using engine::editor::preferredGamePreviewRoute;
    using engine::editor::shouldForwardGamePreviewInput;

    constexpr std::array previews{
        GamePreviewRoute{"main-menu", ""},
        GamePreviewRoute{
            "district-planning",
            "districts/central"},
        GamePreviewRoute{
            "district-action",
            "districts/central"},
        GamePreviewRoute{
            "harbor-planning",
            "districts/harbor"},
    };

    const auto central = preferredGamePreviewRoute(
        previews,
        "districts/central",
        "main-menu");
    const auto retainedBattle = preferredGamePreviewRoute(
        previews,
        "districts/central",
        "district-action");
    const auto harbor = preferredGamePreviewRoute(
        previews,
        "districts/harbor",
        "district-planning");
    const auto reference = preferredGamePreviewRoute(
        previews,
        "reference/central",
        "main-menu");
    const auto empty = preferredGamePreviewRoute(
        {},
        "districts/central",
        "main-menu");

    if (central != 1u || retainedBattle != 2u ||
        harbor != 3u || reference != 0u || empty) {
        outFail =
            "Game-preview routing should prefer the active scene's first "
            "preview, retain an active preview for that scene, and fall "
            "back safely for scenes without an association.";
        return false;
    }
    if (!shouldForwardGamePreviewInput(false, true) ||
        !shouldForwardGamePreviewInput(true, false) ||
        shouldForwardGamePreviewInput(false, false)) {
        outFail =
            "Game-preview wheel input should remain available for camera "
            "navigation in edit mode while other gameplay input stays "
            "gated until play mode is active.";
        return false;
    }
    return true;
}
