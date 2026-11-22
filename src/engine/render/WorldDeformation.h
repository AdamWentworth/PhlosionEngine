#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <utility>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <vector>

namespace engine::render {
// Pose ownership crosses unloadable model modules. Intrusive, non-virtual
// references keep no deleter/control-block function pointer in an unloaded DLL.
class WorldDeformation;
class WorldDeformationWeak;
class WorldDeformationVersion {
public:
    std::span<const float> values()const{return values_;}
private:
    friend class WorldDeformation;
    friend class WorldDeformationWeak;
    friend class WorldDeformationSet;
    explicit WorldDeformationVersion(std::span<const float> values):values_(values.begin(),values.end()){}
    std::atomic<std::uint32_t> strong_{1},weak_{1};
    std::vector<float> values_;
    void releaseWeak(){if(weak_.fetch_sub(1,std::memory_order_acq_rel)==1)delete this;}
    void release() {
        if(strong_.fetch_sub(1,std::memory_order_acq_rel)==1) {
            std::vector<float>().swap(values_);
            releaseWeak();
        }
    }
};
class WorldDeformation {
public:
    WorldDeformation()=default;
    WorldDeformation(const WorldDeformation& other):value_(other.value_){if(value_)value_->strong_.fetch_add(1,std::memory_order_relaxed);}
    WorldDeformation(WorldDeformation&& other)noexcept:value_(std::exchange(other.value_,nullptr)){}
    ~WorldDeformation(){reset();}
    WorldDeformation& operator=(WorldDeformation other)noexcept{std::swap(value_,other.value_);return *this;}
    void reset(){if(auto* value=std::exchange(value_,nullptr))value->release();}
    const WorldDeformationVersion* get()const{return value_;}
    const WorldDeformationVersion* operator->()const{return value_;}
    explicit operator bool()const{return value_!=nullptr;}
    friend bool operator==(const WorldDeformation&,const WorldDeformation&)=default;
private:
    friend class WorldDeformationSet;
    friend class WorldDeformationWeak;
    explicit WorldDeformation(WorldDeformationVersion* value):value_(value){}
    WorldDeformationVersion* value_=nullptr;
};
class WorldDeformationWeak {
public:
    WorldDeformationWeak()=default;
    WorldDeformationWeak(const WorldDeformation& value):value_(value.value_){retain();}
    WorldDeformationWeak(const WorldDeformationWeak& other):value_(other.value_){retain();}
    WorldDeformationWeak(WorldDeformationWeak&& other)noexcept:value_(std::exchange(other.value_,nullptr)){}
    ~WorldDeformationWeak(){reset();}
    WorldDeformationWeak& operator=(WorldDeformationWeak other)noexcept{std::swap(value_,other.value_);return *this;}
    void reset(){if(auto* value=std::exchange(value_,nullptr))value->releaseWeak();}
    bool expired()const{return !value_ || value_->strong_.load(std::memory_order_acquire)==0;}
    WorldDeformation lock()const {
        if(!value_)return {};
        auto count=value_->strong_.load(std::memory_order_relaxed);
        while(count)if(value_->strong_.compare_exchange_weak(count,count+1,
            std::memory_order_acquire,std::memory_order_relaxed))return WorldDeformation(value_);
        return {};
    }
private:
    void retain(){if(value_)value_->weak_.fetch_add(1,std::memory_order_relaxed);}
    WorldDeformationVersion* value_=nullptr;
};
// One registration per authored producer part. Four retained versions support
// current/previous and in-flight collections; external views may retain older ones.
class WorldDeformationSet {
public:
    WorldDeformation publish(std::uint64_t part,std::span<const float> values) {
        if(values.empty() || values.size()>128u*32u || values.size()%16u)
            throw std::invalid_argument("Invalid registered deformation layout");
        std::lock_guard lock(mutex_);
        auto it=parts_.find(part);
        if(it==parts_.end()) {
            if(parts_.size()==4096)throw std::runtime_error("Deformation registration budget exhausted");
            it=parts_.try_emplace(part).first;
        }
        auto& versions=it->second;
        for(const auto& v:versions)if(v && v->values().size()==values.size() &&
            std::memcmp(v->values().data(),values.data(),values.size_bytes())==0)return v;
        auto next=WorldDeformation(new WorldDeformationVersion(values));
        std::rotate(versions.rbegin(),versions.rbegin()+1,versions.rend());
        versions[0]=next;return next;
    }
    void release(std::uint64_t part){std::lock_guard lock(mutex_);parts_.erase(part);}
    void clear(){std::lock_guard lock(mutex_);parts_.clear();}
private:
    std::mutex mutex_;
    std::map<std::uint64_t,std::array<WorldDeformation,4>> parts_;
};
struct WorldSkinPalette {
    std::span<const float> data;
    WorldDeformation version;
    std::size_t size()const{return data.size();}
    float operator[](std::size_t index)const{return data[index];}
    WorldSkinPalette(std::span<const float> values):data(values){}
    WorldSkinPalette(const float* values,std::size_t count):data(values,count){}
    template<class T> WorldSkinPalette(const T& values):data(values){}
    WorldSkinPalette(WorldDeformation owned):data(owned?owned->values():std::span<const float>{}),version(std::move(owned)){}
};
}
