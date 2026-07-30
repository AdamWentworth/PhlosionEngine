#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstdint>
#include <unordered_set>
#endif

namespace engine::render::dxgi {

struct AdapterPreferenceCandidate {
    std::string name;
    bool discrete = false;
    bool software = false;
};

inline std::string toLowerCopy(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

inline bool containsCi(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) return false;
    return toLowerCopy(haystack).find(toLowerCopy(needle)) != std::string::npos;
}

inline int selectPreferredAdapterCandidate(const std::vector<AdapterPreferenceCandidate>& candidates,
                                           std::string_view preferredName) {
    if (candidates.empty()) return -1;

    if (!preferredName.empty()) {
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            if (containsCi(candidates[i].name, preferredName)) {
                return static_cast<int>(i);
            }
        }
    }

    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const auto& candidate = candidates[i];
        if (candidate.discrete && !candidate.software) {
            return static_cast<int>(i);
        }
    }

    for (std::size_t i = 0; i < candidates.size(); ++i) {
        if (!candidates[i].software) {
            return static_cast<int>(i);
        }
    }

    return 0;
}

#if defined(_WIN32)

struct AdapterCandidate {
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    DXGI_ADAPTER_DESC1 desc{};
    std::string name;
    bool discrete = false;
};

struct AdapterSelection {
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    DXGI_ADAPTER_DESC1 desc{};
    std::string name;
    bool discrete = false;
    bool preferredMatched = false;
};

inline std::string utf8FromWide(const wchar_t* wide) {
    if (!wide || *wide == L'\0') return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1) return {};
    std::string out(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), needed, nullptr, nullptr);
    if (!out.empty() && out.back() == '\0') out.pop_back();
    return out;
}

inline bool isSoftwareAdapter(const DXGI_ADAPTER_DESC1& desc) {
    return (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
}

inline bool isLikelyDiscreteAdapter(const DXGI_ADAPTER_DESC1& desc) {
    if (isSoftwareAdapter(desc)) return false;
    // Heuristic: Intel vendor IDs are typically integrated on hybrid systems.
    return desc.VendorId != 0x8086;
}

inline std::uint64_t luidKey(const LUID& luid) {
    const auto hi = static_cast<std::uint64_t>(static_cast<std::uint32_t>(luid.HighPart));
    const auto lo = static_cast<std::uint64_t>(static_cast<std::uint32_t>(luid.LowPart));
    return (hi << 32u) | lo;
}

inline std::vector<AdapterCandidate> enumerateHardwareAdapters(IDXGIFactory1* factory) {
    std::vector<AdapterCandidate> out;
    if (!factory) return out;

    std::unordered_set<std::uint64_t> seenLuids;
    auto append = [&](Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter) {
        if (!adapter) return;

        DXGI_ADAPTER_DESC1 desc{};
        if (FAILED(adapter->GetDesc1(&desc))) return;
        if (isSoftwareAdapter(desc)) return;

        const std::uint64_t key = luidKey(desc.AdapterLuid);
        if (!seenLuids.insert(key).second) return;

        AdapterCandidate candidate;
        candidate.adapter = std::move(adapter);
        candidate.desc = desc;
        candidate.name = utf8FromWide(desc.Description);
        candidate.discrete = isLikelyDiscreteAdapter(desc);
        out.push_back(std::move(candidate));
    };

    Microsoft::WRL::ComPtr<IDXGIFactory6> factory6;
    if (SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(factory6.ReleaseAndGetAddressOf()))) && factory6) {
        for (UINT i = 0;; ++i) {
            Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
            const HRESULT hr = factory6->EnumAdapterByGpuPreference(
                i,
                DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                IID_PPV_ARGS(adapter.ReleaseAndGetAddressOf()));
            if (hr == DXGI_ERROR_NOT_FOUND) break;
            if (FAILED(hr)) continue;
            append(std::move(adapter));
        }
    }

    for (UINT i = 0;; ++i) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        const HRESULT hr = factory->EnumAdapters1(i, adapter.ReleaseAndGetAddressOf());
        if (hr == DXGI_ERROR_NOT_FOUND) break;
        if (FAILED(hr)) continue;
        append(std::move(adapter));
    }

    return out;
}

inline AdapterSelection selectHardwareAdapter(IDXGIFactory1* factory, std::string_view preferredName) {
    AdapterSelection out;
    const auto candidates = enumerateHardwareAdapters(factory);
    if (candidates.empty()) return out;

    std::vector<AdapterPreferenceCandidate> policyCandidates;
    policyCandidates.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        policyCandidates.push_back(AdapterPreferenceCandidate{
            candidate.name, candidate.discrete, false
        });
    }

    const int index = selectPreferredAdapterCandidate(policyCandidates, preferredName);
    if (index < 0 || static_cast<std::size_t>(index) >= candidates.size()) {
        return out;
    }

    const auto& selected = candidates[static_cast<std::size_t>(index)];
    out.adapter = selected.adapter;
    out.desc = selected.desc;
    out.name = selected.name;
    out.discrete = selected.discrete;
    out.preferredMatched = !preferredName.empty() && containsCi(selected.name, preferredName);
    return out;
}

#endif // defined(_WIN32)

} // namespace engine::render::dxgi
