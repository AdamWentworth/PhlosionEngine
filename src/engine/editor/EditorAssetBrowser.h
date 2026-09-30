#pragma once

#include "EditorShell.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <span>
#include <string_view>

namespace engine::editor {

struct AssetBrowserCategory {
    std::string path;
    std::string parent;
    std::string label;
    std::size_t count = 0;
};

inline std::string_view assetCategory(const WorkspaceAsset& asset) {
    return asset.category.empty() ? std::string_view("Other") : asset.category;
}

inline bool assetInCategory(const WorkspaceAsset& asset, std::string_view category) {
    const auto path = assetCategory(asset);
    return category.empty() || path == category ||
        (path.starts_with(category) && path.size() > category.size() && path[category.size()] == '/');
}

inline char assetNameLetter(std::string_view name) {
    for (const unsigned char ch : name) {
        if (std::isalpha(ch)) return static_cast<char>(std::toupper(ch));
    }
    return '#';
}

inline bool assetSearchContains(std::string_view text, std::string_view query) {
    return std::search(text.begin(), text.end(), query.begin(), query.end(),
        [](unsigned char left, unsigned char right) { return std::tolower(left) == std::tolower(right); }) != text.end()
        || query.empty();
}

inline std::vector<AssetBrowserCategory> assetBrowserCategories(std::span<const WorkspaceAsset> assets) {
    std::map<std::string, std::size_t> counts;
    for (const auto& asset : assets) {
        const auto path = assetCategory(asset);
        for (std::size_t end = path.find('/');; end = path.find('/', end + 1)) {
            ++counts[std::string(path.substr(0, end))];
            if (end == std::string_view::npos) break;
        }
    }
    std::vector<AssetBrowserCategory> result;
    for (const auto& [path, count] : counts) {
        const auto slash = path.rfind('/');
        result.push_back({path, slash == std::string::npos ? "" : path.substr(0, slash),
            slash == std::string::npos ? path : path.substr(slash + 1), count});
    }
    return result;
}

inline std::vector<std::size_t> filterAssetBrowser(std::span<const WorkspaceAsset> assets,
    std::string_view category, std::string_view query, char letter = 0) {
    std::vector<std::size_t> indices;
    for (std::size_t i = 0; i < assets.size(); ++i) {
        const auto& asset = assets[i];
        if (!assetInCategory(asset, category) || (letter && assetNameLetter(asset.displayName) != letter)) continue;
        if (query.empty() || assetSearchContains(asset.displayName, query) ||
            assetSearchContains(asset.typeName, query) || assetSearchContains(asset.category, query) ||
            assetSearchContains(asset.path, query)) indices.push_back(i);
    }
    return indices;
}

} // namespace engine::editor
