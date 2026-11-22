#include "support/ShaderFixture.h"
#include "engine/render/WorldPbrShaderShared.h"
int main(int argc,char** argv) try {
    using namespace shader_fixture;
    check(argc==2,"Expected vulkan or d3d12");
    using namespace engine::render::world_pbr_shader_shared;
    const auto hlsl=injectSharedWorldPbr("__PHLOSION_WORLD_LIGHTING_SHADER__",ShaderLanguage::Hlsl);
    const auto glsl=injectSharedWorldPbr("__PHLOSION_WORLD_LIGHTING_SHADER__",ShaderLanguage::Glsl);
    check(hlsl.find("#define PHLOSION_LOCAL_LIGHT_LOOP [loop]")!=std::string::npos &&
          glsl.find("#define PHLOSION_LOCAL_LIGHT_LOOP [loop]")==std::string::npos,
          "Local-light traversal lost its language-specific loop policy");
    for(const char* loop:{"PHLOSION_LOCAL_LIGHT_LOOP for(int pointIndex",
                         "PHLOSION_LOCAL_LIGHT_LOOP for(int rectIndex"})
        check(hlsl.find(loop)!=std::string::npos && glsl.find(loop)!=std::string::npos,
              "A local-light traversal bypasses the explicit loop policy");
    if(std::string(argv[1])=="translation") {
        std::cout<<"PASS D3D12/GLSL local-light loop translation contract\n";
        return 0;
    }
    auto source=read(PHLOSION_DIRECT_INPUTS)+"\n"+read(PHLOSION_FINITE_CORE)+"\n"+
        read(PHLOSION_DIRECT_CORE)+"\n"+read(PHLOSION_DIRECT_REFERENCE)+"\n"+read(PHLOSION_DIRECT_FIXTURE);
    const auto values=run(argv[1],source,PHLOSION_DIRECT_SPV);
    double error=0,listError=0;
    for(int lane=0;lane<32;++lane) {
        const auto* v=values.data()+lane*8;
        for(int j=0;j<8;++j)check(std::isfinite(v[j]),"Nonfinite direct response");
        check(v[0]<2e-5f,"Separated response changed frozen forward lighting");
        check(v[1]>=-1e-7f && v[2]>0 && v[3]>0,"Missing or negative diffuse/specular lobe");
        check(v[4]<2e-5f,"Indexed source traversal changed direct response");
        check(v[5]==0,"Fully shadowed source contributed radiance");
        const double roughness=.16+.84*lane/31.0,cutoff=1.0-16.0/(144.0*144.0);
        const double radiance=.3*cutoff*cutoff/4.0;
        const double diffuse=.96*.5*radiance/3.14159265;
        const double alphaSquared=std::pow(roughness,4);
        const double distribution=alphaSquared/std::max(3.14159265*alphaSquared*alphaSquared,1e-5);
        const double specular=.04*radiance*distribution/4.0;
        check(std::abs(v[6]-diffuse)<2e-6,"Diffuse output is not the analytic aligned point response");
        check(std::abs(v[7]-specular)<2e-5,"Specular output is not the analytic aligned GGX response");
        error=std::max(error,double(v[0]));listError=std::max(listError,double(v[4]));
    }
    std::cout<<"PASS 4096 mixed point/finite/LTC direct cases; normalized reference error "
             <<error<<", reordered list error "<<listError<<"\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<"\n";return 1;}
