#pragma once

#include <cstddef>
#include <string>

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

namespace engine::render::d3d12_pipeline_compile {

std::string d3dCompileErrorMessage(ID3DBlob* errBlob);
UINT d3dCompileFlags();
bool compileHlslWithCache(const void* sourceData,
                          std::size_t sourceSize,
                          const char* entryPoint,
                          const char* target,
                          UINT flags1,
                          UINT flags2,
                          Microsoft::WRL::ComPtr<ID3DBlob>& outBlob,
                          Microsoft::WRL::ComPtr<ID3DBlob>& errBlob);

} // namespace engine::render::d3d12_pipeline_compile
#endif
