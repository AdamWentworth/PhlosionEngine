#pragma once

#include <memory>

namespace engine::tools::vfx_preview {

class IVfxPreviewProject;

class VfxPreviewApp {
public:
    explicit VfxPreviewApp(std::unique_ptr<IVfxPreviewProject> project);
    ~VfxPreviewApp();

    int run();

private:
    std::unique_ptr<IVfxPreviewProject> project_;
};

} // namespace engine::tools::vfx_preview
