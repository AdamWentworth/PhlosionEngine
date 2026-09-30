#include "engine/editor/EditorAssetBrowser.h"

#include <array>

bool test_editor_asset_browser_contract(std::string& outFail) {
    using namespace engine::editor;
    const std::array assets{
        WorkspaceAsset{.id="one", .displayName="0197_Umbreon_SV", .typeName="Prefab", .category="Characters", .path="one.phlo"},
        WorkspaceAsset{.id="two", .displayName="Bite - Attack", .typeName="Visual Effect", .category="Battle VFX/Bite", .path="two"},
        WorkspaceAsset{.id="three", .displayName="Bite - Damage", .typeName="Visual Effect", .category="Battle VFX/Bite", .path="three"},
        WorkspaceAsset{.id="four", .displayName="Espeon", .typeName="Animated Model", .category="Models/Colosseum/Pokemon", .path="four"},
        WorkspaceAsset{.id="five", .displayName="Other", .typeName="Model", .category="Models/ColosseumExtra", .path="five"},
        WorkspaceAsset{.id="six", .displayName="123", .path="misc"},
    };
    const auto fail = [&](const char* reason) { outFail = reason; return false; };
    if (filterAssetBrowser(assets, "Battle VFX", "") != std::vector<std::size_t>{1,2} ||
        filterAssetBrowser(assets, "Battle VFX/Bite", "damage") != std::vector<std::size_t>{2} ||
        filterAssetBrowser(assets, "Models/Colosseum", "") != std::vector<std::size_t>{3})
        return fail("Category descendants and filtered indices must retain exact source asset identities.");
    if (filterAssetBrowser(assets, "", "PREFAB", 'U') != std::vector<std::size_t>{0} ||
        filterAssetBrowser(assets, "", "FOUR") != std::vector<std::size_t>{3} ||
        filterAssetBrowser(assets, "Other", "", '#') != std::vector<std::size_t>{5})
        return fail("Search must be case insensitive and alphabet browsing must skip numeric prefixes.");
    if (!filterAssetBrowser(assets, "missing", "").empty() ||
        !filterAssetBrowser({}, "", "").empty() || !assetBrowserCategories({}).empty())
        return fail("Missing/empty collections must stay empty.");
    const auto categories = assetBrowserCategories(assets);
    const auto models = std::find_if(categories.begin(), categories.end(),
        [](const auto& row) { return row.path == "Models"; });
    const auto pokemon = std::find_if(categories.begin(), categories.end(),
        [](const auto& row) { return row.path == "Models/Colosseum/Pokemon"; });
    if (models == categories.end() || models->count != 2 || pokemon == categories.end() ||
        pokemon->parent != "Models/Colosseum" || pokemon->label != "Pokemon" || pokemon->count != 1)
        return fail("Category tree must contain ancestors, readable labels and descendant counts.");
    if (filterAssetBrowser(assets, "", "").size() != assets.size())
        return fail("Clearing filters must restore the complete catalog.");
    return true;
}
