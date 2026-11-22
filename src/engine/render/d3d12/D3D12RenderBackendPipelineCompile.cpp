#include "engine/render/PreparationProgress.h"
#include <mutex>
#include "engine/render/ShaderCacheStorage.h"
#include <nlohmann/json.hpp>
#include <stdexcept>
#include "engine/render/ShaderPreparation.h"
#include "engine/render/d3d12/D3D12RenderBackendPipelineCompile.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <memory>
#include <iterator>
#include <unordered_map>
#include <syncstream>

#if defined(_WIN32)
#include <dxcapi.h>
namespace {
using namespace engine::render::shader_cache;

struct ModernCompiler {
    HMODULE module{};
    DxcCreateInstanceProc create{};
    std::uint64_t identity{};
    ModernCompiler() {
        std::array<wchar_t,32768> exe{};
        const auto count=GetModuleFileNameW(nullptr,exe.data(),static_cast<DWORD>(exe.size()));
        if(!count||count>=exe.size())throw std::runtime_error("Cannot locate shader compiler runtime");
        const auto directory=std::filesystem::path(exe.data()).parent_path();
        Key key;key.text("DXC-SM6-HV2016-Gec-v1");
        for(const auto* file:{"dxcompiler.dll","dxil.dll"}) {
            std::ifstream input(directory/file,std::ios::binary);
            std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),{});
            if(bytes.empty()||input.bad())throw std::runtime_error(std::string("Cannot read shader compiler runtime: ")+file);
            key.text(file);key.bytes(bytes.data(),bytes.size());
        }
        module=LoadLibraryExW((directory/"dxcompiler.dll").c_str(),nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if(!module)throw std::runtime_error("Cannot load dxcompiler.dll beside executable");
        create=reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(module,"DxcCreateInstance"));
        if(!create)throw std::runtime_error("Missing DxcCreateInstance");
        identity=key.value;
        // Retain the module for process lifetime: compilation jobs own its COM objects.
    }
};
ModernCompiler& modernCompiler(){static ModernCompiler compiler;return compiler;}
std::uint64_t legacyCompilerIdentity() {
    static const std::uint64_t identity=[] {
        // The SDK import-library function address can name an executable thunk.
        // Hash the loaded SDK compiler DLL, shared by runtime, editor and cooker.
        const HMODULE module=GetModuleHandleW(L"d3dcompiler_47.dll");
        if(!module)throw std::runtime_error("Cannot identify loaded HLSL compiler");
        std::array<wchar_t,32768> path{};
        const auto count=GetModuleFileNameW(module,path.data(),static_cast<DWORD>(path.size()));
        if(!count||count>=path.size())throw std::runtime_error("Cannot locate HLSL compiler");
        std::ifstream input(std::filesystem::path(path.data()),std::ios::binary);
        if(!input)throw std::runtime_error("Cannot read HLSL compiler identity");
        std::array<std::uint8_t,65536> block{};std::uint64_t hash=14695981039346656037ull;
        while(input){input.read(reinterpret_cast<char*>(block.data()),block.size());hash=hashBytes({block.data(),static_cast<std::size_t>(input.gcount())},hash);}
        if(!input.eof())throw std::runtime_error("Cannot hash HLSL compiler");
        return hash;
    }();
    return identity;
}

using engine::render::d3d12_pipeline_compile::HlslCompiler;
std::uint64_t compilerIdentity(HlslCompiler compiler) {
    return compiler==HlslCompiler::Legacy?legacyCompilerIdentity():modernCompiler().identity;
}
std::shared_ptr<std::mutex> requestMutex(std::uint64_t key) {
    static std::mutex guard;
    static std::unordered_map<std::uint64_t,std::weak_ptr<std::mutex>> requests;
    std::lock_guard lock(guard);
    for(auto it=requests.begin();it!=requests.end();)
        if(it->second.expired())it=requests.erase(it);else ++it;
    auto& slot=requests[key];auto mutex=slot.lock();
    if(!mutex){mutex=std::make_shared<std::mutex>();slot=mutex;}
    return mutex;
}
HRESULT compileModern(const void* source,std::size_t size,const char* entry,const char* target,UINT flags1,UINT flags2,
    Microsoft::WRL::ComPtr<ID3DBlob>& code,Microsoft::WRL::ComPtr<ID3DBlob>& error) {
    constexpr UINT supported=D3DCOMPILE_DEBUG|D3DCOMPILE_SKIP_OPTIMIZATION|D3DCOMPILE_OPTIMIZATION_LEVEL2|
        D3DCOMPILE_PACK_MATRIX_ROW_MAJOR|D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR|D3DCOMPILE_ENABLE_STRICTNESS|
        D3DCOMPILE_WARNINGS_ARE_ERRORS|D3DCOMPILE_IEEE_STRICTNESS;
    auto failure=[&](const char* message) {
        const auto length=std::strlen(message);
        if(SUCCEEDED(D3DCreateBlob(length,error.ReleaseAndGetAddressOf())))std::memcpy(error->GetBufferPointer(),message,length);
        return E_INVALIDARG;
    };
    if(flags2||(flags1&~supported))return failure("Unsupported DXC compatibility flags");
    if((flags1&D3DCOMPILE_PACK_MATRIX_ROW_MAJOR)&&(flags1&D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR))
        return failure("Conflicting matrix packing flags");
    std::string profile=target;
    if(profile!="vs_5_0"&&profile!="ps_5_0"&&profile!="cs_5_0"&&profile!="gs_5_0"&&profile!="hs_5_0"&&profile!="ds_5_0")
        return failure("Expected a supported Shader Model 5 compatibility profile");
    profile[3]='6';
    std::wstring wideProfile(profile.begin(),profile.end()),wideEntry(entry,entry+std::strlen(entry));
    std::vector<LPCWSTR> arguments{L"-E",wideEntry.c_str(),L"-T",wideProfile.c_str(),L"-HV",L"2016",L"-Gec"};
    const UINT level=flags1&D3DCOMPILE_OPTIMIZATION_LEVEL2;
    arguments.push_back((flags1&D3DCOMPILE_SKIP_OPTIMIZATION)?L"-Od":
        level==D3DCOMPILE_OPTIMIZATION_LEVEL3?L"-O3":
        level==D3DCOMPILE_OPTIMIZATION_LEVEL2?L"-O2":
        level==D3DCOMPILE_OPTIMIZATION_LEVEL0?L"-O0":L"-O1");
    if(flags1&D3DCOMPILE_DEBUG){arguments.push_back(L"-Zi");arguments.push_back(L"-Qembed_debug");}
    if(flags1&D3DCOMPILE_PACK_MATRIX_ROW_MAJOR)arguments.push_back(L"-Zpr");
    if(flags1&D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR)arguments.push_back(L"-Zpc");
    if(flags1&D3DCOMPILE_ENABLE_STRICTNESS)arguments.push_back(L"-Ges");
    if(flags1&D3DCOMPILE_WARNINGS_ARE_ERRORS)arguments.push_back(L"-WX");
    if(flags1&D3DCOMPILE_IEEE_STRICTNESS)arguments.push_back(L"-Gis");
    Microsoft::WRL::ComPtr<IDxcCompiler3> compiler;
    auto hr=modernCompiler().create(CLSID_DxcCompiler,IID_PPV_ARGS(compiler.GetAddressOf()));
    if(FAILED(hr))return hr;
    DxcBuffer buffer{source,size,DXC_CP_UTF8};
    Microsoft::WRL::ComPtr<IDxcResult> result;
    hr=compiler->Compile(&buffer,arguments.data(),static_cast<UINT32>(arguments.size()),nullptr,IID_PPV_ARGS(result.GetAddressOf()));
    if(FAILED(hr))return hr;
    Microsoft::WRL::ComPtr<IDxcBlobUtf8> messages;
    if(SUCCEEDED(result->GetOutput(DXC_OUT_ERRORS,IID_PPV_ARGS(messages.GetAddressOf()),nullptr))&&messages&&messages->GetStringLength()) {
        hr=D3DCreateBlob(messages->GetStringLength(),error.ReleaseAndGetAddressOf());
        if(FAILED(hr))return hr;
        std::memcpy(error->GetBufferPointer(),messages->GetStringPointer(),messages->GetStringLength());
    }
    HRESULT status{};hr=result->GetStatus(&status);
    if(FAILED(hr)||FAILED(status))return FAILED(hr)?hr:status;
    Microsoft::WRL::ComPtr<IDxcBlob> object;
    hr=result->GetOutput(DXC_OUT_OBJECT,IID_PPV_ARGS(object.GetAddressOf()),nullptr);
    if(FAILED(hr)||!object)return FAILED(hr)?hr:E_FAIL;
    hr=D3DCreateBlob(object->GetBufferSize(),code.ReleaseAndGetAddressOf());
    if(SUCCEEDED(hr))std::memcpy(code->GetBufferPointer(),object->GetBufferPointer(),object->GetBufferSize());
    return hr;
}
bool loadBlob(const std::filesystem::path& path,std::uint64_t key,Microsoft::WRL::ComPtr<ID3DBlob>& out) {
    const auto payload=read(path,1,key,maxShaderBytes);
    if(payload.empty()||FAILED(D3DCreateBlob(payload.size(),out.ReleaseAndGetAddressOf())))return false;
    std::memcpy(out->GetBufferPointer(),payload.data(),payload.size());return true;
}
void recordRecipe(std::uint64_t key,const void* source,std::size_t size,const char* entry,const char* target,UINT flags1,UINT flags2,HlslCompiler compiler) {
    const auto root=engine::env::get("PHLOSION_SHADER_RECIPE_DIR");
    if(!root||root->empty())return;
    nlohmann::json recipe={{"schema",1},{"compiler_policy",compiler==HlslCompiler::Legacy?"fxc-sm5-v1":"dxc-sm6-hv2016-gec-v1"},{"source",std::string(static_cast<const char*>(source),size)},
        {"entry",entry},{"target",target},{"flags1",flags1},{"flags2",flags2},{"key",name(key)},{"compiler",name(compilerIdentity(compiler))}};
    const auto text=recipe.dump();
    if(!atomicWrite(std::filesystem::path(*root)/(name(key)+".json"),
        {reinterpret_cast<const std::uint8_t*>(text.data()),text.size()}))
        throw std::runtime_error("Cannot record shader preparation recipe");
}
} // namespace

namespace engine::render::d3d12_pipeline_compile {

std::string d3dCompileErrorMessage(ID3DBlob* errBlob) {
    if (!errBlob || !errBlob->GetBufferPointer() || errBlob->GetBufferSize() == 0) {
        return {};
    }
    const char* msg = static_cast<const char*>(errBlob->GetBufferPointer());
    return std::string(msg, msg + errBlob->GetBufferSize());
}

UINT d3dCompileFlags() {
#if defined(_DEBUG)
    // Keep shader symbols in Debug, but allow opt-in optimized shader code so
    // renderer perf tracks Release more closely during day-to-day testing.
#if defined(PHLOSION_OPTIMIZE_D3D12_SHADERS_IN_DEBUG)
    return D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_DEBUG;
#else
    return D3DCOMPILE_SKIP_OPTIMIZATION | D3DCOMPILE_DEBUG;
#endif
#else
    return D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
}

std::uint64_t shaderRequestKey(const void* source,std::size_t size,const char* entry,const char* target,UINT flags1,UINT flags2,HlslCompiler compiler) {
    Key key;key.field(std::uint64_t{compiler==HlslCompiler::Legacy?5u:4u});key.field(flags1);key.field(flags2);
    key.text(entry);key.text(target);key.bytes(source,size);return key.value;
}

bool compileHlslWithCache(const void* sourceData,
                          std::size_t sourceSize,
                          const char* entryPoint,
                          const char* target,
                          UINT flags1,
                          UINT flags2,
                          Microsoft::WRL::ComPtr<ID3DBlob>& outBlob,
                          Microsoft::WRL::ComPtr<ID3DBlob>& errBlob,
                          HlslCompiler compiler) {
    if(!sourceData||!sourceSize||!entryPoint||!target)return false;
    engine::render::ShaderPreparationScope trace("d3d12-hlsl", std::string(target) + ":" + entryPoint);
    outBlob.Reset();
    errBlob.Reset();

    using namespace engine::render::shader_cache;
    if(!sourceData||!sourceSize||sourceSize>maxShaderBytes||!entryPoint||!target)return false;
    Key key;key.value=shaderRequestKey(sourceData,sourceSize,entryPoint,target,flags1,flags2,compiler);
    const auto request=requestMutex(key.value);
    std::lock_guard requestLock(*request);
    recordRecipe(key.value,sourceData,sourceSize,entryPoint,target,flags1,flags2,compiler);
    const auto leaf=name(key.value)+".shader";
    // Prepared bytecode is portable across GPUs; device/driver binaries are separate.
    if(loadBlob(preparedRoot()/"d3d12"/leaf,key.value,outBlob)) {
        trace.cacheHit(true);return true;
    }
    if(engine::env::flagEnabled("PHLOSION_REQUIRE_PREPARED_SHADERS"))
        throw std::runtime_error("Prepared HLSL shader missing: "+leaf);
    if(loadBlob(writableRoot()/"d3d12"/name(compilerIdentity(compiler))/leaf,key.value,outBlob)) {
        trace.cacheHit(true);return true;
    }
    std::osyncstream(std::clog)<<"[ShaderPreparation] compiling "<<target<<':'<<entryPoint<<" key="<<name(key.value)<<'\n';

    const auto compileResult=engine::render::prepareWithProgress(
        std::string("Preparing shader ")+target+":"+entryPoint,[&] {
            engine::render::ShaderPreparationScope compileTrace(compiler==HlslCompiler::Legacy?"fxc-compile":"dxc-compile",name(key.value));
            if(compiler==HlslCompiler::Legacy)
                return D3DCompile(sourceData,sourceSize,nullptr,nullptr,nullptr,entryPoint,target,flags1,flags2,
                    outBlob.ReleaseAndGetAddressOf(),errBlob.ReleaseAndGetAddressOf());
            return compileModern(sourceData,sourceSize,entryPoint,target,flags1,flags2,outBlob,errBlob);
        });
    if (FAILED(compileResult) ||
        !outBlob) {
        return false;
    }

    if(!write(writableRoot()/"d3d12"/name(compilerIdentity(compiler))/leaf,1,key.value,
        {static_cast<const std::uint8_t*>(outBlob->GetBufferPointer()),outBlob->GetBufferSize()}))
    {
        if(engine::env::flagEnabled("PHLOSION_REQUIRE_SHADER_CACHE_WRITES"))
            throw std::runtime_error("Prepared shader output is not writable");
        std::clog<<"[ShaderPreparation] shader cache unavailable; using compiled bytecode\n";
    }
    return true;
}

} // namespace engine::render::d3d12_pipeline_compile
#endif
