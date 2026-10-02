#pragma once
#include "ModelPreview.h"
namespace engine::editor {
class ModelPreviewRuntime final : public IEditorProjectRuntime {
public:
    using Factory = std::function<std::unique_ptr<ModelPreview>(const EditorProjectOpenContext&)>;
    explicit ModelPreviewRuntime(Factory factory = {}) : factory_(std::move(factory)) {}
    bool open(const EditorProjectOpenContext& context,std::string* error) override {
        auto preview=factory_ ? factory_(context) : std::make_unique<ModelPreview>();
        if (!preview) { if (error) *error="Model preview creation failed"; return false; }
        preview_=std::move(preview);
        if (error) error->clear();
        return true;
    }
    void prewarm(IRenderBackend&,const EditorProjectCameraContext&) override {}
    void update(float) override {}
    void render(const EditorProjectRenderContext&) override {}
    EditorProjectStats stats() const override { return {}; }
    const char* status() const noexcept override { return "Model preview provider ready."; }
    bool selectAssetPreview(const char* id,const char* path,std::string* error) override {
        if (!preview_) { if (error) *error="Model preview is not open"; return false; }
        return preview_->select(id,path,error);
    }
    EditorProjectAssetPreviewInfo assetPreviewInfo() const noexcept override { return preview_ ? preview_->info() : EditorProjectAssetPreviewInfo{}; }
    EditorProjectAssetAnimation assetPreviewAnimation(std::size_t index) const noexcept override { return preview_ ? preview_->animation(index) : EditorProjectAssetAnimation{}; }
    void setAssetPreviewOptions(const EditorProjectAssetPreviewOptions& options) override { if (preview_) preview_->setOptions(options); }
    void updateAssetPreview(float seconds) override { if (preview_) preview_->update(seconds); }
    void renderAssetPreview(const EditorProjectRenderContext& context) override { if (preview_) preview_->render(context); }
private:
    Factory factory_;
    std::unique_ptr<ModelPreview> preview_;
};
}
