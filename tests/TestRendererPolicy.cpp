#include "engine/render/RendererPolicy.h"
#include <iostream>
#include <stdexcept>

using namespace engine::render;
static void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template<class F> static void rejects(F action) {
    bool rejected = false;
    try { action(); } catch (const std::exception&) { rejected = true; }
    check(rejected, "Invalid or disallowed renderer policy was accepted");
}
int main() {
    try {
        const auto game = parseRendererPolicy(R"({"rendering":{"windows":["vulkan","d3d12"],"linux":["vulkan"]}})");
        check(rendererCandidates(game).front() == "vulkan", "Project preference order lost");
        check(rendererCandidates(game, "vulkan").size() == 1, "Explicit requests must not fall back");
        rejects([&] { rendererCandidates(game, "opengl"); });
        rejects([&] { rendererCandidates(game, "typo"); });
        rejects([&] { rendererCandidates(game, "auto", "macos"); });
        rejects([&] { rendererCandidates(game, "d3d12", "linux"); });
        check(availableRenderers(game, "linux") == std::vector<std::string>{"vulkan"}, "Linux enrollment leaked");
#if defined(_WIN32)
        check(rendererCandidates(game) == std::vector<std::string>({"vulkan","d3d12"}), "Fallback order changed");
#endif
        for (const auto* bad : {
            R"({"rendering":{}})", R"({"rendering":{"windows":[]}})",
            R"({"rendering":{"windows":["vulkan","vulkan"]}})",
            R"({"rendering":{"windows":["auto"]}})", R"({"rendering":{"linux":["d3d12"]}})",
            R"({"rendering":{"windwos":["vulkan"]}})", R"({"rendering":{"windows":[3]}})"})
            rejects([&] { parseRendererPolicy(bad); });
        const auto legacy = parseRendererPolicy(R"({"rendering":{"windows":["opengl"],"linux":["opengl"]}})");
#if PHLOSION_ENABLE_OPENGL
        check(rendererCandidates(legacy).front() == "opengl", "Opt-in legacy build lost OpenGL");
#else
        rejects([&] { rendererCandidates(legacy); });
        check(!rendererCompiled("opengl"), "OpenGL available in disabled build");
#endif
        std::cout << "Renderer policy: ordered enrollment, explicit requests, platform and build gates PASS\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
