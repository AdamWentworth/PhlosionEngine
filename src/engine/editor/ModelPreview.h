#pragma once
#include "EditorProjectPlugin.h"
#include "engine/models/ModelInstance.h"
#include "engine/models/ModelLighting.h"
namespace engine::editor {
using ModelPreviewLighting = models::ModelLighting;
class ModelPreview {
public:
    explicit ModelPreview(std::shared_ptr<const models::ModelProfile> profile = {},
        ModelPreviewLighting lighting = {});
    ~ModelPreview();
    ModelPreview(const ModelPreview&) = delete;
    ModelPreview& operator=(const ModelPreview&) = delete;
    bool select(const char* assetId, const char* path, std::string* error = nullptr);
    EditorProjectAssetPreviewInfo info() const noexcept;
    EditorProjectAssetAnimation animation(std::size_t index) const noexcept;
    void setOptions(const EditorProjectAssetPreviewOptions& options);
    void update(float deltaSeconds);
    void render(const EditorProjectRenderContext& context);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
