#include "engine/render/d3d12/D3D12RenderBackendPipelineCompile.h"
#include "engine/render/PreparationBatch.h"
#include "engine/render/ShaderCacheStorage.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <mutex>

int main(int argc,char** argv) try {
#ifdef _WIN32
    if(argc!=3)throw std::runtime_error("Usage: PhlosionShaderCook <recipe-directory> <output-directory>");
    const std::filesystem::path recipes=argv[1],output=argv[2];
    if(_putenv_s("PHLOSION_PREPARED_SHADER_DIR",(output/"no-input-package").string().c_str()) ||
       _putenv_s("PHLOSION_SHADER_RECIPE_DIR","") || _putenv_s("PHLOSION_REQUIRE_PREPARED_SHADERS","") ||
       _putenv_s("PHLOSION_REQUIRE_SHADER_CACHE_WRITES","1"))
        throw std::runtime_error("Cannot configure shader preparation output");
    engine::render::PreparationBatch jobs;
    std::atomic_size_t completed{0};std::size_t count=0,totalBytes=0;
    for(const auto& item:std::filesystem::directory_iterator(recipes)) {
        if(item.path().extension()!=".json")continue;
        if(item.file_size()>engine::render::shader_cache::maxShaderBytes+65536)throw std::runtime_error("Shader recipe too large");
        totalBytes+=static_cast<std::size_t>(item.file_size());
        if(totalBytes>256u*1024u*1024u)throw std::runtime_error("Shader recipes exceed preparation memory budget");
        auto recipe=nlohmann::json::parse(std::ifstream(item.path()));
        if(recipe.at("schema")!=1)throw std::runtime_error("Unsupported shader recipe");
        jobs.add([recipe=std::move(recipe),&completed,output] {
            const auto policy=recipe.at("compiler_policy").get<std::string>();
            using engine::render::d3d12_pipeline_compile::HlslCompiler;
            if(policy!="dxc-sm6-hv2016-gec-v1"&&policy!="fxc-sm5-v1")
                throw std::runtime_error("Unknown shader compiler policy");
            const auto compiler=policy=="fxc-sm5-v1"?HlslCompiler::Legacy:HlslCompiler::Modern;
            const auto source=recipe.at("source").get<std::string>();
            const auto entry=recipe.at("entry").get<std::string>(),target=recipe.at("target").get<std::string>();
            const auto key=engine::render::d3d12_pipeline_compile::shaderRequestKey(source.data(),source.size(),entry.c_str(),target.c_str(),
                recipe.at("flags1").get<UINT>(),recipe.at("flags2").get<UINT>(),compiler);
            if(recipe.at("key").get<std::string>()!=engine::render::shader_cache::name(key))
                throw std::runtime_error("Shader recipe identity is stale or changed");
            Microsoft::WRL::ComPtr<ID3DBlob> code,error;
            if(!engine::render::d3d12_pipeline_compile::compileHlslWithCache(source.data(),source.size(),entry.c_str(),target.c_str(),
                recipe.at("flags1").get<UINT>(),recipe.at("flags2").get<UINT>(),code,error,compiler))
                throw std::runtime_error("Shader preparation failed: "+engine::render::d3d12_pipeline_compile::d3dCompileErrorMessage(error.Get()));
            if(!engine::render::shader_cache::write(output/"d3d12"/(engine::render::shader_cache::name(key)+".shader"),1,key,
                {static_cast<const std::uint8_t*>(code->GetBufferPointer()),code->GetBufferSize()}))
                throw std::runtime_error("Prepared shader package is not writable");
            ++completed;
        });
        ++count;
    }
    if(!count)throw std::runtime_error("Shader recipe directory is empty");
    jobs.run(4);
    std::cout<<"Prepared "<<completed<<" HLSL shaders; no GPU/device or game assets required.\n";
    return 0;
#else
    (void)argc;(void)argv;throw std::runtime_error("HLSL preparation requires Windows");
#endif
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
