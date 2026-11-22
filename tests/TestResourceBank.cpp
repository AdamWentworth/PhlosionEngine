#include "engine/assets/ResourceBank.h"
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
using namespace engine::assets;
namespace {
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
template<class F> void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,"Expected rejection");}
struct Gate {
    std::mutex mutex;std::condition_variable cv;bool entered=false,released=false;
    void block(){std::unique_lock lock(mutex);entered=true;cv.notify_all();if(!cv.wait_for(lock,std::chrono::seconds(5),[&]{return released;}))throw std::runtime_error("Gate timeout");}
    void wait(){std::unique_lock lock(mutex);check(cv.wait_for(lock,std::chrono::seconds(5),[&]{return entered;}),"Gate enter timeout");}
    void release(){std::lock_guard lock(mutex);released=true;cv.notify_all();}
    ~Gate(){release();}
};
auto loader(int value,std::uint64_t bytes=16){
    return [=](ResourceLoadContext&){return ResourceValue<int>{std::make_shared<const int>(value),bytes};};
}
void identity(){
    ResourceBank b;std::atomic<int> count=0;
    auto load=[&](ResourceLoadContext&){++count;return ResourceValue<int>{std::make_shared<const int>(42),16};};
    auto a=b.request<int>("x",load);auto c=b.request<int>("x",load);
    auto av=a.get<int>(),cv=c.get<int>();check(av==cv&&*av==42&&count==1,"Requests did not join");
    rejects([&]{(void)a.get<double>();});
    a.cancel();c.cancel();b.trim(true);check(b.stats().residentBytes==16,"Pinned value evicted");
    const std::weak_ptr<const int> released=av;
    av.reset();cv.reset();b.trim(true);check(b.stats().residentBytes==0,"Unused payload retained");
    check(released.expired(),"Cancelled request handle retained an unaccounted payload");
    auto other=b.request<double>("x",[](ResourceLoadContext&){return ResourceValue<double>{std::make_shared<const double>(2.5),8};});
    check(*other.get<double>()==2.5,"Type identity failed");
    rejects([&]{b.acquire<int>("fail",[](ResourceLoadContext&)->ResourceValue<int>{throw std::runtime_error("expected");});});
    check(*b.acquire<int>("fail",loader(7))==7,"Failure could not retry");
}
void pressure(){
    ResourceBank b({1,8,32,std::chrono::milliseconds(30000)});
    auto a=b.acquire<int>("a",loader(1)),c=b.acquire<int>("c",loader(2));
    rejects([&]{b.acquire<int>("full",loader(3));});
    check(*a==1&&*c==2&&b.stats().residentBytes==32,"Pressure harmed consumers");
    a.reset();auto d=b.acquire<int>("d",loader(4));
    check(*d==4&&b.stats().residentBytes==32&&b.stats().evictions==1,"LRU admission failed");
    b.invalidate("c");check(*c==2&&b.stats().retiredBytes==16,"Retired live payload uncounted");
    rejects([&]{b.acquire<int>("c",loader(5));});
    c.reset();check(*b.acquire<int>("c",loader(5))==5,"Retired release did not recover");
}
void cancellation(){
    ResourceBank b({1,8,64,std::chrono::milliseconds(30000)});Gate gate;std::atomic<int> cancelledCalls=0;
    auto first=b.request<int>("blocked",[&](ResourceLoadContext&){gate.block();return ResourceValue<int>{std::make_shared<const int>(1),16};});
    gate.wait();
    for(int i=0;i<1000;++i){auto q=b.request<int>("queued",[&](ResourceLoadContext&){++cancelledCalls;return ResourceValue<int>{std::make_shared<const int>(0),16};});q.cancel();}
    auto joined=b.request<int>("blocked",loader(2));first.cancel();gate.release();
    check(*joined.get<int>()==1&&cancelledCalls==0,"Cancellation affected another subscriber or ran queued work");
    Gate active;auto all=b.request<int>("abandoned",[&](ResourceLoadContext& c){active.block();check(c.cancelled(),"Loader did not observe cancel");return ResourceValue<int>{std::make_shared<const int>(8),16};});
    active.wait();all.cancel();active.release();b.stop();
    rejects([&]{all.get<int>();});rejects([&]{b.acquire<int>("stopped",loader(0));});
}
void priorityAndNested(){
    ResourceBank b({1,8,128,std::chrono::milliseconds(30000)});Gate gate;std::vector<int> order;
    auto first=b.request<int>("gate",[&](ResourceLoadContext&){gate.block();return ResourceValue<int>{std::make_shared<const int>(1),16};});gate.wait();
    auto lo=b.request<int>("low",[&](ResourceLoadContext&){order.push_back(1);return ResourceValue<int>{std::make_shared<const int>(1),16};},0);
    auto hi=b.request<int>("high",[&](ResourceLoadContext&){order.push_back(2);return ResourceValue<int>{std::make_shared<const int>(2),16};},10);
    gate.release();first.get<int>();lo.get<int>();hi.get<int>();check(order==std::vector<int>({2,1}),"Priority/FIFO scheduler failed");
    check(*b.acquire<int>("parent",[&](ResourceLoadContext&){return ResourceValue<int>{b.acquire<int>("child",loader(9)),16};})==9,"Single-worker nested load deadlocked");
    rejects([&]{b.acquire<int>("cycle",[&](ResourceLoadContext&){return ResourceValue<int>{b.acquire<int>("cycle",loader(2)),16};});});
}
void files(){
    const auto root=std::filesystem::temp_directory_path()/("phlosion-resource-bank-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(root);
    struct Remove {std::filesystem::path root;~Remove(){std::error_code e;std::filesystem::remove_all(root,e);}} remove{root};
    const auto path=root/"data";{std::ofstream f(path);f<<"AAAA";}
    ResourceBank b;int loads=0;
    const auto load=[&](ResourceLoadContext& c){
        ++loads;ResourceFileStore store(c,root);std::string value,error;
        check(store.readText("data",value,&error),"Read failed");
        std::vector<std::uint8_t> escaped;check(!store.readBytes("../outside",escaped,&error),"Escaped file root");
        return ResourceValue<std::string>{std::make_shared<const std::string>(value),value.size()};
    };
    auto original=b.acquire<std::string>("document",load);
    check(original==b.acquire<std::string>("document",load)&&loads==1,"Unchanged file redecoded");
    const auto time=std::filesystem::last_write_time(path);
    {std::ofstream f(path);f<<"BBBB";}std::filesystem::last_write_time(path,time);
    auto changed=b.acquire<std::string>("document",load);
    check(*original=="AAAA"&&*changed=="BBBB"&&loads==2,"Same-size/time dependency change was missed");
    std::filesystem::remove(path);rejects([&]{b.acquire<std::string>("document",load);});
    check(*changed=="BBBB","Failed replacement invalidated live resource");
    ResourceLoadContext c;rejects([&]{c.read(root/"missing",1);});
}
void shutdownAndLimits(){
    ResourceBank b({1,2,64,std::chrono::milliseconds(0)});Gate gate;
    auto running=b.request<int>("active",[&](ResourceLoadContext& c){
        gate.block();check(c.cancelled(),"Shutdown did not cancel active job");
        return ResourceValue<int>{std::make_shared<const int>(1),16};
    });gate.wait();
    auto queued=b.request<int>("queued",loader(2));
    rejects([&]{b.request<int>("overflow",loader(3));});
    std::thread stopper([&]{b.stop();});
    for(unsigned i=0;i<1000 && running.state()!=ResourceState::Cancelled;++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    check(running.state()==ResourceState::Cancelled,"Stop did not close admission");gate.release();stopper.join();
    rejects([&]{running.get<int>();});rejects([&]{queued.get<int>();});
    check(!b.stats().loading&&!b.stats().residentBytes,"Shutdown retained worker payload");
    ResourceBank retired({1,2,64,std::chrono::milliseconds(0)});
    auto a=retired.acquire<int>("a",loader(1));retired.invalidate("a");
    auto c=retired.acquire<int>("c",loader(2));retired.invalidate("c");
    rejects([&]{retired.acquire<int>("metadata-full",loader(3));});
    a.reset();check(*retired.acquire<int>("replacement",loader(3))==3,"Retired metadata failed to recover");
    rejects([&]{retired.request<int>(std::string(65537,'x'),loader(0));});
}
void cancelledReplacement(){
    ResourceBank b({2,8,128,std::chrono::milliseconds(30000)});Gate gate;
    auto old=b.request<int>("same",[&](ResourceLoadContext& c)->ResourceValue<int>{
        gate.block();check(c.cancelled(),"Abandoned generation not cancelled");
        return {b.acquire<int>("child",loader(1)),16};
    });
    gate.wait();old.cancel();
    auto current=b.acquire<int>("same",loader(9));check(*current==9,"Replacement did not publish");
    gate.release();b.stop();
    rejects([&]{old.get<int>();});check(*current==9,"Abandoned loader damaged replacement");
}
void activeRetention(){
    ResourceBank b({1,8,128,std::chrono::milliseconds(50)});
    auto active=b.acquire<int>("live",loader(7));
    std::this_thread::sleep_for(std::chrono::milliseconds(60));b.trim();
    active.reset();b.trim();
    check(b.stats().residentBytes==16,"Recently active resource expired using original load time");
    std::this_thread::sleep_for(std::chrono::milliseconds(60));b.trim();
    check(b.stats().residentBytes==0,"Idle grace period did not expire");
}
void nestedCrossThreadCycle(){
    ResourceBank b({2,8,128,std::chrono::milliseconds(30000)});
    std::mutex m;std::condition_variable cv;int started=0;
    const auto ready=[&]{
        std::unique_lock lock(m);++started;cv.notify_all();
        check(cv.wait_for(lock,std::chrono::seconds(5),[&]{return started==2;}),"Nested parents did not start");
    };
    auto a=b.request<int>("a",[&](ResourceLoadContext&)->ResourceValue<int>{
        ready();
        return {b.acquire<int>("nested",[&](ResourceLoadContext&)->ResourceValue<int>{
            return {b.acquire<int>("b",loader(99)),16};
        }),16};
    });
    auto c=b.request<int>("b",[&](ResourceLoadContext&)->ResourceValue<int>{
        ready();return {b.acquire<int>("a",loader(99)),16};
    });
    rejects([&]{a.get<int>();});rejects([&]{c.get<int>();});
    ResourceBank expiry({1,8,128,std::chrono::milliseconds(0)});
    {auto value=expiry.acquire<int>("expired",loader(3));check(*value==3,"Expiry value");}
    expiry.trim();
    check(expiry.stats().residentBytes==0&&expiry.stats().evictions==1,"Idle cache did not expire");
}
void crossThreadCycle(){
    ResourceBank b({2,8,128,std::chrono::milliseconds(30000)});
    std::mutex m;std::condition_variable cv;int started=0;
    auto load=[&](const char* other){return [&,other](ResourceLoadContext&)->ResourceValue<int>{
        {std::unique_lock lock(m);++started;cv.notify_all();check(cv.wait_for(lock,std::chrono::seconds(5),[&]{return started==2;}),"Parents did not start");}
        return {b.acquire<int>(other,loader(99)),16};
    };};
    auto a=b.request<int>("a",load("b"));auto c=b.request<int>("b",load("a"));
    rejects([&]{a.get<int>();});rejects([&]{c.get<int>();});
}
}
int main(){
    try{identity();pressure();cancellation();priorityAndNested();files();crossThreadCycle();nestedCrossThreadCycle();shutdownAndLimits();activeRetention();cancelledReplacement();std::cout<<"Resource bank contracts passed\n";return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
