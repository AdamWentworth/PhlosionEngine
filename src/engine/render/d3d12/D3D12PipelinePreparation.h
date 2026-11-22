#pragma once
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <d3d12.h>
namespace engine::render {
HRESULT prepareRootSignature(ID3D12Device*,UINT,const void*,SIZE_T,REFIID,void**);
HRESULT prepareGraphicsPipeline(ID3D12Device*,const D3D12_GRAPHICS_PIPELINE_STATE_DESC*,REFIID,void**);
HRESULT prepareComputePipeline(ID3D12Device*,const D3D12_COMPUTE_PIPELINE_STATE_DESC*,REFIID,void**);
}
#endif
