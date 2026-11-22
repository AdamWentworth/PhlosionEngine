#include "support/ShaderFixture.h"
#include <sstream>
int main(int argc,char** argv) try {
    using namespace shader_fixture;
    check(argc==2,"Expected vulkan or d3d12");
    const std::string language=std::string(argv[1])=="d3d12"?"#define fract frac\n#define LAYERED_HLSL 1\n":"";
    for(const auto& name:{"fixture","terrain","mask","priority"}) {
        const std::string root=PHLOSION_LAYERED_FIXTURES;
        const auto code=language+read(PHLOSION_LAYERED_CORE)+"\n"+(std::string(name)=="priority"?read(PHLOSION_LAYERED_EVALUATOR)+"\n":"")+read((root+"/"+name+".glsl").c_str());
        const auto result=run(argv[1],code,(std::string(PHLOSION_LAYERED_BIN)+"/layered-"+name+".comp.spv").c_str());
        std::istringstream oracle(read((root+"/"+name+".expected").c_str()));
        double maximum=0;
        for(unsigned i=0;i<result.size();++i) {
            double expected=0;check(bool(oracle>>expected),"Missing independent oracle sample");
            const double error=std::abs(result[i]-expected);maximum=std::max(maximum,error);
            check(std::isfinite(result[i])&&error<=.00002,
                std::string(name)+" value "+std::to_string(i)+" actual "+std::to_string(result[i])+" expected "+std::to_string(expected));
        }
        double extra;check(!(oracle>>extra),"Unexpected oracle sample");
        std::cout<<"PASS 32 "<<name<<" cases, maximum error "<<maximum<<"\n";
    }
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
