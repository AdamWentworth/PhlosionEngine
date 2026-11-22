#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

namespace engine::render::d3d12_pipeline_compile {

enum class HlslCompiler { Modern, Legacy };

std::string d3dCompileErrorMessage(ID3DBlob* errBlob);
UINT d3dCompileFlags();
std::uint64_t shaderRequestKey(const void* source, std::size_t size,
    const char* entry, const char* target, UINT flags1, UINT flags2,
    HlslCompiler compiler = HlslCompiler::Modern);
bool compileHlslWithCache(const void* sourceData,
                          std::size_t sourceSize,
                          const char* entryPoint,
                          const char* target,
                          UINT flags1,
                          UINT flags2,
                          Microsoft::WRL::ComPtr<ID3DBlob>& outBlob,
                          Microsoft::WRL::ComPtr<ID3DBlob>& errBlob,
                          HlslCompiler compiler = HlslCompiler::Modern);

} // namespace engine::render::d3d12_pipeline_compile
#endif
