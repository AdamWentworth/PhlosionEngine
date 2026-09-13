#pragma once
#include <string_view>

namespace engine::editor {
#if defined(NDEBUG) && defined(PHLOSION_EDITOR_BUILD_CONFIGURATION)
static_assert(std::string_view(PHLOSION_EDITOR_BUILD_CONFIGURATION) != "RelWithDebInfo",
    "Development must retain assertions. Reconfigure the editor build directory.");
#endif
constexpr std::string_view buildProfileName(std::string_view configuration) {
    return configuration == "RelWithDebInfo" ? "Development" : configuration;
}
} // namespace engine::editor
