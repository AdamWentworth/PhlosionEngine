#include "support/ShaderFixture.h"

int main(int argc,char** argv) try {
    using namespace shader_fixture;
    check(argc==2,"Expected opengl, vulkan or d3d12");const std::string api=argv[1];
    const auto source=read(PHLOSION_FINITE_CORE)+"\n"+read(PHLOSION_FINITE_FIXTURE);
    const auto actual=run(api,source,PHLOSION_FINITE_SPV);
    // Independent float64 evaluation of these public synthetic inputs; no capture data.
    constexpr std::array<double,128> expected{
#include "finite_light/expected.inc"
    };
    double maximum=0;
    for(std::size_t i=0;i<expected.size();++i) {
        const double error=std::abs(actual[i]-expected[i]),limit=2e-6+.002*std::abs(expected[i]);
        check(std::isfinite(actual[i])&&error<=limit,"Fixture "+std::to_string(i/8)+" channel "+std::to_string(i%8)+" actual="+std::to_string(actual[i])+" expected="+std::to_string(expected[i]));
        maximum=std::max(maximum,error);
    }
    const auto value=[&](std::size_t fixture,std::size_t channel){return actual[fixture*8+channel];};
    const auto close=[&](float a,float b) {return std::abs(a-b)<=2e-6+.002*std::abs(b);};
    // Independent double-precision area/intensity and angular mapping at case 16.
    constexpr std::array<float,8> mapped{.02857873223f,.005715746446f,.001428936612f,0,
        .0004348807113f,.0001739522845f,.00008697614225f,1};
    for(std::size_t c=0;c<8;++c) check(close(value(16,c),mapped[c]),"Rectangle area/intensity contract differs");
    for(std::size_t i=128;i<actual.size();++i) check(std::isfinite(actual[i])&&actual[i]>=0,"Nonfinite/negative production response");
    for(std::size_t c:{0u,1u,2u,4u,5u,6u}) {
        check(value(16,c)>0,"Mapped rectangle produced no lighting");
        check(value(17,c)==0&&value(18,c)==0&&value(23,c)==0,"Off/back/on-plane source leaked energy");
        check(close(value(19,c),value(16,c)),"Mapped response changes under rotation");
        check(close(value(20,c),2*value(16,c)),"Radiance-to-intensity mapping is nonlinear away from the cutoff");
        check(close(value(25,c),value(26,c)),"Authored zero roughness bypassed supported floor");
        if(c<3) {check(value(31,c)==0,"Metal retained diffuse");check(close(value(21,c),value(16,c)),"Authored F0 changed diffuse");}
        else check(value(21,c)>value(16,c),"Authored dielectric F0 was ignored");
    }
    std::cout<<"PASS "<<api<<" finite light: 16 numerical and 16 production/domain cases, max reference error "<<maximum<<'\n';return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
