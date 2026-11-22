#include "engine/render/WorldFramePaletteCache.h"
#include "engine/render/vulkan/VulkanWorldDrawPacket.h"
#if defined(_WIN32)
#define NOMINMAX
#include "engine/render/d3d12/D3D12WorldDrawPacket.h"
#endif
#include <future>
#include <barrier>
#include "engine/render/WorldRecordingWorkers.h"
#include <iostream>
#include <stdexcept>
#include <vector>

void check(bool ok,const char* message) { if(!ok)throw std::runtime_error(message); }
template<class T> T handle(std::uintptr_t value) { return reinterpret_cast<T>(value); }
using namespace engine::render::vulkan_backend;

struct Trace {
    std::vector<std::uint64_t> events;
    std::array<std::uint64_t,12> boundDescriptors{};
    std::vector<std::array<std::uint64_t,12>> drawDescriptors;
    template<class... V> void add(std::uint64_t tag,V... values) { events.push_back(tag); (events.push_back(std::uint64_t(values)),...); }
    void vkCmdBindPipeline(VkCommandBuffer,VkPipelineBindPoint,VkPipeline value) {add(1,reinterpret_cast<std::uintptr_t>(value));}
    void vkCmdBindVertexBuffers(VkCommandBuffer,unsigned,unsigned,const VkBuffer* buffers,const VkDeviceSize* offsets) {add(2,reinterpret_cast<std::uintptr_t>(*buffers),*offsets);}
    void vkCmdBindIndexBuffer(VkCommandBuffer,VkBuffer buffer,VkDeviceSize offset,VkIndexType type) {add(3,reinterpret_cast<std::uintptr_t>(buffer),offset,type);}
    void vkCmdBindDescriptorSets(VkCommandBuffer,VkPipelineBindPoint,VkPipelineLayout,unsigned first,unsigned count,const VkDescriptorSet* sets,unsigned n,const unsigned* offsets) {
        check(first+count<=8,"descriptor range");
        const bool dynamic=first<=1 && first+count>1;
        check(n==(dynamic?4u:0u),"dynamic offsets attached to wrong descriptor range");
        for(unsigned i=0;i<count;++i)boundDescriptors[first+i]=reinterpret_cast<std::uintptr_t>(sets[i]);
        if(dynamic)for(unsigned i=0;i<4;++i)boundDescriptors[8+i]=offsets[i];
        add(4,first,count,n);
        for(unsigned i=0;i<count;++i)add(40,reinterpret_cast<std::uintptr_t>(sets[i]));
        for(unsigned i=0;i<n;++i)add(41,offsets[i]);
    }
    void vkCmdSetViewport(VkCommandBuffer,unsigned,unsigned,const VkViewport* v) {add(5,v->x,v->y,v->width,v->height);}
    void vkCmdSetScissor(VkCommandBuffer,unsigned,unsigned,const VkRect2D* v) {add(6,v->offset.x,v->offset.y,v->extent.width,v->extent.height);}
    void vkCmdPushConstants(VkCommandBuffer,VkPipelineLayout,VkShaderStageFlags,unsigned,unsigned size,const void* p) {
        check(size==sizeof(WorldPushConstants),"push size");
        const auto& v=*static_cast<const WorldPushConstants*>(p);add(7,v.materialMode,v.viewProjection[0],v.roughnessFactor);
    }
    void vkCmdDrawIndexed(VkCommandBuffer,unsigned count,unsigned instances,unsigned first,int base,unsigned) {add(8,count,instances,first,base);drawDescriptors.push_back(boundDescriptors);}
};
WorldIndexedDrawPacket packet() {
    WorldIndexedDrawPacket p;
    p.viewport={{0,0,640,480,0,1},{{0,0},{640,480}}};
    for(unsigned i=0;i<8;++i)p.bindings.sets[i]=handle<VkDescriptorSet>(10+i);
    p.bindings.offsets={256,512,768,0};p.vertices=handle<VkBuffer>(30);p.indices=handle<VkBuffer>(31);
    p.surfacePipeline=handle<VkPipeline>(40);p.outlinePipeline=handle<VkPipeline>(41);
    p.surface.materialMode=2;p.outline.materialMode=3;p.surface.viewProjection[0]=12;
    p.indexCount=36;p.instanceCount=4;p.firstIndex=8;p.baseVertex=9;
    return p;
}
std::vector<std::uint64_t> isolatedTrace(const WorldIndexedDrawPacket& p) {
    WorldCommandState state;WorldCommandCounters counters;
    WorldCommandRecorder<Trace> recorder{{},handle<VkCommandBuffer>(1),handle<VkPipelineLayout>(2),state,counters};
    recorder.record(p);
    check(counters.framePipelineBindCalls==2 && counters.frameVertexBufferBindCalls==1 &&
        counters.frameIndexBufferBindCalls==1 && counters.frameDescriptorBindCalls==1 &&
        counters.frameDescriptorBindSkips==0 && counters.frameViewportUpdates==1,"fresh context inherited cached bindings");
    return recorder.commands.events;
}
void testVulkan() {
    auto source=packet();const auto saved=source;const auto expected=isolatedTrace(saved);
    source.bindings.sets[0]=handle<VkDescriptorSet>(90);source.surface.materialMode=77;
    check(isolatedTrace(saved)==expected,"packet retained producer input references");
    std::vector<std::future<std::vector<std::uint64_t>>> jobs;
    for(unsigned i=0;i<4;++i)jobs.push_back(std::async(std::launch::async,[&]{return isolatedTrace(saved);}));
    for(auto& job:jobs)check(job.get()==expected,"concurrent recorder state leaked");
    WorldCommandState state;WorldCommandCounters counters;
    WorldCommandRecorder<Trace> recorder{{},handle<VkCommandBuffer>(1),handle<VkPipelineLayout>(2),state,counters};
    recorder.record(saved);recorder.record(saved);
    check(counters.frameVertexBufferBindCalls==1 && counters.frameVertexBufferBindSkips==1 &&
        counters.frameIndexBufferBindCalls==1 && counters.frameIndexBufferBindSkips==1 &&
        counters.frameDescriptorBindCalls==1 && counters.frameDescriptorBindSkips==1 &&
        counters.frameViewportUpdates==1 && counters.frameViewportSkips==1,"same context lost binding reuse");
    auto clipped=saved;clipped.viewport.scissor.offset.x=7;
    recorder.record(clipped);check(counters.frameViewportUpdates==2,"same-size clip change lost");
    clipped.viewport.viewport.x=16;recorder.record(clipped);
    check(counters.frameViewportUpdates==3,"atlas tile change lost");
    state={};recorder.record(saved);
    check(counters.frameVertexBufferBindCalls==2 && counters.frameIndexBufferBindCalls==2,"pass reset retained bindings");
    std::vector<unsigned> modes;
    // Inspect the actual emitted command stream for outline-before-surface ordering.
    for(std::size_t i=0;i+3<expected.size();++i)if(expected[i]==7 && (expected[i+1]==2 || expected[i+1]==3))modes.push_back(unsigned(expected[i+1]));
    check(modes==std::vector<unsigned>({3,2}),"outline/surface ordering changed");
    std::vector<WorldIndexedDrawPacket> sequence{saved,saved};
    for(unsigned i=0;i<8;++i){auto v=saved;v.bindings.sets[i]=handle<VkDescriptorSet>(120+i);sequence.push_back(v);}
    for(unsigned i=0;i<4;++i){auto v=saved;v.bindings.offsets[i]+=256;sequence.push_back(v);}
    sequence.push_back(saved);
    Trace reference;
    WorldCommandState cachedState;WorldCommandCounters cachedCounts;
    WorldCommandRecorder<Trace> cached{{},handle<VkCommandBuffer>(1),handle<VkPipelineLayout>(2),cachedState,cachedCounts};
    for(const auto& value:sequence) {
        WorldCommandState fresh;WorldCommandCounters counts;
        WorldCommandRecorder<Trace> ordinary{{},handle<VkCommandBuffer>(1),handle<VkPipelineLayout>(2),fresh,counts};
        ordinary.record(value);
        reference.drawDescriptors.insert(reference.drawDescriptors.end(),
            ordinary.commands.drawDescriptors.begin(),ordinary.commands.drawDescriptors.end());
        cached.record(value);
    }
    check(reference.drawDescriptors==cached.commands.drawDescriptors,
          "partial Vulkan binding changed effective draw descriptors");
    check(cached.descriptorReuses>0 && cached.descriptorWrites<8*sequence.size(),
          "Vulkan range failed to reuse unchanged descriptors");
}
#if defined(_WIN32)
struct D3DTrace {
    std::vector<std::uint64_t> events;
    std::array<std::uint64_t,192> bindings{};
    std::vector<std::array<std::uint64_t,192>> drawBindings;
    static std::uint32_t bits(float value){std::uint32_t out;std::memcpy(&out,&value,sizeof(out));return out;}
    template<class... V> void add(std::uint64_t tag,V... values) {events.push_back(tag);(events.push_back(std::uint64_t(values)),...);}
    void RSSetViewports(unsigned,const D3D12_VIEWPORT* v){
        bindings[109]=bits(v->TopLeftX);bindings[110]=bits(v->TopLeftY);
        bindings[111]=bits(v->Width);bindings[112]=bits(v->Height);
        bindings[113]=bits(v->MinDepth);bindings[114]=bits(v->MaxDepth);add(1,v->Width);}
    void RSSetScissorRects(unsigned,const D3D12_RECT* v){
        bindings[115]=v->left;bindings[116]=v->top;bindings[117]=v->right;bindings[118]=v->bottom;add(2,v->right);}
    void SetDescriptorHeaps(unsigned,ID3D12DescriptorHeap* const* heaps){
        const auto value=reinterpret_cast<std::uintptr_t>(*heaps);
        if(bindings[99]!=value)std::fill(bindings.begin()+140,bindings.begin()+175,0);
        bindings[99]=value;}
    void SetGraphicsRootSignature(ID3D12RootSignature* root){
        const auto value=reinterpret_cast<std::uintptr_t>(root);
        if(bindings[100]!=value){std::fill(bindings.begin(),bindings.begin()+99,0);std::fill(bindings.begin()+140,bindings.begin()+175,0);}
        bindings[100]=value;}
    void SetGraphicsRootConstantBufferView(unsigned root,D3D12_GPU_VIRTUAL_ADDRESS value){bindings[root]=value;add(3,root,value);}
    void SetGraphicsRootShaderResourceView(unsigned root,D3D12_GPU_VIRTUAL_ADDRESS value){bindings[25+root]=value;add(4,root,value);}
    void SetGraphicsRoot32BitConstants(unsigned,unsigned n,const void* p,unsigned){check(n==24,"planar size");for(unsigned i=0;i<n;++i)bindings[75+i]=bits(static_cast<const float*>(p)[i]);add(5,*static_cast<const float*>(p));}
    void SetGraphicsRootDescriptorTable(unsigned root,D3D12_GPU_DESCRIPTOR_HANDLE value){bindings[140+root]=value.ptr;add(6,root,value.ptr);}
    void IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY t){bindings[108]=t;}
    void IASetVertexBuffers(unsigned,unsigned,const D3D12_VERTEX_BUFFER_VIEW* v){bindings[102]=v->BufferLocation;bindings[103]=v->SizeInBytes;bindings[104]=v->StrideInBytes;add(7,v->BufferLocation);}
    void IASetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW* v){bindings[105]=v->BufferLocation;bindings[106]=v->SizeInBytes;bindings[107]=v->Format;add(8,v->BufferLocation);}
    void SetPipelineState(ID3D12PipelineState* p){bindings[101]=reinterpret_cast<std::uintptr_t>(p);add(9,bindings[101]);}
    void DrawIndexedInstanced(unsigned n,unsigned i,unsigned,int,unsigned){bindings[119]=n;bindings[120]=i;drawBindings.push_back(bindings);add(10,n,i);}
};
void testD3D() {
    engine::render::d3d12_internal::WorldIndexedDrawPacket p;
    p.surfacePipeline=handle<ID3D12PipelineState*>(200);p.outlinePipeline=handle<ID3D12PipelineState*>(201);
    p.surfaceConstants=300;p.outlineConstants=301;p.motion=302;p.indexCount=36;p.instanceCount=4;
    p.diffuse.planar=6;for(unsigned i=0;i<p.diffuse.tables.size();++i)p.diffuse.tables[i].ptr=400+i;
    const auto saved=p;
    auto run=[&] { D3DTrace trace;engine::render::d3d12_internal::recordWorldIndexedDraw(trace,saved);return trace.events; };
    auto expected=run();p.diffuse.planar=19;p.diffuse.tables[0].ptr=900;
    auto a=std::async(std::launch::async,run);auto b=std::async(std::launch::async,run);
    check(a.get()==expected && b.get()==expected,"D3D packet/context sharing");
    const std::vector<std::uint64_t> tail{3,1,301,9,201,10,36,4,3,1,300,9,200,10,36,4};
    check(expected.size()>=tail.size() && std::equal(tail.begin(),tail.end(),expected.end()-tail.size()),"D3D outline/surface order or constants");
    D3DTrace bundle;
    engine::render::d3d12_internal::recordWorldIndexedDraw(bundle,saved,true);
    check(bundle.events==std::vector<std::uint64_t>(expected.begin()+4,expected.end()),
          "bundle recorded forbidden viewport/scissor or lost draw bindings");
    p.outlinePipeline=nullptr;D3DTrace noOutline;
    engine::render::d3d12_internal::recordWorldIndexedDraw(noOutline,p);
    check(noOutline.events.size()==expected.size()-8,"disabled outline recorded");
    using Packet=engine::render::d3d12_internal::WorldIndexedDrawPacket;
    Packet complete=saved;
    complete.root=handle<ID3D12RootSignature*>(500);complete.heap=handle<ID3D12DescriptorHeap*>(501);
    complete.diffuse.constants=600;complete.object=601;complete.view=602;complete.skin=603;complete.instances=604;
    complete.vertices={700,128,32};complete.indices={800,72,DXGI_FORMAT_R32_UINT};
    complete.material.ptr=900;complete.displacement.ptr=901;
    complete.viewport={0,0,640,480,0,1};complete.scissor={0,0,640,480};
    std::vector<Packet> sequence{complete,complete};
    auto windChanged=complete;windChanged.diffuse.wind=1201;sequence.push_back(windChanged);
    auto layerChanged=complete;layerChanged.diffuse.layered=1301;layerChanged.diffuse.layeredTextures.ptr=1401;sequence.push_back(layerChanged);
    auto changed=complete;changed.root=handle<ID3D12RootSignature*>(502);sequence.push_back(changed);
    changed.heap=handle<ID3D12DescriptorHeap*>(503);sequence.push_back(changed);
    changed.object+=256;changed.motion+=256;changed.skin+=256;changed.view+=256;
    changed.diffuse.tables[17].ptr+=3;changed.diffuse.tables[18].ptr+=7;changed.diffuse.tables[19].ptr+=11;
    changed.diffuse.planar+=256;changed.diffuse.tables[3].ptr+=1;sequence.push_back(changed);
    changed.viewport.TopLeftX=20;changed.scissor.left=20;changed.vertices.StrideInBytes=40;
    changed.indices.Format=DXGI_FORMAT_R16_UINT;changed.outlinePipeline=nullptr;sequence.push_back(changed);
    sequence.push_back(complete);
    const auto cachedRun=[&] {
        D3DTrace trace;
        engine::render::d3d12_internal::WorldCommandRecorder<D3DTrace> cache(trace);
        for(const auto& value:sequence)engine::render::d3d12_internal::recordWorldIndexedDraw(cache,value);
        check(cache.reuses>60,"D3D command range did not reuse unchanged bindings");
        return trace.drawBindings;
    };
    D3DTrace ordinary;
    for(const auto& value:sequence)engine::render::d3d12_internal::recordWorldIndexedDraw(ordinary,value);
    check(cachedRun()==ordinary.drawBindings,"D3D binding reuse changed effective draw state");
    auto one=std::async(std::launch::async,cachedRun);auto two=std::async(std::launch::async,cachedRun);
    check(one.get()==ordinary.drawBindings && two.get()==ordinary.drawBindings,"D3D cached recording state crossed lanes");
}
#endif
void testRecordingReport() {
    struct CountingBuffer : std::stringbuf {
        unsigned writes=0;
        std::streamsize xsputn(const char* text,std::streamsize size) override {
            ++writes;return std::stringbuf::xsputn(text,size);
        }
    } buffer;
    std::ostream stream(&buffer);
    engine::render::WorldRecordingStats stats;
    stats.batches=14;stats.packets=717;stats.serialPackets=151;
    stats.overlappingBatches=7;stats.recordingMs=0.5;stats.overlapMs=0.2;
    stats.print(stream,"vulkan",42);
    check(buffer.writes==1,"per-frame diagnostic issued multiple pipe writes");
    check(buffer.str()=="[WorldRecording] api=vulkan frame=42 batches=14 jobs=28 packets=717 serial=151 overlapping_batches=7 recording_ms=0.5 overlap_ms=0.2 owner_claimed_batches=0 setup_ms=0 dispatch_ms=0 run_ms=0 join_wait_ms=0 helper_start_delay_ms=0 execute_ms=0 serial_batches=0 serial_ms=0 binding_writes=0 binding_reuses=0\n",
          "recording diagnostic fields changed");
}
void testWorkers() {
    using namespace engine::render;
    WorldRecordingWorkers workers;
    std::barrier rendezvous(2);
    std::array<std::thread::id,2> ids{};
    std::array<std::vector<unsigned>,2> ordered;
    for(unsigned frame=0;frame<12;++frame) {
        WorldRecordingWorkers::RunStats timing;
        auto times=workers.run([&](unsigned lane) {
            if(frame==0)ids[lane]=std::this_thread::get_id();
            check(ids[lane]==std::this_thread::get_id(),"recording lane changed thread");
            ordered[lane].clear();
            rendezvous.arrive_and_wait();
            for(unsigned i=128*lane;i<128*(lane+1);++i)ordered[lane].push_back(i);
        },&timing);
        check(!timing.ownerClaimed && timing.runMs>=timing.joinWaitMs && timing.dispatchMs>=0 && timing.helperStartDelayMs>=0,"Invalid joined recording timings");
        check(ids[0]!=ids[1] && ids[0]==std::this_thread::get_id(),"caller/helper did not use private recording lanes");
        WorldRecordingStats stats;stats.add(256,times);
        check(stats.overlappingBatches==1,"recording intervals failed to overlap");
        unsigned next=0;
        for(const auto& part:ordered)for(auto value:part)check(value==next++,"submission order changed");
    }
    std::array<std::atomic<unsigned>,2> completed{};
    for(unsigned i=0;i<2000;++i)workers.run([&](unsigned lane){++completed[lane];});
    check(completed[0]==2000 && completed[1]==2000,"claimed work was lost or duplicated");
    bool caught=false;unsigned finished=0;
    try { workers.run([&](unsigned lane) { if(!lane)throw std::runtime_error("expected");finished=1; }); }
    catch(const std::runtime_error&) { caught=true; }
    check(caught && finished==1,"failed job did not join all lanes");
    workers.run([&](unsigned lane) { ordered[lane]={lane}; });
    check(ordered[0][0]==0 && ordered[1][0]==1,"worker could not recover after joined error");
}
void testFramePalettes() {
    using engine::render::WorldFramePaletteCache;
    WorldFramePaletteCache cache(2,128);
    std::array<float,16> a{},b{},copy{};
    b[0]=1;
    std::uint64_t allocations=0;
    auto upload=[&]() -> std::optional<std::uint64_t> { return ++allocations; };
    auto get=[&](const auto& data) { return cache.resolve(std::as_bytes(std::span(data)),upload); };
    const auto first=get(a),second=get(b),again=get(copy);
    check(first && second && again && first->value!=second->value &&
          again->value==first->value && again->reused && allocations==2,"interleaved palette reuse");
    a[1]=2;const auto changed=get(a);
    check(changed && !changed->reused && changed->value!=first->value,"same address pose mutation");
    check(cache.size()==2 && cache.byteSize()==128 && cache.stats().bypasses==1,"bounded palette retention");
    const auto bypass=get(a);
    check(bypass && bypass->value!=changed->value && get(copy)->value==first->value,"full cache fallback or stale reuse");
    auto failure=cache.resolve(std::as_bytes(std::span(a)),[]() -> std::optional<std::uint64_t> {return {};});
    check(!failure && cache.stats().failures==1 && cache.size()==2,"failed upload publication");
    check(!cache.resolve({},upload),"empty palette accepted");
    cache.beginFrame();
    check(!cache.size() && !cache.byteSize() && !cache.stats().requests,"frame palette lifetime");
    const auto next=get(copy);check(next && !next->reused && next->value!=first->value,"stale frame upload");
    WorldFramePaletteCache noRetention(0,0);
    for(unsigned i=0;i<3;++i)check(bool(noRetention.resolve(std::as_bytes(std::span(a)),upload)),"uncached upload failed");
    check(!noRetention.size() && noRetention.stats().bypasses==3,"disabled retention");
}
void testPreparedPalettes() {
    using engine::render::WorldFramePaletteCache;
    WorldFramePaletteCache cache(3,192);
    std::array<float,16> a{},b{},same{};b[0]=2;
    const auto bytes=[](auto& data)->std::span<const std::byte>{return std::as_bytes(std::span(data));};
    std::array inputs{bytes(a),bytes(b),bytes(same)};
    std::array<std::byte,192> gpu{};std::size_t reserved=0;
    const auto reserve=[&](std::size_t count){
        auto offset=reserved;reserved+=count;
        // Producer memory may change after admission, before copies start.
        a[0]=9;
        return WorldFramePaletteCache::Reservation{offset,gpu.data()+offset};
    };
    bool flushed=false;
    cache.prepare(inputs,reserve,[&]{
        check(gpu[0]==std::byte{0} && std::memcmp(gpu.data()+64,b.data(),64)==0,
              "flush before joined owned copies");
        flushed=true;
    });
    check(flushed && reserved==128 && cache.size()==2 && cache.stats().preparedUploads==2 &&
        cache.stats().preparedBytes==128 && cache.stats().preparationJoins==1,"prepared duplicate coalescing");
    unsigned lazy=0;
    const auto upload=[&]()->std::optional<std::uint64_t>{++lazy;return 999;};
    check(cache.resolve(bytes(same),upload)->value==0 && cache.resolve(bytes(b),upload)->value==64 &&
        !lazy,"prepared palettes copied again while drawing");
    std::array next{bytes(a)};
    bool rejected=false;
    try {cache.prepare(next,[](std::size_t){return WorldFramePaletteCache::Reservation{};},[]{});}
    catch(const std::runtime_error&){rejected=true;}
    check(rejected && cache.size()==2,"failed reservation published a palette");
    rejected=false;
    try {cache.prepare(next,[&](std::size_t){return WorldFramePaletteCache::Reservation{128,gpu.data()+128};},
                       []{throw std::runtime_error("flush failure");});}
    catch(const std::runtime_error&){rejected=true;}
    check(rejected && cache.size()==2 && cache.stats().preparedUploads==2,"failed flush published data");
    cache.prepare(next,[&](std::size_t){return WorldFramePaletteCache::Reservation{128,gpu.data()+128};},[]{});
    check(cache.resolve(bytes(a),upload)->value==128 && !lazy,"failed upload could not retry");
    a[1]=1;rejected=false;
    try {cache.prepare(next,[&](std::size_t){++lazy;return WorldFramePaletteCache::Reservation{};},[]{});}
    catch(const std::runtime_error&){rejected=true;}
    check(rejected && !lazy,"retention overflow reserved partial GPU storage");
    cache.beginFrame();
    check(!cache.size() && !cache.stats().preparedUploads,"prepared palette escaped frame fence");
    check(!cache.resolve(bytes(same),upload)->reused && lazy==1,"stale prepared allocation reused");
}
void testFrameDeformationBatch() {
    using namespace engine::render;
    WorldFramePaletteCache cache(3,192);
    std::array<std::byte,512> gpu{};
    std::array<float,16> a{},b{},c{},d{};a[0]=1;b[0]=2;c[0]=3;d[0]=4;
    const auto bytes=[](auto& value)->std::span<const std::byte>{return std::as_bytes(std::span(value));};
    std::size_t cursor=0;unsigned reserves=0,flushes=0,lazy=0;
    const auto reserve=[&](std::size_t size) {
        ++reserves;auto at=cursor;cursor+=size;
        return WorldFramePaletteCache::Reservation{at,gpu.data()+at};
    };
    const auto flush=[&]{++flushes;};
    const auto upload=[&]()->std::optional<std::uint64_t>{++lazy;return 511;};
    const auto rejected=[&](auto fn,const char* reason) {
        bool failed=false;try{fn();}catch(const std::exception&){failed=true;}check(failed,reason);
    };
    cache.beginBatch();
    rejected([&]{cache.beginBatch();},"nested batch accepted");
    rejected([&]{cache.beginFrame();},"open batch was reset");
    std::array first{bytes(a),bytes(a)};
    cache.prepare(first,reserve,flush);
    auto saved=a;a[0]=9;
    std::array second{bytes(b),bytes(saved)};
    cache.prepare(second,reserve,flush);
    check(!reserves && !flushes && !cache.size(),"admission touched GPU storage");
    rejected([&]{cache.resolve(bytes(saved),upload);},"draw bypassed open batch");
    std::array overflow{bytes(c),bytes(d)};
    rejected([&]{cache.prepare(overflow,reserve,flush);},"unique pending budget ignored");
    std::array third{bytes(c)};
    cache.prepare(third,reserve,flush); // Failed producer added no partial data.
    rejected([&]{cache.commitBatch([](std::size_t){return WorldFramePaletteCache::Reservation{};},flush);},
             "failed batch reservation accepted");
    check(!cache.size() && !cache.stats().preparedUploads,"failed reserve published data");
    cursor=0;
    rejected([&]{cache.commitBatch(reserve,[]{throw std::runtime_error("flush");});},"failed flush accepted");
    check(!cache.size() && !cache.stats().preparedUploads,"failed flush published data");
    rejected([&]{cache.resolve(bytes(saved),upload);},"failed commit fell back to lazy draw");
    cursor=0;
    cache.commitBatch(reserve,flush);
    check(cache.size()==3 && cache.stats().preparedUploads==3 && cursor==192 && flushes==1,
          "multi-producer commit lost/coalesced incorrect inputs");
    check(std::memcmp(gpu.data(),saved.data(),64)==0 && std::memcmp(gpu.data()+64,b.data(),64)==0 &&
          std::memcmp(gpu.data()+128,c.data(),64)==0,"producer mutation corrupted owned batch");
    check(cache.resolve(bytes(saved),upload)->value==0 && cache.resolve(bytes(c),upload)->value==128 && !lazy,
          "committed batch copied at draw");
    rejected([&]{cache.commitBatch(reserve,flush);},"duplicate commit accepted");
    cache.beginBatch();cache.cancelBatch();
    rejected([&]{cache.resolve(bytes(saved),upload);},"cancel permitted ready or immediate consumption");
    rejected([&]{cache.prepare(first,reserve,flush);},"canceled batch permitted preparation");
    rejected([&]{cache.beginBatch();},"canceled frame silently restarted");
    cache.beginFrame();
    cache.beginBatch();cursor=0;reserves=flushes=0;
    cache.prepare(second,reserve,flush);cache.prepare(third,reserve,flush);
    cache.commitBatch(reserve,flush);
    check(reserves==3 && flushes==1 && cache.stats().preparationJoins==1,"producers did not share one join");
    cache.beginBatch();cache.prepare(third,reserve,flush);cache.commitBatch(reserve,flush);
    check(reserves==3 && flushes==1,"ready palette reallocated in new batch");
    cache.beginFrame();cache.beginBatch();cache.commitBatch(reserve,flush);
    check(!cache.size() && !cache.stats().preparedUploads,"empty batch made allocations");
}
int main() try {
    testFrameDeformationBatch();
    testPreparedPalettes();
    testFramePalettes();
    testRecordingReport();
    testWorkers();
    testVulkan();
#if defined(_WIN32)
    testD3D();
#endif
    std::cout<<"PASS isolated native recording: frozen bindings, private caches, concurrent traces, outlines, clipping, pass resets\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
