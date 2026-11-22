#pragma once
#include "engine/core/IAssetStore.h"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <typeindex>
#include <vector>

namespace engine::assets {
struct ResourceDependency {
    std::filesystem::path path;
    std::uint64_t bytes=0, hash=0;
    bool current() const;
};
class ResourceLoadContext {
public:
    std::vector<std::uint8_t> read(const std::filesystem::path&,std::uint64_t maxBytes=512ull<<20);
    void depend(const std::filesystem::path&,std::uint64_t maxBytes=512ull<<20);
    bool cancelled() const;
    const std::vector<ResourceDependency>& dependencies() const {return dependencies_;}
private:
    friend class ResourceBank;
    friend struct ResourceBankCore;
    std::vector<ResourceDependency> dependencies_;
    std::function<bool()> cancelled_;
    std::uint64_t inputBytes_=0;
};
// Tracks every external buffer/image requested by a decoder. Virtual paths stay
// inside root; prepared publication verification remains the game's boundary.
class ResourceFileStore final : public engine::IAssetStore {
public:
    ResourceFileStore(ResourceLoadContext&,std::filesystem::path root);
    bool readBytes(const std::string&,std::vector<std::uint8_t>&,std::string*) const override;
    bool readText(const std::string&,std::string&,std::string*) const override;
    bool exists(const std::string&) const override;
private:
    std::filesystem::path resolve(const std::string&) const;
    ResourceLoadContext& context_;
    std::filesystem::path root_;
};
struct ResourceBankOptions {
    std::size_t workers=2,maxRequests=256;
    std::uint64_t maxResidentBytes=1024ull<<20;
    std::chrono::milliseconds idleRetention{30000};
};
struct ResourceBankStats {
    std::uint64_t hits=0,loads=0,failures=0,cancellations=0,evictions=0;
    std::uint64_t residentBytes=0,retiredBytes=0,peakResidentBytes=0,loadedBytes=0;
    std::size_t entries=0,queued=0,loading=0;
    std::uint64_t inputBytes=0,loadMicroseconds=0;
};
enum class ResourceState {Queued,Loading,Ready,Failed,Cancelled};
struct ResourceBankCore;
struct ResourceBankEntry;
class ResourceRequest {
public:
    ResourceRequest()=default;
    ~ResourceRequest();
    ResourceRequest(ResourceRequest&&) noexcept;
    ResourceRequest& operator=(ResourceRequest&&) noexcept;
    ResourceRequest(const ResourceRequest&)=delete;
    ResourceRequest& operator=(const ResourceRequest&)=delete;
    ResourceState state() const;
    void cancel();
    template<class T> std::shared_ptr<const T> get() {
        return std::static_pointer_cast<const T>(wait(typeid(T)));
    }
private:
    friend class ResourceBank;
    ResourceRequest(std::shared_ptr<ResourceBankCore>,std::shared_ptr<ResourceBankEntry>);
    std::shared_ptr<const void> wait(std::type_index);
    std::shared_ptr<ResourceBankCore> core_;
    std::shared_ptr<ResourceBankEntry> entry_;
    bool subscribed_=false;
};
template<class T> struct ResourceValue {
    std::shared_ptr<const T> value;
    std::uint64_t bytes=0;
};
class ResourceBank {
public:
    explicit ResourceBank(ResourceBankOptions options={});
    ~ResourceBank();
    ResourceBank(const ResourceBank&)=delete;
    ResourceBank& operator=(const ResourceBank&)=delete;
    template<class T,class F> ResourceRequest request(std::string key,F loader,int priority=0) {
        return requestErased(std::move(key),typeid(T),
            [fn=std::move(loader)](ResourceLoadContext& context) {
                ResourceValue<T> v=fn(context);
                return ErasedValue{std::move(v.value),v.bytes};
            },priority);
    }
    template<class T,class F> std::shared_ptr<const T> acquire(std::string key,F loader) {
        return request<T>(std::move(key),std::move(loader)).template get<T>();
    }
    // Only unused resources are evicted. Active payloads survive invalidation;
    // their bytes remain counted until the last consumer releases them.
    void trim(bool allUnused=false);
    void invalidate(const std::string& key);
    void stop();
    ResourceBankStats stats() const;
private:
    struct ErasedValue {std::shared_ptr<const void> value;std::uint64_t bytes;};
    using Loader=std::function<ErasedValue(ResourceLoadContext&)>;
    friend struct ResourceBankCore;
    ResourceRequest requestErased(std::string,std::type_index,Loader,int);
    std::shared_ptr<ResourceBankCore> core_;
    struct Workers;
    std::unique_ptr<Workers> workers_;
};
// One preparation context per host/project. Decoder jobs inherit it. Low-level
// import APIs remain usable without a bank; ordinary game hosts always own one.
class ResourceBankScope {
public:
    explicit ResourceBankScope(ResourceBank&);
    ~ResourceBankScope();
    ResourceBankScope(const ResourceBankScope&)=delete;
    ResourceBankScope& operator=(const ResourceBankScope&)=delete;
private:
    ResourceBank* previous_;
};
ResourceBank* currentResourceBank();
std::string canonicalResourcePath(const std::filesystem::path&);
}
