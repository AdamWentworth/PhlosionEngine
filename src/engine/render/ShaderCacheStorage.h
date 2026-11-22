#pragma once
#include "engine/core/Environment.h"
#include "engine/core/Paths.h"
#include <array>
#include <cstdint>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <thread>
#include <span>
#include <sstream>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace engine::render::shader_cache {
inline std::uint64_t hashBytes(std::span<const std::uint8_t> bytes, std::uint64_t hash=14695981039346656037ull) {
    for (auto b:bytes) hash=(hash^b)*1099511628211ull;
    return hash;
}
struct Key {
    std::uint64_t value=14695981039346656037ull;
    void bytes(const void* data,std::size_t size) {
        const auto count=static_cast<std::uint64_t>(size);
        value=hashBytes({reinterpret_cast<const std::uint8_t*>(&count),sizeof(count)},value);
        if (size) value=hashBytes({static_cast<const std::uint8_t*>(data),size},value);
    }
    template<class T> void field(const T& v) { bytes(&v,sizeof(v)); }
    void text(std::string_view s) { bytes(s.data(),s.size()); }
};
inline std::string name(std::uint64_t key) {
    std::ostringstream out;out<<std::hex<<std::setw(16)<<std::setfill('0')<<key;return out.str();
}
inline std::filesystem::path writableRoot() {
    if (auto root=engine::env::get("PHLOSION_SHADER_CACHE_DIR");root&&!root->empty()) return *root;
#ifdef _WIN32
    if (auto root=engine::env::get("LOCALAPPDATA");root&&!root->empty()) return std::filesystem::path(*root)/"Phlosion"/"ShaderCache";
#else
    if (auto root=engine::env::get("XDG_CACHE_HOME");root&&!root->empty()) return std::filesystem::path(*root)/"phlosion"/"shaders";
    if (auto root=engine::env::get("HOME");root&&!root->empty()) return std::filesystem::path(*root)/".cache"/"phlosion"/"shaders";
#endif
    return std::filesystem::path(engine::paths::data("cache/shaders"));
}
inline std::filesystem::path preparedRoot() {
    if (auto root=engine::env::get("PHLOSION_PREPARED_SHADER_DIR");root&&!root->empty()) return *root;
    return std::filesystem::path(engine::paths::data(".phlosion/shaders"));
}
inline bool atomicWrite(const std::filesystem::path& path,std::span<const std::uint8_t> bytes) noexcept {
    std::filesystem::path temp;
    try {
        std::error_code ec;std::filesystem::create_directories(path.parent_path(),ec);if(ec) return false;
        static std::atomic_uint64_t serial{0};
        auto suffix=std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(serial.fetch_add(1));
#ifdef _WIN32
        suffix+="-"+std::to_string(GetCurrentProcessId());
#endif
        temp=path;temp+="."+suffix+".tmp";
        {std::ofstream out(temp,std::ios::binary|std::ios::trunc);if(!out)return false;
         out.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
         out.close();if(!out){std::filesystem::remove(temp,ec);return false;}}
#ifdef _WIN32
        static std::mutex replacementMutex;
        std::lock_guard replacement(replacementMutex);
        bool ok=MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
        for(unsigned attempt=0;!ok&&attempt<8;++attempt) {
            const auto error=GetLastError();
            if(error!=ERROR_SHARING_VIOLATION&&error!=ERROR_ACCESS_DENIED)break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            ok=MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
        }
#else
        std::filesystem::rename(temp,path,ec);const bool ok=!ec;
#endif
        if(!ok)std::filesystem::remove(temp,ec);
        return ok;
    } catch (...) { std::error_code ec;if(!temp.empty())std::filesystem::remove(temp,ec);return false; }
}
inline constexpr std::uint64_t magic=0x3250485348434143ull;
inline constexpr std::uint64_t version=1;
inline constexpr std::size_t maxShaderBytes=16u*1024u*1024u;
using Header=std::array<std::uint64_t,6>;
inline std::vector<std::uint8_t> encode(std::uint64_t kind,std::uint64_t key,std::span<const std::uint8_t> payload) {
    Header header{magic,version,kind,key,payload.size(),hashBytes(payload)};
    std::vector<std::uint8_t> file(sizeof(header)+payload.size());
    std::memcpy(file.data(),header.data(),sizeof(header));
    std::memcpy(file.data()+sizeof(header),payload.data(),payload.size());return file;
}
inline std::span<const std::uint8_t> decode(std::span<const std::uint8_t> file,std::uint64_t kind,std::uint64_t key,std::size_t maximum) {
    if(file.size()<sizeof(Header)||file.size()-sizeof(Header)>maximum)return {};
    Header h{};std::memcpy(h.data(),file.data(),sizeof(h));
    const auto payload=file.subspan(sizeof(h));
    if(h[0]!=magic||h[1]!=version||h[2]!=kind||h[3]!=key||h[4]!=payload.size()||h[5]!=hashBytes(payload))return {};
    return payload;
}
inline std::vector<std::uint8_t> read(const std::filesystem::path& path,std::uint64_t kind,std::uint64_t key,std::size_t maximum) noexcept {
    try {
        std::ifstream in(path,std::ios::binary|std::ios::ate);if(!in)return {};
        const auto size=in.tellg();if(size<static_cast<std::streamoff>(sizeof(Header))||size>static_cast<std::streamoff>(maximum+sizeof(Header)))return {};
        std::vector<std::uint8_t> file(static_cast<std::size_t>(size));in.seekg(0);
        if(!in.read(reinterpret_cast<char*>(file.data()),size))return {};
        auto payload=decode(file,kind,key,maximum);return {payload.begin(),payload.end()};
    } catch (...) { return {}; }
}
inline bool write(const std::filesystem::path& path,std::uint64_t kind,std::uint64_t key,std::span<const std::uint8_t> payload) noexcept {
    try { return atomicWrite(path,encode(kind,key,payload)); } catch (...) { return false; }
}
}
