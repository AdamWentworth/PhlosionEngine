#include "engine/editor/EditorProjectPlugin.h"
#include "engine/editor/EditorProjectPluginContract.h"

#include <filesystem>
#include <iostream>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#ifndef PHLOSION_EDITOR_BUILD_CONFIGURATION
#define PHLOSION_EDITOR_BUILD_CONFIGURATION "Unknown"
#endif

namespace {

class DynamicLibrary {
public:
    ~DynamicLibrary() {
        close();
    }

    bool open(
        const std::filesystem::path& path,
        std::string& outError) {
#if defined(_WIN32)
        handle_ = LoadLibraryExW(
            path.c_str(),
            nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!handle_) {
            outError =
                "Could not load project editor plugin (Windows error " +
                std::to_string(GetLastError()) + "): " +
                path.generic_string();
            return false;
        }
#else
        handle_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle_) {
            const char* message = dlerror();
            outError =
                "Could not load project editor plugin: " +
                std::string(message ? message : "unknown error") +
                " (" + path.generic_string() + ")";
            return false;
        }
#endif
        outError.clear();
        return true;
    }

    template <typename Function>
    Function function(const char* name) const {
#if defined(_WIN32)
        return reinterpret_cast<Function>(GetProcAddress(handle_, name));
#else
        return reinterpret_cast<Function>(dlsym(handle_, name));
#endif
    }

private:
    void close() {
        if (!handle_) {
            return;
        }
#if defined(_WIN32)
        FreeLibrary(handle_);
#else
        dlclose(handle_);
#endif
        handle_ = nullptr;
    }

#if defined(_WIN32)
    HMODULE handle_ = nullptr;
#else
    void* handle_ = nullptr;
#endif
};

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr
            << "Usage: PhlosionEditorPluginProbe <project-plugin>\n";
        return 2;
    }

    const std::filesystem::path pluginPath =
        std::filesystem::absolute(argv[1]);
    if (!std::filesystem::is_regular_file(pluginPath)) {
        std::cerr << "Plugin does not exist: "
                  << pluginPath.generic_string() << '\n';
        return 2;
    }

    DynamicLibrary library;
    std::string error;
    if (!library.open(pluginPath, error)) {
        std::cerr << error << '\n';
        return 1;
    }

    const auto getContract =
        library.function<
            engine::editor::EditorProjectPluginContractFn>(
            engine::editor::kEditorProjectPluginContractSymbol);
    if (!getContract) {
        std::cerr
            << "Project editor plugin is missing the required ABI contract "
            << "symbol '"
            << engine::editor::kEditorProjectPluginContractSymbol
            << "'. Rebuild the editor and project plugin as a pair.\n";
        return 1;
    }

    const auto* contract = getContract();
    if (!engine::editor::validateEditorProjectPluginContract(
            contract,
            PHLOSION_EDITOR_BUILD_CONFIGURATION,
            error)) {
        std::cerr << error << '\n';
        return 1;
    }

    std::cout
        << "Compatible project editor plugin: "
        << pluginPath.generic_string() << '\n'
        << "  configuration: " << contract->buildConfiguration << '\n'
        << "  ABI version: " << contract->abiVersion << '\n'
        << "  contract size: " << contract->structureSize << '\n'
        << "  layout fingerprint: " << contract->layoutFingerprint << '\n'
        << "  compiler ABI: " << contract->compilerAbi << '\n';
    return 0;
}
