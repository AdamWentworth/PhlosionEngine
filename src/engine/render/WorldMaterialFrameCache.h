#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <sstream>
#include <unordered_map>
#include <vector>
namespace engine::render {
// Frame-owned native material constants, shared across objects and command
// lanes. Backing allocations remain append-only until the native frame fence.
class WorldMaterialFrameCache {
public:
    struct Stats {std::uint64_t requests=0,builds=0,reuses=0,bytes=0,reusedBytes=0,bypasses=0;};
    void beginFrame(){entries_.clear();lookup_.clear();retainedBytes_=0;recent_=~std::size_t(0);stats={};}
    template<class Build> std::optional<std::uint64_t> resolve(std::span<const std::byte> data,Build&& build) {
        ++stats.requests;
        const auto equal=[&](const Entry& e){return e.bytes.size()==data.size() && std::memcmp(e.bytes.data(),data.data(),data.size())==0;};
        const auto hit=[&](std::size_t i){recent_=i;++stats.reuses;stats.reusedBytes+=data.size();return entries_[i].value;};
        if(recent_<entries_.size() && equal(entries_[recent_]))return hit(recent_);
        std::size_t hash=14695981039346656037ull;
        for(auto b:data){hash^=std::to_integer<unsigned char>(b);hash*=1099511628211ull;}
        const auto [first,last]=lookup_.equal_range(hash);
        for(auto i=first;i!=last;++i)if(equal(entries_[i->second]))return hit(i->second);
        const auto result=build();if(!result)return {};
        ++stats.builds;stats.bytes+=data.size();
        if(entries_.size()<4096 && data.size()<=8u*1024u*1024u-retainedBytes_) {
            entries_.push_back({{data.begin(),data.end()},*result});
            recent_=entries_.size()-1;lookup_.emplace(hash,recent_);retainedBytes_+=data.size();
        } else ++stats.bypasses;
        return result;
    }
    void print(std::ostream& out,const char* api,std::uint64_t frame)const {
        std::ostringstream s;s<<"[WorldMaterials] api="<<api<<" frame="<<frame
            <<" requests="<<stats.requests<<" builds="<<stats.builds<<" reuses="<<stats.reuses
            <<" upload_bytes="<<stats.bytes<<" reuse_bytes="<<stats.reusedBytes<<" bypasses="<<stats.bypasses<<'\n';out<<s.str();
    }
    Stats stats;
private:
    struct Entry{std::vector<std::byte> bytes;std::uint64_t value;};
    std::vector<Entry> entries_;
    std::unordered_multimap<std::size_t,std::size_t> lookup_;
    std::size_t retainedBytes_=0,recent_=~std::size_t(0);
};
}
