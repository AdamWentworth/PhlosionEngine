#pragma once
#include "engine/editor/EditorSdlGameInput.h"
#include <filesystem>
#include <memory>

namespace engine::input {
// Bounded opt-in automation. Owns only its virtual device/subsystem reference;
// pushes events, never drains the host queue. Destroy before external SDL_Quit.
class SdlInputReplay {
public:
    explicit SdlInputReplay(const std::filesystem::path& script);
    ~SdlInputReplay();
    SdlInputReplay(const SdlInputReplay&) = delete;
    SdlInputReplay& operator=(const SdlInputReplay&) = delete;
    void frame(editor::EditorSdlGameInputContext& context);
    int frameNumber() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace engine::input
