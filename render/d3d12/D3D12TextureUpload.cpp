#include "engine/render/d3d12/D3D12TextureUpload.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#if defined(_WIN32)
#include <d3d12.h>

namespace {

struct CpuMipLevel {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> rgba;
};

constexpr int kGlRepeat = 10497;
constexpr int kGlClampToEdge = 33071;
constexpr int kGlMirroredRepeat = 33648;

float srgbByteToLinear(unsigned char v) {
    const float c = static_cast<float>(v) / 255.0f;
    if (c <= 0.04045f) {
        return c / 12.92f;
    }
    return std::pow((c + 0.055f) / 1.055f, 2.4f);
}

unsigned char linearToSrgbByte(float linear) {
    const float c = std::clamp(linear, 0.0f, 1.0f);
    const float srgb =
        (c <= 0.0031308f) ? (c * 12.92f) : (1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f);
    const int quantized = static_cast<int>(std::lround(srgb * 255.0f));
    return static_cast<unsigned char>(std::clamp(quantized, 0, 255));
}

int wrapTexelIndex(int index, int size, int wrapMode) {
    if (size <= 1) return 0;
    if (wrapMode == kGlClampToEdge) {
        if (index < 0) return 0;
        if (index >= size) return size - 1;
        return index;
    }
    if (wrapMode == kGlMirroredRepeat) {
        const int period = size * 2;
        int j = index % period;
        if (j < 0) j += period;
        if (j >= size) {
            j = period - 1 - j;
        }
        return j;
    }
    // Default to repeat for unknown modes.
    int j = index % size;
    if (j < 0) j += size;
    return j;
}

std::vector<CpuMipLevel> buildRgbaMipChain(const unsigned char* rgbaPixels,
                                           int width,
                                           int height,
                                           int wrapS,
                                           int wrapT,
                                           bool srgbColorData,
                                           bool generateMipChain,
                                           const engine::render::backend::
                                               WorldTextureMipLevel*
                                                   authoredMipLevels,
                                           std::uint32_t authoredMipLevelCount) {
    std::vector<CpuMipLevel> chain;
    if (!rgbaPixels || width <= 0 || height <= 0) return chain;

    bool authoredValid = authoredMipLevels && authoredMipLevelCount > 0u;
    for (std::uint32_t level = 0u;
         authoredValid && level < authoredMipLevelCount;
         ++level) {
        const auto& source = authoredMipLevels[level];
        authoredValid =
            source.rgba && source.width > 0 && source.height > 0 &&
            (level != 0u || (source.width == width && source.height == height));
    }
    if (authoredValid) {
        chain.reserve(authoredMipLevelCount);
        for (std::uint32_t level = 0u;
             level < authoredMipLevelCount;
             ++level) {
            const auto& source = authoredMipLevels[level];
            CpuMipLevel mip;
            mip.width = source.width;
            mip.height = source.height;
            mip.rgba.assign(
                source.rgba,
                source.rgba +
                    static_cast<std::size_t>(source.width) *
                        static_cast<std::size_t>(source.height) * 4u);
            chain.push_back(std::move(mip));
        }
        return chain;
    }

    CpuMipLevel base;
    base.width = width;
    base.height = height;
    base.rgba.assign(
        rgbaPixels,
        rgbaPixels + static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    chain.push_back(std::move(base));

    if (!generateMipChain) {
        return chain;
    }

    while (chain.back().width > 1 || chain.back().height > 1) {
        const CpuMipLevel& prev = chain.back();
        CpuMipLevel next;
        next.width = (prev.width / 2 > 0) ? (prev.width / 2) : 1;
        next.height = (prev.height / 2 > 0) ? (prev.height / 2) : 1;
        next.rgba.resize(static_cast<std::size_t>(next.width) * static_cast<std::size_t>(next.height) * 4u);

        for (int y = 0; y < next.height; ++y) {
            for (int x = 0; x < next.width; ++x) {
                float sumR = 0.0f;
                float sumG = 0.0f;
                float sumB = 0.0f;
                float sumA = 0.0f;
                std::uint32_t taps = 0;

                for (int oy = 0; oy < 2; ++oy) {
                    const int srcY = wrapTexelIndex(y * 2 + oy, prev.height, wrapT);
                    for (int ox = 0; ox < 2; ++ox) {
                        const int srcX = wrapTexelIndex(x * 2 + ox, prev.width, wrapS);
                        const std::size_t srcIndex =
                            (static_cast<std::size_t>(srcY) * static_cast<std::size_t>(prev.width) +
                             static_cast<std::size_t>(srcX)) * 4u;
                        if (srgbColorData) {
                            sumR += srgbByteToLinear(prev.rgba[srcIndex + 0]);
                            sumG += srgbByteToLinear(prev.rgba[srcIndex + 1]);
                            sumB += srgbByteToLinear(prev.rgba[srcIndex + 2]);
                        } else {
                            sumR += static_cast<float>(prev.rgba[srcIndex + 0]) / 255.0f;
                            sumG += static_cast<float>(prev.rgba[srcIndex + 1]) / 255.0f;
                            sumB += static_cast<float>(prev.rgba[srcIndex + 2]) / 255.0f;
                        }
                        sumA += static_cast<float>(prev.rgba[srcIndex + 3]) / 255.0f;
                        ++taps;
                    }
                }

                const std::size_t dstIndex =
                    (static_cast<std::size_t>(y) * static_cast<std::size_t>(next.width) +
                     static_cast<std::size_t>(x)) * 4u;
                const float invTaps = 1.0f / static_cast<float>((taps > 0) ? taps : 1u);
                if (srgbColorData) {
                    next.rgba[dstIndex + 0] = linearToSrgbByte(sumR * invTaps);
                    next.rgba[dstIndex + 1] = linearToSrgbByte(sumG * invTaps);
                    next.rgba[dstIndex + 2] = linearToSrgbByte(sumB * invTaps);
                } else {
                    const int qR = static_cast<int>(std::lround(sumR * invTaps * 255.0f));
                    const int qG = static_cast<int>(std::lround(sumG * invTaps * 255.0f));
                    const int qB = static_cast<int>(std::lround(sumB * invTaps * 255.0f));
                    next.rgba[dstIndex + 0] = static_cast<unsigned char>(std::clamp(qR, 0, 255));
                    next.rgba[dstIndex + 1] = static_cast<unsigned char>(std::clamp(qG, 0, 255));
                    next.rgba[dstIndex + 2] = static_cast<unsigned char>(std::clamp(qB, 0, 255));
                }
                const int alphaQ = static_cast<int>(std::lround(sumA * invTaps * 255.0f));
                next.rgba[dstIndex + 3] = static_cast<unsigned char>(std::clamp(alphaQ, 0, 255));
            }
        }

        chain.push_back(std::move(next));
    }

    return chain;
}

} // namespace

namespace engine::render::d3d12 {

bool createTextureResourcesFromRgbaBatch(ID3D12Device* device,
                                         ID3D12CommandQueue* commandQueue,
                                         ID3D12Fence* fence,
                                         HANDLE fenceEvent,
                                         std::uint64_t& fenceValue,
                                         ID3D12DescriptorHeap* srvHeap,
                                         std::uint32_t srvDescriptorSize,
                                         const RgbaTextureUploadRequest* requests,
                                         std::size_t requestCount) {
    if (!device || !commandQueue || !fence || !fenceEvent || !srvHeap || !requests ||
        requestCount == 0u) {
        return false;
    }

    struct PreparedTexture {
        std::vector<CpuMipLevel> mipChain;
        Microsoft::WRL::ComPtr<ID3D12Resource> texture;
        std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints;
        std::vector<UINT> numRows;
        std::vector<UINT64> rowSizeBytes;
        UINT mipLevels = 0u;
        UINT64 uploadBaseOffset = 0u;
        UINT64 uploadSize = 0u;
    };

    std::vector<PreparedTexture> prepared(requestCount);
    UINT64 uploadBufferSize = 0u;
    for (std::size_t i = 0; i < requestCount; ++i) {
        const RgbaTextureUploadRequest& req = requests[i];
        if (!req.rgbaPixels || req.width <= 0 || req.height <= 0 || !req.outTexture) {
            return false;
        }

        PreparedTexture& dst = prepared[i];
        dst.mipChain = buildRgbaMipChain(
            req.rgbaPixels,
            req.width,
            req.height,
            req.wrapS,
            req.wrapT,
            req.srgbColorData,
            req.generateMipChain,
            req.authoredMipLevels,
            req.authoredMipLevelCount);
        if (dst.mipChain.empty()) return false;
        dst.mipLevels = static_cast<UINT>(dst.mipChain.size());

        D3D12_HEAP_PROPERTIES defaultHeap{};
        defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
        defaultHeap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        defaultHeap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        defaultHeap.CreationNodeMask = 1;
        defaultHeap.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC texDesc{};
        texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texDesc.Alignment = 0;
        texDesc.Width = static_cast<UINT64>(req.width);
        texDesc.Height = static_cast<UINT>(req.height);
        texDesc.DepthOrArraySize = 1;
        texDesc.MipLevels = static_cast<UINT16>(dst.mipLevels);
        texDesc.Format = req.srgbColorData
            ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
            : DXGI_FORMAT_R8G8B8A8_UNORM;
        texDesc.SampleDesc.Count = 1;
        texDesc.SampleDesc.Quality = 0;
        texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        if (FAILED(device->CreateCommittedResource(&defaultHeap,
                                                   D3D12_HEAP_FLAG_NONE,
                                                   &texDesc,
                                                   D3D12_RESOURCE_STATE_COPY_DEST,
                                                   nullptr,
                                                   IID_PPV_ARGS(dst.texture.ReleaseAndGetAddressOf()))) ||
            !dst.texture) {
            return false;
        }

        dst.footprints.resize(dst.mipLevels);
        dst.numRows.resize(dst.mipLevels);
        dst.rowSizeBytes.resize(dst.mipLevels);

        UINT64 localUploadSize = 0u;
        device->GetCopyableFootprints(&texDesc,
                                      0,
                                      dst.mipLevels,
                                      0,
                                      dst.footprints.data(),
                                      dst.numRows.data(),
                                      dst.rowSizeBytes.data(),
                                      &localUploadSize);
        if (localUploadSize == 0u) return false;

        uploadBufferSize = (uploadBufferSize + D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1u) &
                           ~(static_cast<UINT64>(D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT) - 1u);
        dst.uploadBaseOffset = uploadBufferSize;
        dst.uploadSize = localUploadSize;
        uploadBufferSize += localUploadSize;
    }

    if (uploadBufferSize == 0u) return false;

    D3D12_HEAP_PROPERTIES uploadHeap{};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    uploadHeap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    uploadHeap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    uploadHeap.CreationNodeMask = 1;
    uploadHeap.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC uploadDesc{};
    uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadDesc.Alignment = 0;
    uploadDesc.Width = uploadBufferSize;
    uploadDesc.Height = 1;
    uploadDesc.DepthOrArraySize = 1;
    uploadDesc.MipLevels = 1;
    uploadDesc.Format = DXGI_FORMAT_UNKNOWN;
    uploadDesc.SampleDesc.Count = 1;
    uploadDesc.SampleDesc.Quality = 0;
    uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    uploadDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    Microsoft::WRL::ComPtr<ID3D12Resource> upload;
    if (FAILED(device->CreateCommittedResource(&uploadHeap,
                                               D3D12_HEAP_FLAG_NONE,
                                               &uploadDesc,
                                               D3D12_RESOURCE_STATE_GENERIC_READ,
                                               nullptr,
                                               IID_PPV_ARGS(upload.ReleaseAndGetAddressOf()))) ||
        !upload) {
        return false;
    }

    void* mapped = nullptr;
    D3D12_RANGE readRange{0, 0};
    if (FAILED(upload->Map(0, &readRange, &mapped)) || !mapped) {
        return false;
    }

    auto* mappedBytes = static_cast<unsigned char*>(mapped);
    for (std::size_t i = 0; i < requestCount; ++i) {
        PreparedTexture& src = prepared[i];
        for (UINT mip = 0; mip < src.mipLevels; ++mip) {
            const CpuMipLevel& mipCpu = src.mipChain[mip];
            const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint = src.footprints[mip];
            const std::size_t srcRowPitch = static_cast<std::size_t>(mipCpu.width) * 4u;
            for (int row = 0; row < mipCpu.height; ++row) {
                auto* dstRow = mappedBytes +
                    src.uploadBaseOffset +
                    footprint.Offset +
                    static_cast<std::size_t>(row) *
                        static_cast<std::size_t>(footprint.Footprint.RowPitch);
                const auto* srcRow = mipCpu.rgba.data() +
                    static_cast<std::size_t>(row) * srcRowPitch;
                std::memcpy(dstRow, srcRow, srcRowPitch);
            }
        }
    }

    D3D12_RANGE writeRange{0, static_cast<SIZE_T>(uploadBufferSize)};
    upload->Unmap(0, &writeRange);

    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> copyAllocator;
    if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              IID_PPV_ARGS(copyAllocator.ReleaseAndGetAddressOf()))) ||
        !copyAllocator) {
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> copyList;
    if (FAILED(device->CreateCommandList(0,
                                         D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         copyAllocator.Get(),
                                         nullptr,
                                         IID_PPV_ARGS(copyList.ReleaseAndGetAddressOf()))) ||
        !copyList) {
        return false;
    }

    for (std::size_t i = 0; i < requestCount; ++i) {
        PreparedTexture& src = prepared[i];
        for (UINT mip = 0; mip < src.mipLevels; ++mip) {
            D3D12_TEXTURE_COPY_LOCATION dstLoc{};
            dstLoc.pResource = src.texture.Get();
            dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dstLoc.SubresourceIndex = mip;

            D3D12_TEXTURE_COPY_LOCATION srcLoc{};
            srcLoc.pResource = upload.Get();
            srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            srcLoc.PlacedFootprint = src.footprints[mip];
            srcLoc.PlacedFootprint.Offset += src.uploadBaseOffset;

            copyList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
        }

        D3D12_RESOURCE_BARRIER toShader{};
        toShader.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toShader.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        toShader.Transition.pResource = src.texture.Get();
        toShader.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        toShader.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        toShader.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        copyList->ResourceBarrier(1, &toShader);
    }

    if (FAILED(copyList->Close())) return false;

    ID3D12CommandList* lists[] = {copyList.Get()};
    commandQueue->ExecuteCommandLists(1, lists);

    const std::uint64_t signalValue = ++fenceValue;
    if (FAILED(commandQueue->Signal(fence, signalValue))) return false;
    if (fence->GetCompletedValue() < signalValue) {
        if (FAILED(fence->SetEventOnCompletion(signalValue, fenceEvent))) return false;
        WaitForSingleObject(fenceEvent, INFINITE);
    }

    for (std::size_t i = 0; i < requestCount; ++i) {
        const RgbaTextureUploadRequest& req = requests[i];
        PreparedTexture& src = prepared[i];

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = req.srgbColorData
            ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
            : DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = src.mipLevels;
        srvDesc.Texture2D.PlaneSlice = 0;
        srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = srvHeap->GetCPUDescriptorHandleForHeapStart();
        cpuHandle.ptr += static_cast<SIZE_T>(req.descriptorIndex) *
                         static_cast<SIZE_T>(srvDescriptorSize);
        device->CreateShaderResourceView(src.texture.Get(), &srvDesc, cpuHandle);
        *req.outTexture = src.texture;
    }

    return true;
}

bool createTextureResourceFromRgba(ID3D12Device* device,
                                   ID3D12CommandQueue* commandQueue,
                                   ID3D12Fence* fence,
                                   HANDLE fenceEvent,
                                   std::uint64_t& fenceValue,
                                   ID3D12DescriptorHeap* srvHeap,
                                   std::uint32_t srvDescriptorSize,
                                   std::uint32_t descriptorIndex,
                                   const unsigned char* rgbaPixels,
                                   int width,
                                   int height,
                                   int wrapS,
                                   int wrapT,
                                   bool generateMipChain,
                                   bool srgbColorData,
                                   Microsoft::WRL::ComPtr<ID3D12Resource>& outTexture,
                                   const engine::render::backend::
                                       WorldTextureMipLevel* authoredMipLevels,
                                   std::uint32_t authoredMipLevelCount) {
    if (!device || !commandQueue || !fence || !fenceEvent || !srvHeap || !rgbaPixels || width <= 0 || height <= 0) {
        return false;
    }
    std::vector<CpuMipLevel> mipChain =
        buildRgbaMipChain(
            rgbaPixels,
            width,
            height,
            wrapS,
            wrapT,
            srgbColorData,
            generateMipChain,
            authoredMipLevels,
            authoredMipLevelCount);
    if (mipChain.empty()) return false;
    const UINT mipLevels = static_cast<UINT>(mipChain.size());

    D3D12_HEAP_PROPERTIES defaultHeap{};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    defaultHeap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    defaultHeap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    defaultHeap.CreationNodeMask = 1;
    defaultHeap.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC texDesc{};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Alignment = 0;
    texDesc.Width = static_cast<UINT64>(width);
    texDesc.Height = static_cast<UINT>(height);
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = static_cast<UINT16>(mipLevels);
    texDesc.Format = srgbColorData
        ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
        : DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    Microsoft::WRL::ComPtr<ID3D12Resource> texture;
    if (FAILED(device->CreateCommittedResource(&defaultHeap,
                                               D3D12_HEAP_FLAG_NONE,
                                               &texDesc,
                                               D3D12_RESOURCE_STATE_COPY_DEST,
                                               nullptr,
                                               IID_PPV_ARGS(texture.ReleaseAndGetAddressOf()))) ||
        !texture) {
        return false;
    }

    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(mipLevels);
    std::vector<UINT> numRows(mipLevels);
    std::vector<UINT64> rowSizeBytes(mipLevels);
    UINT64 uploadBufferSize = 0;
    device->GetCopyableFootprints(&texDesc,
                                  0,
                                  mipLevels,
                                  0,
                                  footprints.data(),
                                  numRows.data(),
                                  rowSizeBytes.data(),
                                  &uploadBufferSize);
    if (uploadBufferSize == 0) return false;

    D3D12_HEAP_PROPERTIES uploadHeap{};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    uploadHeap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    uploadHeap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    uploadHeap.CreationNodeMask = 1;
    uploadHeap.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC uploadDesc{};
    uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadDesc.Alignment = 0;
    uploadDesc.Width = uploadBufferSize;
    uploadDesc.Height = 1;
    uploadDesc.DepthOrArraySize = 1;
    uploadDesc.MipLevels = 1;
    uploadDesc.Format = DXGI_FORMAT_UNKNOWN;
    uploadDesc.SampleDesc.Count = 1;
    uploadDesc.SampleDesc.Quality = 0;
    uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    uploadDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    Microsoft::WRL::ComPtr<ID3D12Resource> upload;
    if (FAILED(device->CreateCommittedResource(&uploadHeap,
                                               D3D12_HEAP_FLAG_NONE,
                                               &uploadDesc,
                                               D3D12_RESOURCE_STATE_GENERIC_READ,
                                               nullptr,
                                               IID_PPV_ARGS(upload.ReleaseAndGetAddressOf()))) ||
        !upload) {
        return false;
    }

    void* mapped = nullptr;
    D3D12_RANGE readRange{0, 0};
    if (FAILED(upload->Map(0, &readRange, &mapped)) || !mapped) {
        return false;
    }
    auto* mappedBytes = static_cast<unsigned char*>(mapped);
    for (UINT mip = 0; mip < mipLevels; ++mip) {
        const auto& mipCpu = mipChain[mip];
        const auto& footprint = footprints[mip];
        const std::size_t srcRowPitch = static_cast<std::size_t>(mipCpu.width) * 4u;
        for (int row = 0; row < mipCpu.height; ++row) {
            auto* dstRow = mappedBytes +
                footprint.Offset +
                static_cast<std::size_t>(row) * static_cast<std::size_t>(footprint.Footprint.RowPitch);
            const auto* srcRow =
                mipCpu.rgba.data() + static_cast<std::size_t>(row) * srcRowPitch;
            std::memcpy(dstRow, srcRow, srcRowPitch);
        }
    }
    D3D12_RANGE writeRange{0, static_cast<SIZE_T>(uploadBufferSize)};
    upload->Unmap(0, &writeRange);

    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> copyAllocator;
    if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              IID_PPV_ARGS(copyAllocator.ReleaseAndGetAddressOf()))) ||
        !copyAllocator) {
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> copyList;
    if (FAILED(device->CreateCommandList(0,
                                         D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         copyAllocator.Get(),
                                         nullptr,
                                         IID_PPV_ARGS(copyList.ReleaseAndGetAddressOf()))) ||
        !copyList) {
        return false;
    }

    for (UINT mip = 0; mip < mipLevels; ++mip) {
        D3D12_TEXTURE_COPY_LOCATION dstLoc{};
        dstLoc.pResource = texture.Get();
        dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstLoc.SubresourceIndex = mip;

        D3D12_TEXTURE_COPY_LOCATION srcLoc{};
        srcLoc.pResource = upload.Get();
        srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        srcLoc.PlacedFootprint = footprints[mip];

        copyList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
    }

    D3D12_RESOURCE_BARRIER toShader{};
    toShader.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toShader.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    toShader.Transition.pResource = texture.Get();
    toShader.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    toShader.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    toShader.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    copyList->ResourceBarrier(1, &toShader);

    if (FAILED(copyList->Close())) return false;

    ID3D12CommandList* lists[] = {copyList.Get()};
    commandQueue->ExecuteCommandLists(1, lists);

    const std::uint64_t signalValue = ++fenceValue;
    if (FAILED(commandQueue->Signal(fence, signalValue))) return false;
    if (fence->GetCompletedValue() < signalValue) {
        if (FAILED(fence->SetEventOnCompletion(signalValue, fenceEvent))) return false;
        WaitForSingleObject(fenceEvent, INFINITE);
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = srgbColorData
        ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
        : DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = mipLevels;
    srvDesc.Texture2D.PlaneSlice = 0;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = srvHeap->GetCPUDescriptorHandleForHeapStart();
    cpuHandle.ptr += static_cast<SIZE_T>(descriptorIndex) * static_cast<SIZE_T>(srvDescriptorSize);
    device->CreateShaderResourceView(texture.Get(), &srvDesc, cpuHandle);

    outTexture = texture;
    return true;
}

bool createTextureResourceFromRgba16F(ID3D12Device* device,
                                      ID3D12CommandQueue* commandQueue,
                                      ID3D12Fence* fence,
                                      HANDLE fenceEvent,
                                      std::uint64_t& fenceValue,
                                      ID3D12DescriptorHeap* srvHeap,
                                      std::uint32_t srvDescriptorSize,
                                      std::uint32_t descriptorIndex,
                                      const std::uint16_t* rgba16fPixels,
                                      int width,
                                      int height,
                                      Microsoft::WRL::ComPtr<ID3D12Resource>& outTexture) {
    if (!device || !commandQueue || !fence || !fenceEvent || !srvHeap || !rgba16fPixels ||
        width <= 0 || height <= 0) {
        return false;
    }

    D3D12_HEAP_PROPERTIES defaultHeap{};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    defaultHeap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    defaultHeap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    defaultHeap.CreationNodeMask = 1;
    defaultHeap.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC texDesc{};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Alignment = 0;
    texDesc.Width = static_cast<UINT64>(width);
    texDesc.Height = static_cast<UINT>(height);
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    Microsoft::WRL::ComPtr<ID3D12Resource> texture;
    if (FAILED(device->CreateCommittedResource(&defaultHeap,
                                               D3D12_HEAP_FLAG_NONE,
                                               &texDesc,
                                               D3D12_RESOURCE_STATE_COPY_DEST,
                                               nullptr,
                                               IID_PPV_ARGS(texture.ReleaseAndGetAddressOf()))) ||
        !texture) {
        return false;
    }

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT numRows = 0;
    UINT64 rowSizeBytes = 0;
    UINT64 uploadBufferSize = 0;
    device->GetCopyableFootprints(&texDesc,
                                  0,
                                  1,
                                  0,
                                  &footprint,
                                  &numRows,
                                  &rowSizeBytes,
                                  &uploadBufferSize);
    if (uploadBufferSize == 0) return false;

    D3D12_HEAP_PROPERTIES uploadHeap{};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    uploadHeap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    uploadHeap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    uploadHeap.CreationNodeMask = 1;
    uploadHeap.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC uploadDesc{};
    uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadDesc.Alignment = 0;
    uploadDesc.Width = uploadBufferSize;
    uploadDesc.Height = 1;
    uploadDesc.DepthOrArraySize = 1;
    uploadDesc.MipLevels = 1;
    uploadDesc.Format = DXGI_FORMAT_UNKNOWN;
    uploadDesc.SampleDesc.Count = 1;
    uploadDesc.SampleDesc.Quality = 0;
    uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    uploadDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    Microsoft::WRL::ComPtr<ID3D12Resource> upload;
    if (FAILED(device->CreateCommittedResource(&uploadHeap,
                                               D3D12_HEAP_FLAG_NONE,
                                               &uploadDesc,
                                               D3D12_RESOURCE_STATE_GENERIC_READ,
                                               nullptr,
                                               IID_PPV_ARGS(upload.ReleaseAndGetAddressOf()))) ||
        !upload) {
        return false;
    }

    void* mapped = nullptr;
    D3D12_RANGE readRange{0, 0};
    if (FAILED(upload->Map(0, &readRange, &mapped)) || !mapped) {
        return false;
    }
    auto* mappedBytes = static_cast<unsigned char*>(mapped);
    const std::size_t srcRowPitch =
        static_cast<std::size_t>(width) * 4u * sizeof(std::uint16_t);
    for (int row = 0; row < height; ++row) {
        auto* dstRow = mappedBytes +
            footprint.Offset +
            static_cast<std::size_t>(row) * static_cast<std::size_t>(footprint.Footprint.RowPitch);
        const auto* srcRow = reinterpret_cast<const unsigned char*>(rgba16fPixels) +
            static_cast<std::size_t>(row) * srcRowPitch;
        std::memcpy(dstRow, srcRow, srcRowPitch);
    }
    D3D12_RANGE writeRange{0, static_cast<SIZE_T>(uploadBufferSize)};
    upload->Unmap(0, &writeRange);

    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> copyAllocator;
    if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              IID_PPV_ARGS(copyAllocator.ReleaseAndGetAddressOf()))) ||
        !copyAllocator) {
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> copyList;
    if (FAILED(device->CreateCommandList(0,
                                         D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         copyAllocator.Get(),
                                         nullptr,
                                         IID_PPV_ARGS(copyList.ReleaseAndGetAddressOf()))) ||
        !copyList) {
        return false;
    }

    D3D12_TEXTURE_COPY_LOCATION dstLoc{};
    dstLoc.pResource = texture.Get();
    dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLoc.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION srcLoc{};
    srcLoc.pResource = upload.Get();
    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    srcLoc.PlacedFootprint = footprint;

    copyList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

    D3D12_RESOURCE_BARRIER toShader{};
    toShader.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toShader.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    toShader.Transition.pResource = texture.Get();
    toShader.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    toShader.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    toShader.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    copyList->ResourceBarrier(1, &toShader);

    if (FAILED(copyList->Close())) return false;

    ID3D12CommandList* lists[] = {copyList.Get()};
    commandQueue->ExecuteCommandLists(1, lists);

    const std::uint64_t signalValue = ++fenceValue;
    if (FAILED(commandQueue->Signal(fence, signalValue))) return false;
    if (fence->GetCompletedValue() < signalValue) {
        if (FAILED(fence->SetEventOnCompletion(signalValue, fenceEvent))) return false;
        WaitForSingleObject(fenceEvent, INFINITE);
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.PlaneSlice = 0;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = srvHeap->GetCPUDescriptorHandleForHeapStart();
    cpuHandle.ptr += static_cast<SIZE_T>(descriptorIndex) * static_cast<SIZE_T>(srvDescriptorSize);
    device->CreateShaderResourceView(texture.Get(), &srvDesc, cpuHandle);

    outTexture = texture;
    return true;
}

} // namespace engine::render::d3d12
#endif
