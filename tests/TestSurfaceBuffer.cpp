#include "support/ShaderFixture.h"

int main(int argc, char** argv) try {
    using namespace shader_fixture;
    check(argc == 2, "Expected vulkan or d3d12");
    const auto actual = run(argv[1], std::string("vec3 worldWetEmissiveColor(vec3 color,vec3 emission){return color;}\n")+read(PHLOSION_SURFACE_CORE) + "\n" +
        read(PHLOSION_SURFACE_FIXTURE), PHLOSION_SURFACE_SPV);
    for (int c = 0; c < 8; ++c) {
        const double f = c, length = std::sqrt((f-3)*(f-3)+5);
        const double x = (f-3)/length, y = 2/length, z = -1/length;
        const std::array<double,32> expected{
            std::sqrt(f/8), .5, std::sqrt(.75), 0, f/8, .25, .75, std::min(f*.2,1.),
            x*.5+.5, y*.5+.5, z*.5+.5, 1./3, x,y,z,.04+f*.12,
            f/7, .02+f*.01, std::min(f*.2,1.),.04+f*.12, f/7,.02+f*.01,std::min(f*.2,1.),.04+f*.12,
            f*4,.5,1000,0, f/7,1,0,0};
        for (int j=0; j<32; ++j)
            check(std::isfinite(actual[c*32+j]) &&
                  std::abs(actual[c*32+j]-expected[j]) < 2e-5,
                  "Surface channel mismatch case " + std::to_string(c) + " channel " + std::to_string(j));
    }
    std::cout << "32 surface packing/readback cases passed; HDR emission, normals, roughness, metal, AO and specular.\n";
    std::string core=read(PHLOSION_SURFACE_CORE);
    if(std::string(argv[1])=="d3d12")for(const std::string declaration:{
        "WorldSurfaceBuffer worldSurfaceOutput;","bool worldSurfaceValid = false;"}) {
        const auto at=core.find(declaration);check(at!=std::string::npos,"Surface capture state declaration");
        core.insert(at,"static ");
    }
    const auto participation=run(argv[1],
        "#define PHLOSION_WORLD_SURFACE_CAPTURE 1\n#define CLUSTER_VIEW(i) vec4(0.0,0.0,1.0,0.0)\n"
        "vec3 worldWetEmissiveColor(vec3 color,vec3 emission){return color;}\n"+core+"\n"+
        read(PHLOSION_SURFACE_PARTICIPATION),PHLOSION_SURFACE_PARTICIPATION_SPV);
    for(unsigned i=0;i<32;++i) {
        const double f=double(i)/31.;
        const std::array<double,8> expected{0.,1./3.,f,.2+.8*f,2./3.,1./3.,1.,.04};
        for(unsigned k=0;k<8;++k)check(std::isfinite(participation[i*8+k])&&
            std::abs(participation[i*8+k]-expected[k])<2e-5,
            "Shared-local/forward-primary participation contract "+std::to_string(i)+":"+std::to_string(k));
    }
    std::cout<<"32 shared-local/forward-primary contracts passed with primary composition enabled; surface values retained and specialized local fallback remains separate.\n";
    const auto foliage=run(argv[1],
        "#define PHLOSION_WORLD_SURFACE_CAPTURE 1\n#define CLUSTER_VIEW(i) vec4(0.0,0.0,1.0,0.0)\n"
        "vec3 worldWetEmissiveColor(vec3 color,vec3 emission){return color;}\n"+core+"\n"+
        read(PHLOSION_SURFACE_FOLIAGE),PHLOSION_SURFACE_FOLIAGE_SPV);
    for(unsigned i=0;i<32;++i) {
        const double f=double(i)/31.;
        const std::array<double,8> expected{1.,f,0.,1.-f,f,.2+.8*f,1.,.04};
        for(unsigned k=0;k<8;++k)check(std::isfinite(foliage[i*8+k])&&
            std::abs(foliage[i*8+k]-expected[k])<2e-5,
            "Composed leaf transmission/dielectric material "+std::to_string(i)+":"+std::to_string(k));
    }
    std::cout<<"32 composed foliage packing/consumer contracts passed; transmission is separate from metallic response.\n";
    const auto receiver=run(argv[1],
        "#define WORLD_GI_PRIVATE static\n#define ivec2 int2\n"+read(PHLOSION_RECEIVER_BINDINGS)+"\n"+
        read(PHLOSION_RECEIVER_CORE)+"\n"+read(PHLOSION_RECEIVER_FIXTURE),PHLOSION_RECEIVER_SPV);
    for(unsigned i=0;i<32;++i) {
        const unsigned c=i%16;
        const bool valid=c==1||c==2||c==7||c==11||c==12||c==14||c==15;
        const std::array<double,8> expected{valid?1.:0.,valid?(c==7?.75:.25):0.,valid?.5:0.,
            valid?(c==7?.25:.75):0.,valid?(c==7?.75:.25):0.,valid?.5:0.,valid?(c==7?.25:.75):0.,valid&&c!=11?1.:0.};
        for(unsigned k=0;k<8;++k)check(std::isfinite(receiver[i*8+k])&&
            std::abs(receiver[i*8+k]-expected[k])<2e-5,
            "Shared GI receiver match/rejection "+std::to_string(c)+":"+std::to_string(k));
    }
    std::cout<<"32 shared GI receiver contracts passed: depth, normal, view, participation and zero-weight receivers.\n";
    const auto producer=run(argv[1],
        "#define WORLD_GI_PRIVATE static\n#define ivec2 int2\n"+
        read(PHLOSION_RECEIVER_PRODUCER_BINDINGS)+"\n"+read(PHLOSION_RECEIVER_ENTRY)+"\n"+
        read(PHLOSION_RECEIVER_PRODUCER),PHLOSION_RECEIVER_PRODUCER_SPV);
    for(unsigned i=0;i<32;++i) {
        const bool active=i%4==0;
        const double x=(double(i%8)+.5)/4.-1.,y=1.-(double(i/8)+.5)/2.;
        const std::array<double,8> expected{active?x:0.,active?y+1.2:0.,active?1.6:0.,active?.75:0.,
            active?x:0.,active?y-1.2:0.,active?-1.6:0.,active?.75:0.};
        for(unsigned k=0;k<8;++k)check(std::isfinite(producer[i*8+k])&&
            std::abs(producer[i*8+k]-expected[k])<2e-5,
            "Two-sided GI preparation/address/clear contract "+std::to_string(i)+":"+std::to_string(k));
    }
    std::cout<<"32 two-sided GI producer contracts passed with independent expected directions, coordinates and mask clearing.\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << "\n";
    return 1;
}
