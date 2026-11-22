#include "engine/assets/ResourceBank.h"
#include <algorithm>
#include <array>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>

namespace engine::assets {
namespace {
thread_local ResourceBank* currentBank=nullptr;
thread_local ResourceBankCore* workerCore=nullptr;
thread_local std::vector<std::shared_ptr<ResourceBankEntry>> loadingEntries;
std::uint64_t hashBytes(const std::uint8_t* bytes,std::size_t size,std::uint64_t hash=14695981039346656037ull) {
    for(std::size_t i=0;i<size;++i){hash^=bytes[i];hash*=1099511628211ull;}return hash;
}
bool terminal(ResourceState s){return s!=ResourceState::Queued&&s!=ResourceState::Loading;}
}
std::string canonicalResourcePath(const std::filesystem::path& p) {
    return std::filesystem::weakly_canonical(std::filesystem::absolute(p)).generic_string();
}
bool ResourceDependency::current() const {
    std::error_code error;
    if(std::filesystem::file_size(path,error)!=bytes||error)return false;
    std::ifstream in(path,std::ios::binary);if(!in)return false;
    std::array<std::uint8_t,65536> block{};std::uint64_t digest=14695981039346656037ull,count=0;
    while(in){in.read(reinterpret_cast<char*>(block.data()),block.size());const auto n=in.gcount();
        if(n>0){digest=hashBytes(block.data(),std::size_t(n),digest);count+=std::uint64_t(n);}}
    return in.eof()&&count==bytes&&digest==hash;
}
std::vector<std::uint8_t> ResourceLoadContext::read(const std::filesystem::path& path,std::uint64_t limit) {
    if(cancelled())throw std::runtime_error("Resource load cancelled");
    const auto canonical=canonicalResourcePath(path);
    std::ifstream in(canonical,std::ios::binary|std::ios::ate);
    if(!in)throw std::runtime_error("Cannot open resource: "+canonical);
    const auto n=in.tellg();
    if(n<0||std::uint64_t(n)>limit||std::uint64_t(n)>(1024ull<<20)-inputBytes_)
        throw std::runtime_error("Resource input byte budget exceeded: "+canonical);
    inputBytes_+=std::uint64_t(n);std::vector<std::uint8_t> bytes(static_cast<std::size_t>(n));
    in.seekg(0);if(!in.read(reinterpret_cast<char*>(bytes.data()),n))
        throw std::runtime_error("Short resource read: "+canonical);
    ResourceDependency dependency{canonical,std::uint64_t(bytes.size()),hashBytes(bytes.data(),bytes.size())};
    const auto found=std::find_if(dependencies_.begin(),dependencies_.end(),[&](const auto& d){return d.path==dependency.path;});
    if(found==dependencies_.end())dependencies_.push_back(std::move(dependency));
    else if(found->hash!=dependency.hash||found->bytes!=dependency.bytes)
        throw std::runtime_error("Resource changed during preparation: "+canonical);
    return bytes;
}
void ResourceLoadContext::depend(const std::filesystem::path& path,std::uint64_t limit){(void)read(path,limit);}
bool ResourceLoadContext::cancelled() const{return cancelled_&&cancelled_();}
ResourceFileStore::ResourceFileStore(ResourceLoadContext& c,std::filesystem::path root)
    :context_(c),root_(canonicalResourcePath(root)){}
std::filesystem::path ResourceFileStore::resolve(const std::string& name) const {
    const std::filesystem::path p(name);
    if(p.empty()||p.is_absolute()||p.has_root_name()||name.find(':')!=std::string::npos)
        throw std::runtime_error("Invalid virtual resource path");
    const std::filesystem::path result(canonicalResourcePath(root_/p));
    const auto relative=result.lexically_relative(root_);
    if(relative.empty()||*relative.begin()=="..")throw std::runtime_error("Resource path escapes root");
    return result;
}
bool ResourceFileStore::readBytes(const std::string& name,std::vector<std::uint8_t>& out,std::string* error) const {
    try{auto value=context_.read(resolve(name));out=std::move(value);return true;}
    catch(const std::exception& e){if(error)*error=e.what();return false;}
}
bool ResourceFileStore::readText(const std::string& n,std::string& out,std::string* e) const {
    std::vector<std::uint8_t> bytes;if(!readBytes(n,bytes,e))return false;out.assign(bytes.begin(),bytes.end());return true;
}
bool ResourceFileStore::exists(const std::string& n) const {
    try{return std::filesystem::is_regular_file(resolve(n));}catch(...){return false;}
}
struct ResourceBankEntry {
    std::string key,internalKey;
    std::weak_ptr<ResourceBankEntry> waitingOn;
    std::type_index type{typeid(void)};
    ResourceState state=ResourceState::Queued;
    std::shared_ptr<const void> value;
    std::vector<ResourceDependency> dependencies;
    std::function<std::pair<std::shared_ptr<const void>,std::uint64_t>(ResourceLoadContext&)> load;
    std::exception_ptr failure;
    std::uint64_t bytes=0,sequence=0;
    int priority=0;
    std::size_t subscribers=0;
    std::chrono::steady_clock::time_point used=std::chrono::steady_clock::now();
};
struct ResourceBankCore {
    ResourceBankOptions options;
    ResourceBank* bank=nullptr;
    mutable std::mutex mutex;
    std::condition_variable condition;
    std::unordered_map<std::string,std::shared_ptr<ResourceBankEntry>> entries;
    std::vector<std::shared_ptr<ResourceBankEntry>> queue;
    struct Retired {std::weak_ptr<const void> value;std::uint64_t bytes;};
    std::vector<Retired> retired;
    ResourceBankStats counters;
    bool stopping=false;
    std::uint64_t sequence=0;
    void refreshRetired(){
        std::erase_if(retired,[](const auto& r){return r.value.expired();});
        counters.retiredBytes=0;for(const auto& r:retired)counters.retiredBytes+=r.bytes;
    }
    void erase(const std::shared_ptr<ResourceBankEntry>& e){
        const auto it=entries.find(e->internalKey);
        if(it==entries.end()||it->second!=e)return;
        if(e->value){counters.residentBytes-=e->bytes;
            if(e->value.use_count()>1||e->subscribers>0)retired.push_back({e->value,e->bytes});}
        entries.erase(it);std::erase(queue,e);refreshRetired();
    }
    void trim(bool all,std::uint64_t needed=0){
        refreshRetired();
        const auto now=std::chrono::steady_clock::now();
        for(const auto& [key,e]:entries) {
            (void)key;
            if(e->state==ResourceState::Ready && (e->subscribers || e->value.use_count()>1))
                e->used=now;
        }
        for(;;) {
            std::shared_ptr<ResourceBankEntry> oldest;
            for(const auto& [key,e]:entries){
                (void)key;
                if(e->state==ResourceState::Ready && e->subscribers==0 && e->value.use_count()==1 &&
                   (!oldest || e->used<oldest->used))oldest=e;
            }
            if(!oldest)break;
            const auto used=counters.residentBytes+counters.retiredBytes;
            const bool pressure=needed>options.maxResidentBytes-std::min(used,options.maxResidentBytes);
            if(!all&&!pressure&&now-oldest->used<options.idleRetention)break;
            erase(oldest);++counters.evictions;
        }
    }
    void execute(const std::shared_ptr<ResourceBankEntry>& e){
        {
            std::lock_guard lock(mutex);
            if(e->state!=ResourceState::Queued)return;
            if(stopping||e->subscribers==0){
                e->state=ResourceState::Cancelled;erase(e);condition.notify_all();return;
            }
            e->state=ResourceState::Loading;++counters.loads;++counters.loading;
        }
        const auto started=std::chrono::steady_clock::now();
        std::shared_ptr<const void> value;std::uint64_t bytes=0;std::exception_ptr failure;
        ResourceLoadContext context;
        try{
            context.cancelled_=[this,e]{std::lock_guard lock(mutex);return stopping||e->subscribers==0||e->state==ResourceState::Cancelled;};
            ResourceBankScope scope(*bank);
            loadingEntries.push_back(e);
            struct Pop {~Pop(){loadingEntries.pop_back();}} pop;
            auto result=e->load(context);value=std::move(result.first);bytes=result.second;
            if(!value)throw std::runtime_error("Resource loader returned no value");
            for(const auto& dependency:context.dependencies())
                if(!dependency.current())throw std::runtime_error("Resource changed during load: "+dependency.path.string());
        }catch(...){failure=std::current_exception();}
        std::lock_guard lock(mutex);
        --counters.loading;counters.inputBytes+=context.inputBytes_;
        counters.loadMicroseconds+=std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now()-started).count();
        e->load={};
        if(stopping||e->subscribers==0||e->state==ResourceState::Cancelled){
            e->state=ResourceState::Cancelled;erase(e);
        }else if(failure){
            e->state=ResourceState::Failed;e->failure=failure;++counters.failures;erase(e);
        }else{
            trim(false,bytes);
            const auto used=counters.residentBytes+counters.retiredBytes;
            if(bytes>options.maxResidentBytes-std::min(used,options.maxResidentBytes)){
                try{throw std::runtime_error("Resource resident byte budget exceeded: "+e->key);}
                catch(...){e->failure=std::current_exception();}
                e->state=ResourceState::Failed;++counters.failures;erase(e);
            }else{
                e->value=std::move(value);e->bytes=bytes;e->dependencies=std::move(context.dependencies_);
                e->state=ResourceState::Ready;e->used=std::chrono::steady_clock::now();
                counters.residentBytes+=bytes;counters.loadedBytes+=bytes;
                counters.peakResidentBytes=std::max(counters.peakResidentBytes,counters.residentBytes+counters.retiredBytes);
            }
        }
        condition.notify_all();
    }
    void run(){
        workerCore=this;
        for(;;){
            std::shared_ptr<ResourceBankEntry> e;
            {std::unique_lock lock(mutex);condition.wait(lock,[&]{return stopping||!queue.empty();});
             if(stopping)return;
             auto it=std::max_element(queue.begin(),queue.end(),[](const auto& a,const auto& b){
                 return a->priority!=b->priority?a->priority<b->priority:a->sequence>b->sequence;});
             e=*it;queue.erase(it);}
            execute(e);
        }
    }
};
struct ResourceBank::Workers {std::vector<std::thread> threads;};
ResourceBank::ResourceBank(ResourceBankOptions options):core_(std::make_shared<ResourceBankCore>()),workers_(std::make_unique<Workers>()){
    if(!options.workers||options.workers>8||!options.maxRequests||options.maxRequests>4096||!options.maxResidentBytes||options.idleRetention.count()<0)
        throw std::invalid_argument("Invalid resource bank limits");
    core_->options=options;core_->bank=this;
    core_->retired.reserve(options.maxRequests);core_->queue.reserve(options.maxRequests);
    try{for(std::size_t i=0;i<options.workers;++i)workers_->threads.emplace_back([c=core_]{c->run();});}
    catch(...){stop();throw;}
}
ResourceBank::~ResourceBank(){stop();}
void ResourceBank::stop(){
    {std::lock_guard lock(core_->mutex);core_->stopping=true;
     for(auto& [key,e]:core_->entries){(void)key;if(!terminal(e->state))e->state=ResourceState::Cancelled;}
     core_->queue.clear();core_->condition.notify_all();}
    for(auto& t:workers_->threads)if(t.joinable())t.join();
    std::lock_guard lock(core_->mutex);
    core_->entries.clear();core_->retired.clear();core_->counters.residentBytes=core_->counters.retiredBytes=0;
}
void ResourceBank::trim(bool all){std::lock_guard lock(core_->mutex);core_->trim(all);}
void ResourceBank::invalidate(const std::string& key){
    std::lock_guard lock(core_->mutex);std::vector<std::shared_ptr<ResourceBankEntry>> entries;
    for(const auto& [k,e]:core_->entries){(void)k;if(e->key==key)entries.push_back(e);}
    for(const auto& e:entries){if(!terminal(e->state))e->state=ResourceState::Cancelled;core_->erase(e);}
    core_->condition.notify_all();
}
ResourceBankStats ResourceBank::stats() const {
    std::lock_guard lock(core_->mutex);core_->refreshRetired();auto out=core_->counters;
    out.entries=core_->entries.size();
    for(const auto& [key,e]:core_->entries){(void)key;out.queued+=e->state==ResourceState::Queued;}return out;
}
ResourceRequest ResourceBank::requestErased(std::string key,std::type_index type,Loader loader,int priority){
    if(key.empty()||key.size()>65536||!loader)throw std::invalid_argument("Resource request needs a key and loader");
    const auto internal=std::string(type.name())+"\n"+key;
    if(workerCore==core_.get()&&std::any_of(loadingEntries.begin(),loadingEntries.end(),[&](const auto& e){return e->internalKey==internal;}))
        throw std::runtime_error("Cyclic resource dependency: "+key);
    for(;;){
        std::shared_ptr<ResourceBankEntry> e;
        bool fresh=false;
        {
            std::lock_guard lock(core_->mutex);
            if(core_->stopping)throw std::runtime_error("Resource bank stopped");
            if(auto it=core_->entries.find(internal);it!=core_->entries.end())e=it->second;
            else{
                core_->trim(false);
                if(core_->entries.size()+core_->retired.size()>=core_->options.maxRequests){core_->trim(true);
                    if(core_->entries.size()+core_->retired.size()>=core_->options.maxRequests)throw std::runtime_error("Resource request capacity exceeded");}
                e=std::make_shared<ResourceBankEntry>();e->key=key;e->internalKey=internal;e->type=type;e->priority=priority;e->sequence=++core_->sequence;
                e->load=[fn=loader](ResourceLoadContext& c){auto v=fn(c);return std::make_pair(std::move(v.value),v.bytes);};
                core_->entries.emplace(internal,e);fresh=true;
            }
            ++e->subscribers;e->used=std::chrono::steady_clock::now();
            if(!fresh){++core_->counters.hits;e->priority=std::max(e->priority,priority);}
        }
        ResourceRequest request(core_,e);
        if(!fresh&&request.state()==ResourceState::Ready){
            bool valid=true;for(const auto& d:e->dependencies)if(!d.current()){valid=false;break;}
            if(!valid){std::lock_guard lock(core_->mutex);core_->erase(e);continue;}
        }
        if(fresh){
            // Even nested requests enter the queue. wait() records the parent
            // dependency before helping; executing here would hide that edge
            // from cross-worker cycle detection.
            std::lock_guard lock(core_->mutex);
            if(core_->stopping)e->state=ResourceState::Cancelled;
            else core_->queue.push_back(e);
            core_->condition.notify_all();
        }
        return request;
    }
}
ResourceRequest::ResourceRequest(std::shared_ptr<ResourceBankCore> c,std::shared_ptr<ResourceBankEntry> e)
    :core_(std::move(c)),entry_(std::move(e)),subscribed_(true){}
ResourceRequest::~ResourceRequest(){cancel();}
ResourceRequest::ResourceRequest(ResourceRequest&& other) noexcept
    :core_(std::move(other.core_)),entry_(std::move(other.entry_)),subscribed_(std::exchange(other.subscribed_,false)){}
ResourceRequest& ResourceRequest::operator=(ResourceRequest&& other) noexcept {
    if(this!=&other){cancel();core_=std::move(other.core_);entry_=std::move(other.entry_);subscribed_=std::exchange(other.subscribed_,false);}return *this;
}
ResourceState ResourceRequest::state() const {
    if(!core_)return ResourceState::Cancelled;
    std::lock_guard lock(core_->mutex);return subscribed_?entry_->state:ResourceState::Cancelled;
}
void ResourceRequest::cancel(){
    if(!core_)return;
    std::lock_guard lock(core_->mutex);if(!subscribed_)return;subscribed_=false;
    if(--entry_->subscribers==0&&!terminal(entry_->state)){
        entry_->state=ResourceState::Cancelled;++core_->counters.cancellations;core_->erase(entry_);
    }
    entry_.reset();
    core_->condition.notify_all();
}
std::shared_ptr<const void> ResourceRequest::wait(std::type_index type){
    if(!core_)throw std::runtime_error("Resource request cancelled");
    std::unique_lock lock(core_->mutex);
    if(!subscribed_)throw std::runtime_error("Resource request cancelled");
    if(type!=entry_->type)throw std::runtime_error("Resource type mismatch");
    std::shared_ptr<ResourceBankEntry> parent;
    if(workerCore==core_.get()&&!loadingEntries.empty()){
        parent=loadingEntries.back();
        // Entry identity survives cancellation and replacement of the same key.
        // Every edge is established under this mutex, keeping the graph acyclic.
        for(auto cursor=entry_;cursor;cursor=cursor->waitingOn.lock())
            if(cursor==parent)throw std::runtime_error("Cyclic resource dependency: "+entry_->key);
        parent->waitingOn=entry_;
    }
    struct Reset {std::shared_ptr<ResourceBankEntry> e;~Reset(){if(e)e->waitingOn.reset();}} reset{parent};
    // A worker awaiting a queued dependency performs that job itself. This
    // preserves progress even when every pool thread owns a parent request.
    if(workerCore==core_.get()&&entry_->state==ResourceState::Queued){
        auto pending=entry_;
        std::erase(core_->queue,pending);lock.unlock();core_->execute(pending);lock.lock();
    }
    core_->condition.wait(lock,[&]{return !subscribed_||terminal(entry_->state);});
    if(!subscribed_||entry_->state==ResourceState::Cancelled)throw std::runtime_error("Resource request cancelled");
    if(entry_->state==ResourceState::Failed)std::rethrow_exception(entry_->failure);
    return entry_->value;
}

ResourceBankScope::ResourceBankScope(ResourceBank& b):previous_(std::exchange(currentBank,&b)){}
ResourceBankScope::~ResourceBankScope(){currentBank=previous_;}
ResourceBank* currentResourceBank(){return currentBank;}
}
