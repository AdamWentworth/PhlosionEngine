#pragma once

#include <cstdint>

namespace engine::editor {

class EditorRenderSurface {
public:
    virtual ~EditorRenderSurface() = default;

    virtual bool begin(int width, int height) = 0;
    virtual void end() = 0;
    virtual void shutdown() = 0;
    virtual std::uint64_t textureId() const noexcept = 0;
};

} // namespace engine::editor
