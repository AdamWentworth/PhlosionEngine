#include "engine/render/d3d12/D3D12RenderBackendPipelineCompile.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <system_error>

#if defined(_WIN32)
namespace {

std::uint64_t fnv1a64Append(std::uint64_t hash, const void* data, std::size_t byteCount) {
    static constexpr std::uint64_t kPrime = 1099511628211ull;
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < byteCount; ++i) {
        hash ^= static_cast<std::uint64_t>(bytes[i]);
        hash *= kPrime;
    }
    return hash;
}

std::filesystem::path shaderBlobCachePath(std::uint64_t key) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::path("cache") / "shaders" / "d3d12";
    std::error_code ec;
    fs::create_directories(dir, ec);

    std::ostringstream name;
    name << std::hex << std::setw(16) << std::setfill('0') << key << ".cso";
    return dir / name.str();
}

bool tryLoadShaderBlobFromCache(std::uint64_t key, Microsoft::WRL::ComPtr<ID3DBlob>& outBlob) {
    outBlob.Reset();
    const std::filesystem::path path = shaderBlobCachePath(key);
    if (!std::filesystem::exists(path)) return false;
    const std::wstring widePath = path.wstring();
    return SUCCEEDED(D3DReadFileToBlob(widePath.c_str(), outBlob.ReleaseAndGetAddressOf())) && outBlob;
}

void tryWriteShaderBlobToCache(std::uint64_t key, ID3DBlob* blob) {
    if (!blob) return;
    const std::filesystem::path path = shaderBlobCachePath(key);
    const std::wstring widePath = path.wstring();
    (void)D3DWriteBlobToFile(blob, widePath.c_str(), TRUE);
}

} // namespace

namespace engine::render::d3d12_pipeline_compile {

std::string d3dCompileErrorMessage(ID3DBlob* errBlob) {
    if (!errBlob || !errBlob->GetBufferPointer() || errBlob->GetBufferSize() == 0) {
        return {};
    }
    const char* msg = static_cast<const char*>(errBlob->GetBufferPointer());
    return std::string(msg, msg + errBlob->GetBufferSize());
}

UINT d3dCompileFlags() {
#if defined(_DEBUG)
    // Keep shader symbols in Debug, but allow opt-in optimized shader code so
    // renderer perf tracks Release more closely during day-to-day testing.
#if defined(PAC_OPTIMIZE_D3D12_SHADERS_IN_DEBUG)
    return D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_DEBUG;
#else
    return D3DCOMPILE_SKIP_OPTIMIZATION | D3DCOMPILE_DEBUG;
#endif
#else
    return D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
}

bool compileHlslWithCache(const void* sourceData,
                          std::size_t sourceSize,
                          const char* entryPoint,
                          const char* target,
                          UINT flags1,
                          UINT flags2,
                          Microsoft::WRL::ComPtr<ID3DBlob>& outBlob,
                          Microsoft::WRL::ComPtr<ID3DBlob>& errBlob) {
    outBlob.Reset();
    errBlob.Reset();

    static constexpr std::uint64_t kCacheSchemaVersion = 1ull;
    std::uint64_t hash = 14695981039346656037ull;
    hash = fnv1a64Append(hash, &kCacheSchemaVersion, sizeof(kCacheSchemaVersion));
    hash = fnv1a64Append(hash, &flags1, sizeof(flags1));
    hash = fnv1a64Append(hash, &flags2, sizeof(flags2));
    hash = fnv1a64Append(hash, entryPoint, std::strlen(entryPoint));
    hash = fnv1a64Append(hash, target, std::strlen(target));
    hash = fnv1a64Append(hash, sourceData, sourceSize);

    if (tryLoadShaderBlobFromCache(hash, outBlob)) {
        return true;
    }

    if (FAILED(D3DCompile(sourceData,
                          sourceSize,
                          nullptr,
                          nullptr,
                          nullptr,
                          entryPoint,
                          target,
                          flags1,
                          flags2,
                          outBlob.ReleaseAndGetAddressOf(),
                          errBlob.ReleaseAndGetAddressOf())) ||
        !outBlob) {
        return false;
    }

    tryWriteShaderBlobToCache(hash, outBlob.Get());
    return true;
}

} // namespace engine::render::d3d12_pipeline_compile
#endif
