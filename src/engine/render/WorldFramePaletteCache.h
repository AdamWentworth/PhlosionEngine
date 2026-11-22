#pragma once
#include "engine/render/WorldRecordingWorkers.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <ostream>
#include <span>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace engine::render {
// Owner clears after its frame fence. Values refer only to that frame's upload
// allocation; comparisons use ordinary RAM, never write-combined GPU memory.
class WorldFramePaletteCache {
public:
    struct Result { std::uint64_t value; bool reused; };
    struct Stats {
        std::uint64_t requests=0, uploads=0, reuses=0, uploadBytes=0, reuseBytes=0,
                      bypasses=0, failures=0, preparedUploads=0, preparedBytes=0, preparationJoins=0;
    };
    explicit WorldFramePaletteCache(std::size_t maxEntries=256,
                                    std::size_t maxBytes=8u*1024u*1024u)
        : maxEntries_(maxEntries), maxBytes_(maxBytes) {}
    void beginFrame() {
        if(batch_==Batch::Collecting)throw std::logic_error("Reset during deformation admission");
        pending_.clear();pendingBytes_=0;batch_=Batch::Idle;
        entries_.clear(); bytes_.clear(); recent_=invalid; stats_={};
    }
    const Stats& stats() const { return stats_; }
    std::size_t size() const { return entries_.size(); }
    std::size_t byteSize() const { return bytes_.size(); }

    template<class Upload>
    std::optional<Result> resolve(std::span<const std::byte> data, Upload&& upload) {
        if(batch_!=Batch::Idle)throw std::logic_error("Deformation draw before successful batch commit");
        ++stats_.requests;
        if(data.empty()) { ++stats_.failures; return {}; }
        const auto equal=[&](const Entry& e) {
            return e.size==data.size() &&
                   std::memcmp(bytes_.data()+e.offset,data.data(),data.size())==0;
        };
        const auto hit=[&](std::size_t i) {
            recent_=i; ++stats_.reuses; stats_.reuseBytes+=data.size();
            return Result{entries_[i].value,true};
        };
        // Consecutive submeshes normally share a palette: avoid hashing it again.
        if(recent_!=invalid && equal(entries_[recent_])) return hit(recent_);
        std::uint64_t hash=14695981039346656037ull;
        for(auto value:data) { hash^=std::to_integer<unsigned char>(value); hash*=1099511628211ull; }
        for(std::size_t i=0;i<entries_.size();++i)
            if(i!=recent_ && entries_[i].hash==hash && equal(entries_[i])) return hit(i);
        const std::optional<std::uint64_t> value=upload();
        if(!value) { ++stats_.failures; return {}; }
        ++stats_.uploads; stats_.uploadBytes+=data.size();
        if(entries_.size()<maxEntries_ && data.size()<=maxBytes_-bytes_.size()) {
            const auto offset=bytes_.size();
            bytes_.insert(bytes_.end(),data.begin(),data.end());
            entries_.push_back({hash,*value,offset,data.size()});
            recent_=entries_.size()-1;
        } else {
            // Bound CPU retention only. Never omit a draw or reuse stale data.
            ++stats_.bypasses;
        }
        return Result{*value,false};
    }
    struct Reservation { std::uint64_t value=0; std::byte* destination=nullptr; };
    void beginBatch() {
        if(batch_!=Batch::Idle)throw std::logic_error("Deformation batch already active or aborted");
        batch_=Batch::Collecting;
    }
    void cancelBatch() noexcept {
        if(batch_==Batch::Collecting) {
            pending_.clear();pendingBytes_=0;batch_=Batch::Aborted;
        }
    }
    template<class Reserve,class Flush>
    void commitBatch(Reserve&& reserve,Flush&& flush) {
        if(batch_!=Batch::Collecting)throw std::logic_error("No deformation batch to commit");
        flushPending(reserve,flush);
        batch_=Batch::Idle;
    }
    // A frame batch snapshots each producer without reserving GPU storage.
    // Outside a batch, standalone previews retain synchronous preparation.
    template<class Reserve,class Flush>
    void prepare(std::span<const std::span<const std::byte>> inputs,Reserve&& reserve,Flush&& flush) {
        if(batch_==Batch::Aborted)throw std::logic_error("Deformation batch aborted");
        enqueue(inputs);
        if(batch_==Batch::Collecting)return;
        try {flushPending(reserve,flush);}
        catch(...) {pending_.clear();pendingBytes_=0;throw;}
    }
    void print(std::ostream& out,const char* api,std::uint64_t frame) const {
        std::ostringstream line;
        line<<"[WorldPalettes] api="<<api<<" frame="<<frame
            <<" requests="<<stats_.requests<<" uploads="<<stats_.uploads
            <<" reuses="<<stats_.reuses<<" upload_bytes="<<stats_.uploadBytes
            <<" reuse_bytes="<<stats_.reuseBytes<<" bypasses="<<stats_.bypasses
            <<" failures="<<stats_.failures<<" prepared_uploads="<<stats_.preparedUploads
            <<" prepared_bytes="<<stats_.preparedBytes<<" preparation_joins="<<stats_.preparationJoins<<'\n';
        out<<line.str();
    }
private:
    enum class Batch { Idle, Collecting, Aborted };
    struct Pending { std::vector<std::byte> bytes; std::uint64_t hash; Reservation gpu; };
    Batch batch_=Batch::Idle;
    std::vector<Pending> pending_;
    std::size_t pendingBytes_=0;
    void enqueue(std::span<const std::span<const std::byte>> inputs) {
        const auto count=pending_.size(),byteCount=pendingBytes_;
        try {
            for(const auto data:inputs) {
                if(data.empty())continue;
                const auto matches=[&](const Entry& e) {
                    return e.size==data.size() && std::memcmp(bytes_.data()+e.offset,data.data(),data.size())==0;
                };
                if(std::any_of(entries_.begin(),entries_.end(),matches))continue;
                if(std::any_of(pending_.begin(),pending_.end(),[&](const auto& p){
                    return p.bytes.size()==data.size() && std::memcmp(p.bytes.data(),data.data(),data.size())==0;
                }))continue;
                if(entries_.size()+pending_.size()>=maxEntries_ ||
                   data.size()>maxBytes_-bytes_.size()-pendingBytes_)
                    throw std::runtime_error("Prepared deformation retention budget exhausted");
                std::uint64_t hash=14695981039346656037ull;
                for(auto value:data){hash^=std::to_integer<unsigned char>(value);hash*=1099511628211ull;}
                pending_.push_back({{data.begin(),data.end()},hash,{}});pendingBytes_+=data.size();
            }
        } catch(...) {pending_.resize(count);pendingBytes_=byteCount;throw;}
    }
    template<class Reserve,class Flush>
    void flushPending(Reserve&& reserve,Flush&& flush) {
        if(pending_.empty())return;
        // Allocate publication storage before touching the upload arena.
        bytes_.reserve(bytes_.size()+pendingBytes_);entries_.reserve(entries_.size()+pending_.size());
        for(auto& p:pending_) {
            p.gpu=reserve(p.bytes.size());
            if(!p.gpu.destination)throw std::runtime_error("Prepared deformation upload budget exhausted");
        }
        const auto copy=[&](std::size_t i) {
            const auto& p=pending_[i];std::memcpy(p.gpu.destination,p.bytes.data(),p.bytes.size());
        };
        // Mapped upload heaps can be write-combined. Drain stores on each
        // copying thread before its ordinary-RAM completion flag is published.
        const auto finishCopies=[] {
#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
            _mm_sfence();
#else
            std::atomic_thread_fence(std::memory_order_seq_cst);
#endif
        };
        if(pending_.size()>1) {
            if(!workers_)workers_=std::make_unique<WorldRecordingWorkers>();
            workers_->run([&](unsigned lane){
                for(std::size_t i=lane;i<pending_.size();i+=WorldRecordingWorkers::count)copy(i);
                finishCopies();
            });
            ++stats_.preparationJoins;
        } else { copy(0);finishCopies(); }
        flush(); // Non-coherent memory flush is owner-thread work after the join.
        for(const auto& p:pending_) {
            const auto offset=bytes_.size();
            bytes_.insert(bytes_.end(),p.bytes.begin(),p.bytes.end());
            entries_.push_back({p.hash,p.gpu.value,offset,p.bytes.size()});
        }
        stats_.preparedUploads+=pending_.size();stats_.preparedBytes+=pendingBytes_;
        pending_.clear();pendingBytes_=0;
    }
    struct Entry { std::uint64_t hash,value; std::size_t offset,size; };
    static constexpr auto invalid=std::numeric_limits<std::size_t>::max();
    std::size_t maxEntries_,maxBytes_,recent_=invalid;
    std::vector<Entry> entries_;
    std::vector<std::byte> bytes_;
    Stats stats_;
    std::unique_ptr<WorldRecordingWorkers> workers_;
};
} // namespace engine::render
