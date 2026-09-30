#pragma once

// Optional SDL platform extension. This is deliberately outside the ABI-35
// runtime vtable. Callbacks run on the host owner thread; pointers are borrowed
// only for the duration of each callback. A missing export preserves legacy input.
#include <SDL.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <string>

namespace engine::editor {
class IEditorProjectRuntime;
struct GameViewportRect { int x = 0, y = 0, width = 0, height = 0; };
inline GameViewportRect fitGameViewport(int width, int height, int numerator, int denominator) {
    if (width <= 0 || height <= 0) return {};
    if (numerator <= 0 || denominator <= 0) return {0, 0, width, height};
    const int divisor = std::gcd(numerator, denominator);
    numerator /= divisor; denominator /= divisor;
    const int scale = std::min(width / numerator, height / denominator);
    const int w = scale * numerator, h = scale * denominator;
    return {(width - w) / 2, (height - h) / 2, w, h};
}
struct EditorSdlGameInputContext {
    Uint32 windowId = 0;
    bool windowFocused = false;
    bool activeGameViewport = false;
    bool playing = false;
    bool paused = false;
    bool viewportFocused = false;
    bool viewportHovered = false;
    bool textEditing = false;
    bool synthetic = false;
    GameViewportRect viewport;
};
inline bool gameInputEnabled(const EditorSdlGameInputContext& c) {
    return c.windowFocused && c.activeGameViewport && c.playing && !c.paused &&
        c.viewportFocused && !c.textEditing && c.viewport.width > 0 && c.viewport.height > 0;
}
inline bool gamePointerInside(const EditorSdlGameInputContext& c, int x, int y) {
    return x >= c.viewport.x && y >= c.viewport.y &&
        x - c.viewport.x < c.viewport.width && y - c.viewport.y < c.viewport.height;
}
struct EditorSdlGameInputExtension {
    std::uint32_t version;
    std::uint32_t structureSize;
    std::uint64_t layoutFingerprint;
    void (*beginFrame)(IEditorProjectRuntime*, const EditorSdlGameInputContext*);
    void (*event)(IEditorProjectRuntime*, const EditorSdlGameInputContext*, const SDL_Event*);
    void (*endFrame)(IEditorProjectRuntime*, const EditorSdlGameInputContext*);
};
inline constexpr const char* kEditorSdlGameInputSymbol = "phlosionEditorSdlGameInput";
using EditorSdlGameInputFn = const EditorSdlGameInputExtension* (*)();
namespace detail {
constexpr std::uint64_t sdlInputAbiValue(std::uint64_t hash, std::uint64_t value) {
    for (int byte=0;byte<8;++byte) {hash^=value&0xffu;hash*=1099511628211ull;value>>=8u;}return hash;
}
template<class T> constexpr std::uint64_t sdlInputAbiType(std::uint64_t hash) {
    return sdlInputAbiValue(sdlInputAbiValue(hash,sizeof(T)),alignof(T));
}
}
inline constexpr std::uint64_t kEditorSdlGameInputFingerprint = [] {
    auto h = detail::sdlInputAbiType<SDL_Event>(14695981039346656037ull);
    h = detail::sdlInputAbiType<EditorSdlGameInputContext>(h);
    h = detail::sdlInputAbiType<GameViewportRect>(h);
    h = detail::sdlInputAbiType<EditorSdlGameInputExtension>(h);
#define SDL_CONTEXT_OFFSET(field) h=detail::sdlInputAbiValue(h,offsetof(EditorSdlGameInputContext,field))
    SDL_CONTEXT_OFFSET(windowId);SDL_CONTEXT_OFFSET(windowFocused);SDL_CONTEXT_OFFSET(activeGameViewport);
    SDL_CONTEXT_OFFSET(playing);SDL_CONTEXT_OFFSET(paused);SDL_CONTEXT_OFFSET(viewportFocused);
    SDL_CONTEXT_OFFSET(viewportHovered);SDL_CONTEXT_OFFSET(textEditing);SDL_CONTEXT_OFFSET(synthetic);SDL_CONTEXT_OFFSET(viewport);
#undef SDL_CONTEXT_OFFSET
    h = detail::sdlInputAbiValue(h, offsetof(EditorSdlGameInputExtension, beginFrame));
    h = detail::sdlInputAbiValue(h, offsetof(EditorSdlGameInputExtension, event));
    h = detail::sdlInputAbiValue(h, offsetof(EditorSdlGameInputExtension, endFrame));
    return detail::sdlInputAbiValue(h, SDL_COMPILEDVERSION);
}();
inline bool validateEditorSdlGameInput(const EditorSdlGameInputExtension* e, std::string& error) {
    // Do not read callback fields until all header checks have succeeded.
    if (!e || e->version != 1 || e->structureSize != sizeof(EditorSdlGameInputExtension) ||
        e->layoutFingerprint != kEditorSdlGameInputFingerprint) {
        error = "Malformed optional SDL game-input extension: version, size or layout mismatch.";
        return false;
    }
    if (!e->beginFrame || !e->event || !e->endFrame) {
        error = "Malformed optional SDL game-input extension: required callback missing.";
        return false;
    }
    return true;
}
} // namespace engine::editor
