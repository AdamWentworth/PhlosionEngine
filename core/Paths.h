// src/engine/core/Paths.h
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "engine/core/Environment.h"

namespace engine::paths {

// ---------------- Assets ----------------
//
// Asset root can be overridden with env var PAC_ASSET_ROOT.
// Default is "assets".
inline std::string assetRoot() {
    if (const auto v = engine::env::get("PAC_ASSET_ROOT")) return *v;
    return "assets";
}

// Join asset root with a relative path (uses forward slashes).
inline std::string asset(std::string_view rel) {
    std::string root = assetRoot();
    if (!root.empty() && (root.back() == '/' || root.back() == '\\')) root.pop_back();

    std::string r(rel);
    while (!r.empty() && (r.front() == '/' || r.front() == '\\')) r.erase(r.begin());

    return root + "/" + r;
}

// ---------------- Data (repo/runtime root) ----------------
//
// Data root can be overridden with env var PAC_DATA_ROOT.
// Default is "." (current working directory).
//
// Use this for non-asset runtime files that you ship alongside the exe
// (scripts/, config/, etc.) when they are not under assets/.
inline std::string dataRoot() {
    if (const auto v = engine::env::get("PAC_DATA_ROOT")) return *v;
    // Dev convenience: if launched from a subfolder (e.g. dist/Release),
    // walk upward and prefer the repository root that contains source data.
    // This keeps config/script edits in the repo hot without duplicating files.
    std::error_code ec;
    std::filesystem::path p = std::filesystem::current_path(ec);
    if (!ec) {
        while (!p.empty()) {
            std::error_code probeEc;
            const bool hasGit =
                std::filesystem::exists(p / ".git", probeEc) && !probeEc;
            const bool hasConfig =
                std::filesystem::exists(p / "config" / "pokemon_config.json", probeEc) && !probeEc;
            const bool hasScripts =
                std::filesystem::exists(p / "scripts", probeEc) && !probeEc;
            if (hasGit && hasConfig && hasScripts) {
                return p.string();
            }
            const std::filesystem::path parent = p.parent_path();
            if (parent.empty() || parent == p) break;
            p = parent;
        }
    }
    return ".";
}

// Optional packed data bundle (scripts/config). Empty if not set.
inline std::string dataPack() {
    if (const auto v = engine::env::get("PAC_DATA_PACK")) return *v;
    return "";
}

// Join data root with a relative path (uses forward slashes).
inline std::string data(std::string_view rel) {
    std::string root = dataRoot();
    if (!root.empty() && (root.back() == '/' || root.back() == '\\')) root.pop_back();

    std::string r(rel);
    while (!r.empty() && (r.front() == '/' || r.front() == '\\')) r.erase(r.begin());

    return root + "/" + r;
}

} // namespace engine::paths
