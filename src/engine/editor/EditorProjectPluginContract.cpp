#include "engine/editor/EditorProjectPluginContract.h"

#include <cstring>
#include <string>

namespace engine::editor {

bool validateEditorProjectPluginContract(
    const EditorProjectPluginContract* contract,
    const char* expectedBuildConfiguration,
    std::string& outError) {
    if (!contract) {
        outError = "Project editor plugin returned a null ABI contract.";
        return false;
    }
    if (contract->structureSize != sizeof(EditorProjectPluginContract)) {
        outError =
            "Project editor plugin contract size mismatch (expected " +
            std::to_string(sizeof(EditorProjectPluginContract)) +
            ", got " + std::to_string(contract->structureSize) +
            "). Rebuild the editor and project plugin as a pair.";
        return false;
    }
    if (contract->abiVersion != kEditorProjectPluginAbiVersion) {
        outError =
            "Project editor plugin ABI version mismatch (expected " +
            std::to_string(kEditorProjectPluginAbiVersion) +
            ", got " + std::to_string(contract->abiVersion) +
            "). Rebuild the editor and project plugin as a pair.";
        return false;
    }
    if (!contract->buildConfiguration ||
        !expectedBuildConfiguration ||
        std::strcmp(
            contract->buildConfiguration,
            expectedBuildConfiguration) != 0) {
        outError =
            "Project editor plugin build-configuration mismatch (editor " +
            std::string(expectedBuildConfiguration
                    ? expectedBuildConfiguration
                    : "unknown") +
            ", plugin " +
            std::string(contract->buildConfiguration
                    ? contract->buildConfiguration
                    : "unknown") +
            "). Load the plugin from the matching configuration directory.";
        return false;
    }
    if (!contract->compilerAbi ||
        std::strcmp(
            contract->compilerAbi,
            kEditorProjectPluginCompilerAbi) != 0) {
        outError =
            "Project editor plugin compiler ABI mismatch (editor " +
            std::string(kEditorProjectPluginCompilerAbi) +
            ", plugin " +
            std::string(contract->compilerAbi
                    ? contract->compilerAbi
                    : "unknown") +
            "). Rebuild both artifacts with the same toolchain.";
        return false;
    }
    if (contract->layoutFingerprint !=
        kEditorProjectPluginLayoutFingerprint) {
        outError =
            "Project editor plugin ABI layout mismatch. Rebuild the editor "
            "and project plugin as a pair.";
        return false;
    }
    if (!contract->createRuntime || !contract->destroyRuntime) {
        outError =
            "Project editor plugin contract is missing required runtime "
            "callbacks. Rebuild the project plugin.";
        return false;
    }
    outError.clear();
    return true;
}

} // namespace engine::editor
