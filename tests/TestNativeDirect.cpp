#include "support/ShaderFixture.h"
#include "engine/render/WorldEnvironmentLighting.h"
#include "engine/render/WorldClusteredLighting.h"
#include "engine/render/WorldIndirectGpu.h"
#include <limits>
int main(int argc,char** argv) try {
    using namespace shader_fixture;
    using namespace engine::render;
    check(argc==2,"Expected cpu, vulkan or d3d12");
    if(std::string(argv[1])=="cpu") {
        WorldEnvironmentLighting e;e.enabled=true;
        e.pointLights[3]={{1,2,3},12,{4,5,6}};
        auto& p=e.pointLights[3].source;p.axis={0,-1,0};p.cone=true;
        p.innerAngle=20;p.outerAngle=80;p.softness=3;p.attenuationCap=.2f;
        auto packed=packWorldPointLights(e);
        check(packed[17]==std::array<float,4>{0,-1,0,7},"Native point metadata lost");
        check(packed[18][3]==.2f && std::abs(packed[19][0]-std::cos(10.f*3.14159265f/180))<1e-6f,"Cone/cap packing");
        auto& r=e.rectLights[15];r={{2,3,4},12,{2,0,0},{0,0,.1f},{4,2,1}};
        r.response=WorldRectLightResponse::FiniteSource;r.source.radius=.1f;r.source.length=3.8f;
        r.source.cone=true;r.source.intensityScale=.8f;
        auto rp=packWorldRectLights(e);
        check(rp[61][3]==2 && rp[110][1]==3.8f && rp[111][3]==.8f,"Native last-slot truncation");
        auto historical=r;historical.response=WorldRectLightResponse::FiniteLine;
        const auto oldShadow=rectLightShadowView(historical),nativeShadow=rectLightShadowView(r);
        check(oldShadow.enabled&&nativeShadow.enabled,"Source shadow producer disabled");
        for(unsigned i=0;i<16;++i)check(std::abs(oldShadow.viewProjection[i]-nativeShadow.viewProjection[i])<1e-6f,
            "Native source changed its established shadow projection");
        auto shorter=r;shorter.source.length=1.f;
        check(rectLightShadowView(shorter).viewProjection!=nativeShadow.viewProjection,"Explicit source length ignored by projection");
        WorldIndirectLightingUpdate update;update.identity=update.revision=1;
        update.transport=std::make_shared<IndirectTransport>();update.environment=e;
        update.environment.pointLights[3].source.intensityScale=2;
        const auto gi=packIndirectUpdate(update,{}, {}, {},false);
        check(gi[5][2]==2 && gi[13][0]==8 && std::abs(gi[15][0]-3.2f)<1e-6f,
            "Native intensity scale diverged between direct and bounce producers");
        for(int bad=0;bad<7;++bad) {
            auto s=r.source;
            if(bad==0)s.axis={0,0,0};
            if(bad==1)s.radius=-1;
            if(bad==2)s.length=std::numeric_limits<float>::quiet_NaN();
            if(bad==3)s.outerAngle=s.innerAngle;
            if(bad==4)s.softness=0;
            if(bad==5)s.intensityScale=-1;
            if(bad==6)s.roughnessBias=INFINITY;
            check(!validWorldLocalLightSource(s),"Invalid source accepted");
        }
        check(packWorldPointLights(WorldEnvironmentLighting{})==WorldPointLightUniforms{},"Disabled source retained metadata");
        std::cout<<"PASS native source validation, cone units, last-slot metadata and disable\n";return 0;
    }
    const auto source=read(PHLOSION_FINITE_CORE)+"\n"+read(PHLOSION_NATIVE_FIXTURE);
    const auto values=run(argv[1],source,PHLOSION_NATIVE_SPV);
    double maximum=0,relativeMaximum=0,normalizedMaximum=0;
    for(int i=0;i<32;++i) {
        const double d=2+i%4,roughness=(.08+.92*(i%8)/7.0)*(i%5==0?.7:1);
        const double metal=i%3==0?1:0,r2=roughness*roughness;
        const double q=d*d/144,fade=1-q*q;
        double attenuation=i%2==0?fade*fade/(d*d):1-d/12;
        if(i%7==0)attenuation=std::min(attenuation,.03);
        const double pi=3.14159265358979323846,D=1/(pi*r2*r2);
        const double diffuse=(1-metal)/pi*(i>=16?1:1-.337748344*roughness);
        const double specular=i>=16?D*.125:std::min(D*.25,100.);
        const double fresnelApprox=std::exp2(-5.55473-6.98316);
        const auto falloff=[](double squared) {
            const double fade=1-std::min(std::pow(squared/144,2),1.);
            const double raw=fade*fade/std::max(squared,.0001);
            return raw<.0004?2*std::max(raw-.0002,0.):raw;
        };
        double extra=0;
        if(i<8) {
            const double x=1-.1*i;
            extra=std::pow(std::clamp((1/std::sqrt(x*x+1)-.2)/.7,0.,1.),2);
        } else if(i<16) extra=falloff(std::pow(10+.4*(i-8),2));
        else if(i<24) extra=.4/pi*(1-.337748344*.5)*falloff(d*d+5);
        else {
            const double h=.2*(i-23),a=(1-h)*(1-h)+d*d+.25,b=(1+h)*(1+h)+d*d+.25;
            extra=falloff(.5*(1-h*h+d*d+.25+std::sqrt(a*b)));
        }
        const std::array<double,3> albedo{.4,.2,.1},intensity{.3,2,.7};
        for(int c=0;c<8;++c) {
            const int channel=c%4;double expected;
            if(c==3)expected=attenuation;
            else if(c==7)expected=extra;
            else {
                double f0=metal?albedo[channel]:.04;
                if(i>=16)f0=fresnelApprox+(1-fresnelApprox)*f0;
                expected=(c<4?diffuse*albedo[channel]:specular*f0)*intensity[channel]*attenuation;
            }
            const double actual=values[i*8+c],error=std::abs(actual-expected);
            // At a nearly perfect half-vector the GGX peak is sensitive to
            // normalization's last two float32 bits. Bound that input rounding
            // analytically; retain the float64 reference rather than blessing output.
            const double h=1-2*std::numeric_limits<float>::epsilon();
            const double denominator=1-h*h+h*h*r2*r2;
            const double lowD=r2*r2/(pi*denominator*denominator);
            const double rounding=c>=4&&c<7?std::abs(expected)*(1-lowD/D):0;
            check(std::isfinite(actual)&&error<2e-5+.003*std::abs(expected)+rounding,
                "Native analytic case "+std::to_string(i)+" channel "+std::to_string(c)+
                " actual "+std::to_string(actual)+" expected "+std::to_string(expected));
            maximum=std::max(maximum,error);
            if(expected!=0)relativeMaximum=std::max(relativeMaximum,error/std::abs(expected));
            normalizedMaximum=std::max(normalizedMaximum,error/(1+std::abs(expected)));
        }
    }
    std::cout<<"PASS 32 independent float64 direct-light cases; max absolute error "<<maximum<<", relative "<<relativeMaximum<<", normalized "<<normalizedMaximum<<"\n";return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
