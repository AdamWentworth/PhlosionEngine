#include "engine/editor/EditorProjectPlugin.h"
#include "engine/editor/EditorProjectPluginContract.h"

#include <string>

namespace {

engine::editor::IEditorProjectRuntime* fakeCreateRuntime() {
    return nullptr;
}

void fakeDestroyRuntime(engine::editor::IEditorProjectRuntime*) {}

engine::editor::EditorProjectPluginContract validContract() {
    return {
        engine::editor::kEditorProjectPluginAbiVersion,
        sizeof(engine::editor::EditorProjectPluginContract),
        engine::editor::kEditorProjectPluginLayoutFingerprint,
        "Debug",
        engine::editor::kEditorProjectPluginCompilerAbi,
        &fakeCreateRuntime,
        &fakeDestroyRuntime,
    };
}

bool expectRejected(
    const engine::editor::EditorProjectPluginContract& contract,
    const std::string& expectedMessage,
    std::string& outFail) {
    std::string error;
    if (engine::editor::validateEditorProjectPluginContract(
            &contract,
            "Debug",
            error)) {
        outFail = "invalid contract was accepted";
        return false;
    }
    if (error.find(expectedMessage) == std::string::npos) {
        outFail =
            "diagnostic did not contain '" + expectedMessage +
            "': " + error;
        return false;
    }
    return true;
}

} // namespace

bool test_editor_project_plugin_contract(std::string& outFail) {
    auto contract = validContract();
    std::string error;
    if (!engine::editor::validateEditorProjectPluginContract(
            &contract,
            "Debug",
            error)) {
        outFail = "valid contract was rejected: " + error;
        return false;
    }

    contract = validContract();
    contract.structureSize -= 1u;
    if (!expectRejected(contract, "contract size mismatch", outFail)) {
        return false;
    }
    contract = validContract();
    contract.abiVersion -= 1u;
    if (!expectRejected(contract, "ABI version mismatch", outFail)) {
        return false;
    }
    contract = validContract();
    contract.layoutFingerprint ^= 1u;
    if (!expectRejected(contract, "ABI layout mismatch", outFail)) {
        return false;
    }
    contract = validContract();
    contract.buildConfiguration = "Release";
    if (!expectRejected(
            contract,
            "build-configuration mismatch",
            outFail)) {
        return false;
    }
    contract = validContract();
    contract.compilerAbi = "different-toolchain";
    if (!expectRejected(contract, "compiler ABI mismatch", outFail)) {
        return false;
    }
    contract = validContract();
    contract.destroyRuntime = nullptr;
    if (!expectRejected(
            contract,
            "missing required runtime callbacks",
            outFail)) {
        return false;
    }
    return true;
}
