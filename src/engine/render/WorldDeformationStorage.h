#pragma once
#include "engine/render/WorldDeformation.h"
#include "engine/render/WorldRecordingWorkers.h"
#include <atomic>
#include <cstddef>
#include <functional>
#include <optional>
#include <ostream>
#include <sstream>
#include <unordered_map>

namespace engine::render {
// Owner-thread registration and allocation. Worker lanes only copy disjoint
// mapped ranges. Every range is retired against its last submitted fence value.
class WorldDeformationStorage {
public:
    struct Stats {
        std::uint64_t requests=0,uploads=0,reuses=0,uploadBytes=0,reuseBytes=0,
            bypasses=0,failures=0,preparedUploads=0,preparedBytes=0,preparationJoins=0,
            persistentReuses=0,retiredBytes=0,evictions=0;
    };
    struct Result {std::uint64_t value;bool reused;};
    void configure(std::byte* mapped,std::size_t capacity,std::uint64_t base,std::size_t unit,
                   std::function<void()> flush={},std::size_t alignment=256) {
        if(mapped_==mapped && capacity_==capacity && base_==base && unit_==unit && alignment_==alignment)return;
        if(mapped_)throw std::logic_error("Replace deformation storage only after reset");
        if(!mapped || !unit || alignment<256 || (alignment&(alignment-1)) || alignment%unit || capacity<2*alignment)throw std::invalid_argument("Invalid deformation storage");
        mapped_=mapped;capacity_=capacity;base_=base;unit_=unit;alignment_=alignment;flush_=std::move(flush);
        free_.emplace(alignment,capacity-alignment);
    }
    void reset() {
        if(batch_==Batch::Collecting)throw std::logic_error("Reset during deformation admission");
        pending_.clear();resident_.clear();raw_.clear();transient_.clear();retired_.clear();free_.clear();
        mapped_=nullptr;capacity_=0;serial_=completed_=0;batch_=Batch::Idle;stats_={};flush_={};
    }
    void beginFrame(std::uint64_t serial,std::uint64_t completed) {
        if(!mapped_ || serial<=serial_ || completed<completed_ || completed>=serial)
            throw std::logic_error("Invalid deformation fence progression");
        if(batch_==Batch::Collecting)throw std::logic_error("Reset during deformation admission");
        for(auto& e:raw_)retire(e.range);
        for(auto r:transient_)retire(r);
        raw_.clear();transient_.clear();pending_.clear();
        serial_=serial;completed_=completed;batch_=Batch::Idle;stats_={};
        for(auto it=resident_.begin();it!=resident_.end();) {
            if(it->second.owner.expired()){retire(it->second.range);it=resident_.erase(it);}
            else ++it;
        }
        reclaim();
    }
    void beginBatch(){if(batch_!=Batch::Idle)throw std::logic_error("Deformation batch already active or aborted");batch_=Batch::Collecting;}
    void cancelBatch()noexcept{if(batch_==Batch::Collecting){pending_.clear();batch_=Batch::Aborted;}}
    void prepare(std::span<const WorldSkinPalette> inputs) {
        if(batch_==Batch::Aborted)throw std::logic_error("Deformation batch aborted");
        const auto count=pending_.size();
        try {
            for(const auto& p:inputs) {
                if(p.data.empty())continue;
                if(p.data.size()>128u*32u || p.data.size()%16u)throw std::invalid_argument("Invalid prepared skin palette");
                if(p.version) {
                    if(p.version->values().data()!=p.data.data() || p.version->values().size()!=p.data.size())
                        throw std::logic_error("Registered deformation does not own input");
                    if(auto* e=find(p.version)){e->range.used=serial_;++stats_.persistentReuses;continue;}
                    if(std::any_of(pending_.begin(),pending_.end(),[&](const auto& q){return q.version==p.version;}))continue;
                } else {
                    const auto bytes=std::as_bytes(p.data);
                    if(findRaw(bytes) || std::any_of(pending_.begin(),pending_.end(),[&](const auto& q){return equal(q.bytes,bytes);} ))continue;
                }
                if(resident_.size()+pending_.size()>=16384)throw std::runtime_error("Deformation resident entry budget exhausted");
                Pending next;next.version=p.version;
                if(!p.version){const auto b=std::as_bytes(p.data);next.bytes.assign(b.begin(),b.end());}
                pending_.push_back(std::move(next));
            }
        } catch(...) {pending_.resize(count);throw;}
        if(batch_==Batch::Idle) {
            try {publish();}catch(...){pending_.clear();throw;}
        }
    }
    void commitBatch(){if(batch_!=Batch::Collecting)throw std::logic_error("No deformation batch to commit");publish();batch_=Batch::Idle;}
    // Registered consumers must have been admitted. Missing/stale state cannot
    // silently become a legacy upload or bind another producer's allocation.
    std::optional<Result> resolve(std::span<const std::byte> bytes,WorldDeformation version={}) {
        requireReady();++stats_.requests;
        if(version) {
            if(bytes.size()!=version->values().size_bytes())throw std::logic_error("Registered deformation layout changed");
            auto* e=find(version);
            if(!e || e->range.used!=serial_)throw std::logic_error("Unadmitted registered deformation");
            ++stats_.reuses;stats_.reuseBytes+=bytes.size();return Result{value(e->range),true};
        }
        if(bytes.empty()){++stats_.failures;return {};}
        if(auto* e=findRaw(bytes)){++stats_.reuses;stats_.reuseBytes+=bytes.size();return Result{value(e->range),true};}
        raw_.reserve(raw_.size()+1);
        Raw e;e.bytes.assign(bytes.begin(),bytes.end());e.range=allocate(bytes.size());
        try {std::memcpy(mapped_+e.range.offset,bytes.data(),bytes.size());finishCopies();if(flush_)flush_();}
        catch(...){freeRange(e.range);throw;}
        const auto result=value(e.range);raw_.push_back(std::move(e));
        ++stats_.uploads;stats_.uploadBytes+=bytes.size();return Result{result,false};
    }
    std::uint64_t uploadTransient(std::span<const std::byte> bytes) {
        requireReady();if(bytes.empty())throw std::invalid_argument("Empty deformation auxiliary upload");
        const auto r=allocate(bytes.size());
        try {std::memcpy(mapped_+r.offset,bytes.data(),bytes.size());finishCopies();if(flush_)flush_();transient_.push_back(r);}
        catch(...){freeRange(r);throw;}
        return value(r);
    }
    const Stats& stats()const{return stats_;}
    std::size_t rawByteSize()const{std::size_t n=0;for(const auto& e:raw_)n+=e.bytes.size();return n;}
    std::size_t residentCount()const{return resident_.size();}
    std::size_t residentBytes()const{std::size_t n=0;for(const auto& [_,e]:resident_)n+=e.range.size;return n;}
    void print(std::ostream& out,const char* api,std::uint64_t frame)const {
        std::ostringstream s;s<<"[WorldPalettes] api="<<api<<" frame="<<frame
        <<" requests="<<stats_.requests<<" uploads="<<stats_.uploads<<" reuses="<<stats_.reuses
        <<" upload_bytes="<<stats_.uploadBytes<<" reuse_bytes="<<stats_.reuseBytes
        <<" bypasses="<<stats_.bypasses<<" failures="<<stats_.failures
        <<" prepared_uploads="<<stats_.preparedUploads<<" prepared_bytes="<<stats_.preparedBytes
        <<" preparation_joins="<<stats_.preparationJoins<<" persistent_reuses="<<stats_.persistentReuses
        <<" resident_count="<<residentCount()<<" resident_bytes="<<residentBytes()
        <<" retired_bytes="<<stats_.retiredBytes<<" evictions="<<stats_.evictions<<'\n';out<<s.str();
    }
private:
    enum class Batch {Idle,Collecting,Aborted};
    struct Range {std::size_t offset=0,size=0;std::uint64_t used=0;};
    struct Resident {WorldDeformationWeak owner;Range range;};
    struct Raw {std::vector<std::byte> bytes;Range range;};
    struct Pending {
        WorldDeformation version;std::vector<std::byte> bytes;Range range;
        std::span<const std::byte> data()const{return version?std::as_bytes(version->values()):std::span<const std::byte>(bytes);}
    };
    std::byte* mapped_=nullptr;std::size_t capacity_=0,unit_=1,alignment_=256;
    std::uint64_t base_=0,serial_=0,completed_=0;
    std::function<void()> flush_;
    Batch batch_=Batch::Idle;Stats stats_;
    std::map<std::size_t,std::size_t> free_;
    std::unordered_map<const WorldDeformationVersion*,Resident> resident_;
    std::vector<Raw> raw_;std::vector<Range> transient_,retired_;
    std::vector<Pending> pending_;std::unique_ptr<WorldRecordingWorkers> workers_;
    void requireReady()const{if(!serial_ || batch_!=Batch::Idle)throw std::logic_error("Deformation draw before successful batch commit");}
    static bool equal(std::span<const std::byte> a,std::span<const std::byte> b){return a.size()==b.size() && std::equal(a.begin(),a.end(),b.begin());}
    Resident* find(const WorldDeformation& v) {
        auto it=resident_.find(v.get());
        if(it==resident_.end())return nullptr;
        if(it->second.owner.lock()!=v){retire(it->second.range);resident_.erase(it);reclaim();return nullptr;}
        return &it->second;
    }
    Raw* findRaw(std::span<const std::byte> bytes){for(auto& e:raw_)if(equal(e.bytes,bytes))return &e;return nullptr;}
    std::uint64_t value(Range r)const{return base_+r.offset/unit_;}
    void retire(Range r){retired_.push_back(r);}
    void freeRange(Range r) {
        auto next=free_.lower_bound(r.offset);
        if(next!=free_.end() && r.offset+r.size==next->first){r.size+=next->second;next=free_.erase(next);}
        if(next!=free_.begin()){auto previous=std::prev(next);if(previous->first+previous->second==r.offset){r.offset=previous->first;r.size+=previous->second;free_.erase(previous);}}
        free_.emplace(r.offset,r.size);
    }
    void reclaim(){for(auto it=retired_.begin();it!=retired_.end();)if(it->used<=completed_){stats_.retiredBytes+=it->size;freeRange(*it);it=retired_.erase(it);}else ++it;}
    Range allocate(std::size_t bytes) {
        if(bytes>capacity_-alignment_)throw std::runtime_error("Deformation allocation exceeds storage");
        const auto size=(bytes+alignment_-1)&~(alignment_-1);
        for(unsigned attempt=0;attempt<2;++attempt) {
            for(auto it=free_.begin();it!=free_.end();++it)if(it->second>=size) {
                Range r{it->first,size,serial_};const auto remaining=it->second-size;free_.erase(it);
                if(remaining)free_.emplace(r.offset+size,remaining);return r;
            }
            // Only cold, completed allocations may be evicted. Admission acts
            // as KeepAlive for unchanged poses even when no draw follows.
            for(auto it=resident_.begin();it!=resident_.end();) {
                if(it->second.range.used<serial_ && it->second.range.used<=completed_) {
                    freeRange(it->second.range);it=resident_.erase(it);++stats_.evictions;
                } else ++it;
            }
        }
        ++stats_.failures;throw std::runtime_error("Deformation storage exhausted by live GPU ranges");
    }
    static void finishCopies() {
#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
        _mm_sfence();
#else
        std::atomic_thread_fence(std::memory_order_seq_cst);
#endif
    }
    void publish() {
        // Admission marks every unchanged active version first. Above the high
        // watermark, trim completed cold versions toward the low watermark.
        if(residentBytes()>capacity_*2/3) {
            std::vector<const WorldDeformationVersion*> cold;
            for(const auto& [key,e]:resident_)if(e.range.used<serial_ && e.range.used<=completed_)cold.push_back(key);
            std::sort(cold.begin(),cold.end(),[&](auto a,auto b){return resident_.at(a).range.used<resident_.at(b).range.used;});
            auto bytes=residentBytes();
            for(auto key:cold) {
                if(bytes<=capacity_/4)break;
                const auto r=resident_.at(key).range;bytes-=r.size;freeRange(r);resident_.erase(key);++stats_.evictions;
            }
        }
        if(pending_.empty())return;
        // Preallocate publication nodes before touching mapped memory, so a
        // failure can roll back all newly reserved ranges without partial ready state.
        std::unordered_map<const WorldDeformationVersion*,Resident> additions;
        additions.reserve(pending_.size());resident_.reserve(resident_.size()+pending_.size());
        raw_.reserve(raw_.size()+pending_.size());
        std::size_t reserved=0;
        try {
            for(auto& p:pending_) {
                p.range=allocate(p.data().size());++reserved;
                if(p.version)additions.emplace(p.version.get(),Resident{p.version,p.range});
            }
            const auto copy=[&](std::size_t i){const auto& p=pending_[i];const auto data=p.data();std::memcpy(mapped_+p.range.offset,data.data(),data.size());};
            if(pending_.size()>1) {
                if(!workers_)workers_=std::make_unique<WorldRecordingWorkers>();
                workers_->run([&](unsigned lane){for(std::size_t i=lane;i<pending_.size();i+=WorldRecordingWorkers::count)copy(i);finishCopies();});
            } else {copy(0);finishCopies();}
            if(flush_)flush_();
        } catch(...) {for(std::size_t i=0;i<reserved;++i)freeRange(pending_[i].range);throw;}
        resident_.merge(additions);
        for(auto& p:pending_) {
            stats_.preparedBytes+=p.data().size();
            if(!p.version)raw_.push_back({std::move(p.bytes),p.range});
        }
        stats_.preparedUploads+=pending_.size();stats_.preparationJoins+=pending_.size()>1;
        pending_.clear();
    }
};
}
