#include "engine/render/ShaderCacheStorage.h"
#include "engine/render/PreparationBatch.h"
#include <iostream>
#ifdef _WIN32
#include "engine/render/d3d12/D3D12RenderBackendPipelineCompile.h"
#endif
#include <set>
#include <algorithm>
#include <mutex>
#include <thread>
#include <stdexcept>
#include <string_view>
using namespace engine::render;
using namespace engine::render::shader_cache;
void check(bool ok,const char* what){if(!ok)throw std::runtime_error(what);}
int main() try {
    const std::vector<std::uint8_t> payload{0,1,2,127,255};
    auto file=encode(1,42,payload);
    check(std::ranges::equal(decode(file,1,42,100),payload),"Cache payload changed");
    for(std::size_t n=0;n<file.size();++n)check(decode(std::span(file).first(n),1,42,100).empty(),"Partial write accepted");
    check(decode(file,2,42,100).empty(),"Wrong cache kind accepted");
    check(decode(file,1,43,100).empty(),"Stale shader key accepted");
    check(decode(file,1,42,4).empty(),"Oversized cache accepted");
    for(std::size_t i=0;i<file.size();++i){auto bad=file;bad[i]^=1;check(decode(bad,1,42,100).empty(),"Corrupt cache accepted");}
    Key a,b;a.text("a");a.text("bc");b.text("ab");b.text("c");check(a.value!=b.value,"Ambiguous component boundaries");
    const auto root=std::filesystem::temp_directory_path()/("phlosion-shader-test-"+name(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    struct Cleanup{std::filesystem::path p;~Cleanup(){std::error_code e;std::filesystem::remove_all(p,e);}}cleanup{root};
    const auto path=root/"nested/entry";
    check(write(path,1,42,payload),"Atomic write failed");
    check(read(path,1,42,100)==payload,"Stored cache read failed");
    const std::vector<std::uint8_t> newer{9,8,7};check(write(path,1,42,newer),"Atomic replace failed");
    check(read(path,1,42,100)==newer,"Old cache survived replacement");
    check(!write(path/"impossible",1,42,payload),"Invalid cache directory accepted");
    check(read(root/"missing",1,42,100).empty(),"Missing cache did not fall back");
    PreparationBatch concurrent;
    for(int i=0;i<32;++i)concurrent.add([&]{check(write(path,1,42,payload),"Concurrent atomic writer failed");});
    concurrent.run(4);check(read(path,1,42,100)==payload,"Concurrent write torn");
    for(const auto& entry:std::filesystem::directory_iterator(path.parent_path()))check(entry.path()==path,"Temporary cache file leaked");
    std::atomic_int active{0},peak{0},finished{0};
    PreparationBatch batch;
    for(int i=0;i<12;++i)batch.add([&,i]{
        int n=++active,old=peak.load();while(n>old&&!peak.compare_exchange_weak(old,n)){}
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
        --active;++finished;if(i==2)throw std::runtime_error("expected preparation failure");
    });
    bool failed=false;try{batch.run(3);}catch(const std::runtime_error&){failed=true;}
    check(failed&&finished==12&&active==0&&peak<=3&&peak>1,"Failure escaped without bounded join");
    batch.add([&]{++finished;});batch.run(1);check(finished==13,"Batch could not be reused after joined failure");
    {
        bool joined=false;int polls=0;
        try {
            PreparationProgressScope progress([&](std::string_view label){
                if(!label.empty() && ++polls==2)throw PreparationCancelled{};
            });
            prepareWithProgress("test",[&]{std::this_thread::sleep_for(std::chrono::milliseconds(80));joined=true;return 0;});
            throw std::runtime_error("Cancelled preparation returned normally");
        }catch(const PreparationCancelled&){}
        check(joined&&!preparationProgress,"Cancellation escaped live compiler work or retained callback");
    }
#ifdef _WIN32
    {
        struct Environment {
            const char* key;std::string previous;
            Environment(const char* name,const std::string& value):key(name),previous(engine::env::get(name).value_or("")) {
                check(_putenv_s(key,value.c_str())==0,"Cannot isolate compiler cache test");
            }
            ~Environment(){_putenv_s(key,previous.c_str());}
        };
        Environment cache("PHLOSION_SHADER_CACHE_DIR",root.string());
        Environment prepared("PHLOSION_PREPARED_SHADER_DIR",(root/"no-package").string());
        Environment strict("PHLOSION_REQUIRE_PREPARED_SHADERS","");
        Environment recipes("PHLOSION_SHADER_RECIPE_DIR","");
        const std::string source="float4 main(float4 p:POSITION):SV_Position{return p;}";
        Microsoft::WRL::ComPtr<ID3DBlob> code,error;
        using namespace engine::render::d3d12_pipeline_compile;
        check(compileHlslWithCache(source.data(),source.size(),"main","vs_5_0",0,0,code,error),"CPU HLSL compile failed");
        std::array<wchar_t,32768> modulePath{};
        check(GetModuleFileNameW(GetModuleHandleW(L"dxcompiler.dll"),modulePath.data(),static_cast<DWORD>(modulePath.size()))!=0,"DXC DLL missing");
        Key compilerKey;compilerKey.text("DXC-SM6-HV2016-Gec-v1");
        for(const auto* compilerFile:{"dxcompiler.dll","dxil.dll"}) {
            std::ifstream dll(std::filesystem::path(modulePath.data()).parent_path()/compilerFile,std::ios::binary);
            std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(dll)),{});
            check(!bytes.empty(),"Compiler DLL unreadable");
            compilerKey.text(compilerFile);compilerKey.bytes(bytes.data(),bytes.size());
        }
        const auto key=shaderRequestKey(source.data(),source.size(),"main","vs_5_0",0,0);
        check(key!=shaderRequestKey(source.data(),source.size(),"main","vs_5_0",1,0),"Compiler flags missing from shader identity");
        const auto saved=read(root/"d3d12"/name(compilerKey.value)/(name(key)+".shader"),1,key,maxShaderBytes);
        check(saved.size()==code->GetBufferSize(),"Cache did not identify DXC and validator binaries");
        check(std::memcmp(saved.data(),code->GetBufferPointer(),saved.size())==0,"Compiled cache bytes changed");
        // Exercise the actual compiler, including its DXIL output and error path.
        const std::string_view object(static_cast<const char*>(code->GetBufferPointer()), code->GetBufferSize());
        check(object.find("DXIL") != std::string_view::npos,
            "Compiler did not produce Shader Model 6 DXIL");
        check(!compileHlslWithCache(source.data(),source.size(),"main","vs_5_0",0,1,code,error)&&error,
            "Unsupported compatibility flags silently accepted");
        const std::string broken="float4 main(:";
        check(!compileHlslWithCache(broken.data(),broken.size(),"main","vs_5_0",0,0,code,error)&&error&&!code,
            "Invalid shader source returned stale bytecode");
        const UINT debugFlags=D3DCOMPILE_DEBUG|D3DCOMPILE_SKIP_OPTIMIZATION|D3DCOMPILE_PACK_MATRIX_ROW_MAJOR;
        check(compileHlslWithCache(source.data(),source.size(),"main","vs_5_0",debugFlags,0,code,error),
            "Debug symbols/unoptimized compiler mapping failed");
        check(compileHlslWithCache(source.data(),source.size(),"main","vs_5_0",
                D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_WARNINGS_ARE_ERRORS|D3DCOMPILE_IEEE_STRICTNESS,0,code,error),
            "Strict/warning/IEEE compatibility flags failed");
        check(!compileHlslWithCache(source.data(),source.size(),"main","vs_5_0",
                D3DCOMPILE_PACK_MATRIX_ROW_MAJOR|D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR,0,code,error)&&error,
            "Conflicting matrix packing silently accepted");
        const auto legacyKey=shaderRequestKey(source.data(),source.size(),"main","vs_5_0",0,0,HlslCompiler::Legacy);
        check(legacyKey!=key,"Compiler generations share a request identity");
        check(compileHlslWithCache(source.data(),source.size(),"main","vs_5_0",0,0,code,error,HlslCompiler::Legacy),
            "Legacy compiler policy failed");
        const std::string_view legacyObject(static_cast<const char*>(code->GetBufferPointer()), code->GetBufferSize());
        check(legacyObject.find("DXIL") == std::string_view::npos,
            "Legacy compiler produced modern bytecode");
        check(!compileHlslWithCache(broken.data(),broken.size(),"main","vs_5_0",0,0,code,error,HlslCompiler::Legacy)&&error&&!code,
            "Legacy compiler failure retained stale bytecode");
        PreparationBatch compilers;
        for(unsigned i=0;i<16;++i)compilers.add([&,i] {
            const auto input=source+"// job "+std::to_string(i%8);
            Microsoft::WRL::ComPtr<ID3DBlob> first,second,diagnostic;
            check(compileHlslWithCache(input.data(),input.size(),"main","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,first,diagnostic),
                "Concurrent distinct/duplicate shader compile failed");
            check(compileHlslWithCache(input.data(),input.size(),"main","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,second,diagnostic),
                "Concurrent compiler cache reuse failed");
            check(first->GetBufferSize()==second->GetBufferSize()&&
                !std::memcmp(first->GetBufferPointer(),second->GetBufferPointer(),first->GetBufferSize()),
                "Concurrent compiler/cache output differs");
        });
        compilers.run(4);
        check(_putenv_s("PHLOSION_REQUIRE_PREPARED_SHADERS","1")==0,"Cannot set package guard");
        bool missingPackage=false;
        try { (void)compileHlslWithCache(source.data(),source.size(),"main","vs_5_0",0,0,code,error); }
        catch(const std::runtime_error&){missingPackage=true;}
        check(missingPackage,"Writable cache concealed missing release package");

    }
#endif
    std::cout<<"PASS cache identity, corruption, truncation, stale keys, atomic replacement, concurrent writers, unavailable storage and joined preparation failure\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
