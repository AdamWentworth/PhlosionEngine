// ResourceManager.h

#pragma once
#include <string>
#include <unordered_map>
#include <memory>

class Model; // Forward declaration

// This class centralizes loading & caching of models (GLTF, etc.)
class ResourceManager {
public:
    ResourceManager() = default;

    // Loads (if not already loaded) and returns a shared pointer to the Model
    std::shared_ptr<Model> getModel(const std::string& modelPath);

    void clear() { loadedModels.clear(); }

private:
    // key: filepath, value: shared_ptr to the loaded Model
    std::unordered_map<std::string, std::shared_ptr<Model>> loadedModels;
};
