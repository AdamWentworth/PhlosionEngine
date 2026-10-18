#include "support/ShaderFixture.h"

int main(int argc,char** argv) try {
    using namespace shader_fixture;
    check(argc==2,"Expected opengl, vulkan or d3d12");const std::string api=argv[1];
    const auto source=read(PHLOSION_MOMENT_CORE)+"\n"+read(PHLOSION_FINITE_CORE)+"\n"+read(PHLOSION_MOMENT_FIXTURE);
    const auto actual=run(api,source,PHLOSION_MOMENT_SPV);
    // Float64 oracle with public inputs, independent of the shader implementation.
    constexpr std::array<double,256> expected{
#include "shadow_moments/expected.inc"
    };
    double maximum=0;
    for(std::size_t i=0;i<expected.size();++i) {
        const double error=std::abs(actual[i]-expected[i]);
        check(std::isfinite(actual[i])&&error<=4e-5,"Fixture "+std::to_string(i/8)+" channel "+std::to_string(i%8)+
            " actual="+std::to_string(actual[i])+" expected="+std::to_string(expected[i]));
        maximum=std::max(maximum,error);
    }
    std::cout<<"PASS "<<api<<" shadow moments: 32 filtering/visibility cases, max reference error "<<maximum<<'\n';return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
