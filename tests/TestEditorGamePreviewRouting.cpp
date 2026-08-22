#include "engine/editor/EditorGamePreviewRouting.h"

#include <array>
#include <string>

bool test_editor_game_preview_routing_contract(
    std::string& outFail) {
    using engine::editor::GamePreviewRoute;
    using engine::editor::preferredGamePreviewRoute;

    constexpr std::array previews{
        GamePreviewRoute{"main-menu", ""},
        GamePreviewRoute{
            "route1-planning-classic",
            "routes/route1"},
        GamePreviewRoute{
            "route1-battle-classic",
            "routes/route1"},
        GamePreviewRoute{
            "route1-5-planning-classic",
            "routes/route1-5"},
    };

    const auto route1 = preferredGamePreviewRoute(
        previews,
        "routes/route1",
        "main-menu");
    const auto retainedBattle = preferredGamePreviewRoute(
        previews,
        "routes/route1",
        "route1-battle-classic");
    const auto route1_5 = preferredGamePreviewRoute(
        previews,
        "routes/route1-5",
        "route1-planning-classic");
    const auto reference = preferredGamePreviewRoute(
        previews,
        "reference/route1",
        "main-menu");
    const auto empty = preferredGamePreviewRoute(
        {},
        "routes/route1",
        "main-menu");

    if (route1 != 1u || retainedBattle != 2u ||
        route1_5 != 3u || reference != 0u || empty) {
        outFail =
            "Game-preview routing should prefer the active scene's first "
            "preview, retain an active preview for that scene, and fall "
            "back safely for scenes without an association.";
        return false;
    }
    return true;
}
