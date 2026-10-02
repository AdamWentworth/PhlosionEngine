#pragma once
#include "ModelData.h"
#include "ModelGeometryCache.h"
#include "ModelPose.h"
#include "WorldIndexedBatches.h"
#include <array>
#include <filesystem>
#include <memory>

namespace engine::models {

struct ModelFrame {
    glm::mat4 modelMatrix{1.0f};
    int animationIndex = -1;
    float animationTime = 0.0f;
    float materialTime = 0.0f;
    bool looping = true;
    glm::vec4 tint{1.0f};
    bool translucent = false;
    bool unlit = false;
    bool pairedSkinMatrices = false;
    bool inking = false;
    int graphicsQuality = 3;
    glm::vec3 cameraPosition{0.0f};
    glm::vec3 cameraUp{0.0f, 1.0f, 0.0f};
    glm::vec3 cameraFacing{0.0f, 0.0f, 1.0f};
};

// Optional source interpretation. The default handles ordinary model materials
// and authored animation. Implementations must borrow immutable model data only.
class ModelProfile {
public:
    virtual ~ModelProfile() = default;
    virtual bool validate(const MeshData& mesh, std::string& error) const;
    virtual float supportContactY(const MeshData& mesh) const;
    virtual void samplePose(const MeshData& mesh, const ModelFrame& frame, PoseEval& pose) const;
    virtual std::vector<std::uint8_t> preserveVertexColor(const MeshData& mesh) const;
    virtual std::vector<batches::WorldIndexedBatch> materialTemplates(
        const MeshData& mesh, const std::string& key, const ModelFrame& frame) const;
    virtual void prepareMaterial(const MeshData& mesh, const PoseEval& pose,
        const ModelFrame& frame, std::size_t submesh,
        const batches::WorldIndexedBatch& source, batches::WorldIndexedBatch& destination) const;
    virtual float visibility(const MeshData& mesh, const ModelFrame& frame,
        std::size_t submesh) const;
};

class ModelInstance {
public:
    explicit ModelInstance(std::shared_ptr<const ModelProfile> profile = {});
    ModelInstance(const ModelInstance&) = delete;
    ModelInstance& operator=(const ModelInstance&) = delete;
    ModelInstance(ModelInstance&&) = default;
    ModelInstance& operator=(ModelInstance&&) = default;
    // Failed loads leave the current resource and prepared frames intact.
    bool load(const std::filesystem::path& path, std::string* error = nullptr);
    bool setMesh(MeshData mesh, std::string* error = nullptr);
    void clear();
    const MeshData* mesh() const { return mesh_.get(); }
    bool samplePose(const ModelFrame& frame, PoseEval& pose, std::string* error = nullptr) const;
    bool sampleNode(const ModelFrame& frame, std::string_view name, glm::vec3& position,
        std::string* error = nullptr) const;
    // Each slot owns its palettes until that slot is prepared again or the
    // resource is replaced. Separate instances never share mutable palettes.
    // Reacquire the returned frame view after moving an instance.
    const std::vector<batches::WorldIndexedBatch>* prepare(const ModelFrame& frame,
        std::size_t slot = 0, std::string* error = nullptr);
private:
    struct FrameStorage {
        std::shared_ptr<const std::vector<batches::WorldIndexedBatch>> materials;
        std::vector<batches::WorldIndexedBatch> batches;
    };
    std::shared_ptr<const ModelProfile> profile_;
    std::shared_ptr<const MeshData> mesh_;
    geometry::FastTexturedMeshTemplateCache geometry_;
    std::array<std::shared_ptr<const std::vector<batches::WorldIndexedBatch>>, 8> materials_;
    std::array<FrameStorage, 16> frames_;
};
}
