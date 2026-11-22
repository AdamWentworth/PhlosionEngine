#include "support/ShaderFixture.h"
int main(int argc,char** argv) try {
    using namespace shader_fixture;
    check(argc==2,"Expected vulkan or d3d12");
    const std::string language=std::string(argv[1])=="d3d12"?"#define fract frac\n":"";
    const auto source=language+read(PHLOSION_FINITE_CORE)+"\n"+read(PHLOSION_FOLIAGE_CORE)+"\n"+read(PHLOSION_FOLIAGE_FIXTURE);
    const auto values=run(argv[1],source,PHLOSION_FOLIAGE_SPV);
    constexpr double pi=3.14159265358979323846;
    double maximum=0;
    for(unsigned i=0;i<32;++i) {
        const double angular=std::array{0.,.00466875575,.006975,.0105}[i%4];
        const double theta=i*pi/16,rough=.08+.92*(i%8)/7.,trans=(i%3)*.5;
        const auto translucency=[&](double vl,double ns) {
            const double a=std::pow(.2+.5*rough,2),q=std::max(-vl,0.);
            return trans*(1+std::abs(ns))*.5*a/(pi*std::pow(1+(a-1)*q*q,2));
        };
        std::array<double,8> expected{.3,(i&2)?-.4:.4,
            ((i&4)?-1.:1.)*((i&1)?1.:-1.)*std::sqrt(.75),
            translucency(std::cos(theta),std::max(std::cos(theta),0.)),0,0,0,0};
        // Analytic disk representative for N=V=+Z. At the opposite pole the
        // production bounded extension picks the center of the disk.
        const double separation=std::acos(std::clamp(std::cos(theta),-1.,1.));
        const double effective=std::abs(std::sin(theta))<1e-6?separation:std::max(separation-angular,0.);
        const double vl=std::cos(effective),nl=std::max(vl,0.);
        const double nh=std::sqrt(std::max(0.,(1+vl)*.5)),lh=nh;
        const double sun=std::max(std::cos(theta),0.);
        const double energy=1-.337748344*rough,edge=(.5+2*lh*lh)*rough-1;
        const double diffuse=(1+edge*std::pow(1-nl,5))*energy/pi;
        for(unsigned c=0;c<3;++c)expected[4+c]=(.4/std::pow(2.,c))*(diffuse*sun+translucency(vl,sun));
        const double a=std::pow(rough,4),d=a/(pi*std::pow(1+(a-1)*nh*nh,2));
        const double visibility=.5/(nl+std::sqrt(nl*nl*(1-a)+a));
        expected[7]=(.04+.96*std::pow(1-lh,5))*std::min(d*nl*visibility,100.);
        for(unsigned c=0;c<8;++c) {
            const double actual=values[i*8+c],error=std::abs(actual-expected[c]);
            check(std::isfinite(actual)&&error<.0003+.002*std::abs(expected[c]),
                "Leaf case "+std::to_string(i)+" channel "+std::to_string(c)+" actual "+
                std::to_string(actual)+" expected "+std::to_string(expected[c]));
            maximum=std::max(maximum,error);
        }
    }
    std::cout<<"PASS 32 independent analytic leaf cases (256 channels): front/back, tangent sign, negative normal Z, sun disk, roughness, transmission; maximum error "<<maximum<<"\n";

    const auto profileSource=language+read(PHLOSION_FINITE_CORE)+"\n"+read(PHLOSION_FOLIAGE_CORE)+"\n"+
        read(PHLOSION_FOLIAGE_GEOMETRY)+"\n"+read(PHLOSION_FOLIAGE_PROFILE);
    const auto profile=run(argv[1],profileSource,PHLOSION_FOLIAGE_PROFILE_SPV);
    for(unsigned i=0;i<32;++i) {
        const double angle=(i%8)*pi/8.,scale=i<16?1.:1.7;
        // Orthographic, unit local point at (0,1,.5), forward=-view. Projection
        // into the horizontal view basis followed by a one-radius back offset.
        const double x=-std::sin(angle)-std::cos(angle);
        const double y=std::cos(angle)-std::sin(angle);
        const double width=8*pi/180;
        const double coverage=angle>pi/8+width?-2.:std::sqrt(std::clamp((pi/8-angle+width)/(2*width),0.,1.));
        const double px=i%4,phase=px*.7;
        const auto fract=[](double v){return v-std::floor(v);};
        const double noise=fract(52.9829189*fract((px+phase)*.06711056+(.5+phase)*.00583715));
        const double cutoff=(.3+(.15-.3)*5/9.)*(1+noise);
        const double distance=-2.02/(.6-1.02);
        const std::array<double,8> expected{x,y,.5,coverage,cutoff,
            .5*(1.02-2.02/(distance-2))+.5,.4+((i%2)? .1:0.),.68};
        (void)scale; // Unit-local output remains invariant under uniform scale.
        for(unsigned k=0;k<8;++k)check(std::isfinite(profile[i*8+k])&&std::abs(profile[i*8+k]-expected[k])<.00015,
            "Foliage profile/geometry case "+std::to_string(i)+" channel "+std::to_string(k)+
            " actual "+std::to_string(profile[i*8+k])+" expected "+std::to_string(expected[k]));
    }
    std::cout<<"PASS 32 analytic profile/billboard cases: eight angles, scale invariance, angular overlap, alpha footprint/phase, atlas fade, perspective/orthographic depth\n";
    const auto terrain=run(argv[1],language+read(PHLOSION_FINITE_CORE)+"\n"+
        read(PHLOSION_FOLIAGE_CORE)+"\n"+read(PHLOSION_FOLIAGE_TERRAIN),PHLOSION_FOLIAGE_TERRAIN_SPV);
    for(unsigned i=0;i<32;++i) {
        // Closed-form endpoint/interior expectations, including tint-preserve
        // and the independently varied height and camera-distance ramps.
        const double heightWeight=std::max(1.-(i%4)*.5,0.);
        const double nearWeight=std::max(1.-((i/4)%4)/3.,0.);
        const double terrainWeight=1.-(1.-heightWeight)*.5*nearWeight*(1.+nearWeight);
        const std::array<double,3> base=i<16?std::array{.5625,.37,.3525}:std::array{.8,.4,.2};
        const std::array<double,3> ground{.1,.2,.3};
        const double y=.5*terrainWeight*terrainWeight,z=1.-y,n=std::hypot(y,z);
        const std::array<double,8> expected{
            base[0]*(1.-.8*terrainWeight)+ground[0]*.8*terrainWeight,
            base[1]*(1.-.8*terrainWeight)+ground[1]*.8*terrainWeight,
            base[2]*(1.-.8*terrainWeight)+ground[2]*.8*terrainWeight,
            .5-.46*heightWeight,0.,y/n,z/n,(i%2)?2.4:3.};
        for(unsigned k=0;k<8;++k)check(std::isfinite(terrain[i*8+k])&&std::abs(terrain[i*8+k]-expected[k])<.00002,
            "Foliage terrain/tint case "+std::to_string(i)+" channel "+std::to_string(k));
    }
    std::cout<<"PASS 32 analytic tint/terrain cases: preserved tint, transmission weights, height/distance blend, normalized normals\n";
    const auto depth=run(argv[1],language+read(PHLOSION_FINITE_CORE)+"\n"+
        read(PHLOSION_FOLIAGE_CORE)+"\n"+read(PHLOSION_FOLIAGE_DEPTH),PHLOSION_FOLIAGE_DEPTH_SPV);
    for(unsigned i=0;i<32;++i) {
        const double d=4.+i,offset=-.25*(i%4);
        const bool perspective=(i%2)!=0;
        const double original=perspective?.5*(1.002-.2002/d)+.5:.5*(.02*(5.2+d)-.7)+.5;
        const double projected=perspective?.5*(1.002-.2002/(d+offset))+.5:
            .5*(.02*(5.2+d+offset)-.7)+.5;
        const double rayScale=perspective?(d+offset)/d:1.;
        const std::array<double,8> expected{projected,projected,original,1.,d+offset,3.*rayScale,4.*rayScale,original};
        for(unsigned k=0;k<8;++k)check(std::isfinite(depth[i*8+k])&&std::abs(depth[i*8+k]-expected[k])<.00004,
            "Foliage absolute/proxy depth case "+std::to_string(i)+" channel "+std::to_string(k));
    }
    std::cout<<"PASS 32 independent depth cases: absolute projection, conservative proxy, off-axis perspective/orthographic displacement and original motion depth\n";
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
