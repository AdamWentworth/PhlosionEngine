#include "engine/editor/EditorAssetBrowser.h"

#include <array>

bool test_editor_asset_browser_contract(std::string& outFail) {
    using namespace engine::editor;
    const std::array assets{
        WorkspaceAsset{.id="one", .displayName="0197_Unicorn", .typeName="Prefab", .category="Characters", .path="one.phlo"},
        WorkspaceAsset{.id="two", .displayName="Spark - Attack", .typeName="Visual Effect", .category="Battle VFX/Spark", .path="two"},
        WorkspaceAsset{.id="three", .displayName="Spark - Damage", .typeName="Visual Effect", .category="Battle VFX/Spark", .path="three"},
        WorkspaceAsset{.id="four", .displayName="Fox", .typeName="Animated Model", .category="Models/Archive/Animals", .path="four"},
        WorkspaceAsset{.id="five", .displayName="Other", .typeName="Model", .category="Models/ArchiveExtra", .path="five"},
        WorkspaceAsset{.id="six", .displayName="123", .path="misc"},
    };
    const auto fail = [&](const char* reason) { outFail = reason; return false; };
    if (filterAssetBrowser(assets, "Battle VFX", "") != std::vector<std::size_t>{1,2} ||
        filterAssetBrowser(assets, "Battle VFX/Spark", "damage") != std::vector<std::size_t>{2} ||
        filterAssetBrowser(assets, "Models/Archive", "") != std::vector<std::size_t>{3})
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
    const auto animals = std::find_if(categories.begin(), categories.end(),
        [](const auto& row) { return row.path == "Models/Archive/Animals"; });
    if (models == categories.end() || models->count != 2 || animals == categories.end() ||
        animals->parent != "Models/Archive" || animals->label != "Animals" || animals->count != 1)
        return fail("Category tree must contain ancestors, readable labels and descendant counts.");
    if (filterAssetBrowser(assets, "", "").size() != assets.size())
        return fail("Clearing filters must restore the complete catalog.");
    return true;
}
