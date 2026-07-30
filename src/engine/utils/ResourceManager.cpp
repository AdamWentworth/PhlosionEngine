// ResourceManager.cpp

#include "ResourceManager.h"

// Keep your existing include style to match your current include paths:
#include "engine/render/Model.h"

// NEW: optional fastgltf parsing/logging (does not change loader behavior)
#include "engine/render/gltf/FastGltfValidator.h"
#include "engine/utils/Log.h"



std::shared_ptr<Model> ResourceManager::getModel(const std::string& modelPath) {
    auto it = loadedModels.find(modelPath);
    if (it != loadedModels.end()) {
        return it->second;
    }

#if defined(PAC_VERBOSE_STARTUP) && PAC_VERBOSE_STARTUP
    LOG_INFO_T("RES", std::string("Loading model: ") + modelPath);
#endif

    // Optional fastgltf "shadow parse" for compatibility checking.
    // Enabled only if you set: PAC_FASTGLTF_VALIDATE=1
    pac::fastgltf_validator::logSummaryIfEnabled(modelPath);

    auto modelPtr = std::make_shared<Model>(modelPath);
    loadedModels.emplace(modelPath, modelPtr);
    return modelPtr;
}

