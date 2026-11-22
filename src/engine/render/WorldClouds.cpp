#include "engine/render/IWorldClouds.h"
#include <algorithm>
#include <cmath>
namespace engine::render {
void validateCloudSettings(const WorldCloudSettings& c) {
    for(float x:{c.bottom,c.top,c.horizonCoverage,c.density,c.coverageScale,c.noiseScale,c.detailScale,
        c.shadowIntensity,c.worldShadowIntensity,c.lightIntensity,c.ambientIntensity,c.inScatter,
        c.outScatter,c.inVsOut,c.silverLining,c.ambientOutscatter,c.rayStart,c.rayFalloff,c.distantFogOpacity})
        if(!std::isfinite(x)||x<0)throw std::invalid_argument("Invalid cloud settings");
    for(auto offset:{c.coverageOffset,c.noiseOffset,c.detailOffset})for(float x:offset)
        if(!std::isfinite(x))throw std::invalid_argument("Invalid cloud wind");
    if(c.top<=c.bottom||c.top>10000||c.coverageScale<=0||c.noiseScale<=0||c.detailScale<=0||
       c.inScatter>=1||c.outScatter>=1||c.silverLining>=1||c.inVsOut>1||c.rayFalloff<=0||c.distantFogOpacity>1)
        throw std::invalid_argument("Cloud settings outside supported range");
}
WorldCloudRows packWorldClouds(const WorldCloudView& u) {
    const auto b=worldCloudBudget(u.quality);const auto& c=u.environment.clouds;validateCloudSettings(c);
    if(!u.owner||!u.view.identity||!u.view.cameraValid||u.view.width<1||u.view.height<1||
       u.view.width>8192||u.view.height>8192)throw std::invalid_argument("Invalid cloud view");
    const auto matrix=glm::make_mat4(u.view.viewProjection.data());
    for(float v:u.view.viewProjection)if(!std::isfinite(v))throw std::invalid_argument("Invalid cloud camera");
    if(std::abs(glm::determinant(matrix))<1e-12f)throw std::invalid_argument("Singular cloud camera");
    auto inverse=glm::inverse(matrix);auto env=packWorldEnvironment(u.environment);
    WorldCloudRows p{};const unsigned w=(unsigned(u.view.width)+b.downsample-1)/b.downsample;
    const unsigned h=(unsigned(u.view.height)+b.downsample-1)/b.downsample;
    p[0]={float(u.view.width),float(u.view.height),float(w),float(h)};
    p[1]={float(b.shadowResolution),float(b.downsample),float((w+7)/8),float((h+7)/8)};
    p[2]={u.view.cameraPosition[0],u.view.cameraPosition[1],u.view.cameraPosition[2],float(u.quality)};
    const bool moon=std::max({env[7][0],env[7][1],env[7][2]})>std::max({env[1][0],env[1][1],env[1][2]});
    p[3]=env[moon?6:0];p[3][3]=b.enabled&&u.environment.enabled&&u.environment.cloudCoverage>0?1.f:0.f;
    const auto color=env[moon?7:1];
    p[4]={color[0]*c.lightIntensity/10,color[1]*c.lightIntensity/10,color[2]*c.lightIntensity/10,c.density};
    const auto sky=prepareSkyModel(u.environment.sky.turbidity,u.environment.sky.groundReflectance,
        std::max(std::asin(std::clamp(env[0][1],-1.f,1.f)),u.environment.sky.minimumSunElevation));
    auto ambient=averageSkyRadiance(sky,u.environment.sunDirection,u.environment.sky.horizonMinimum);
    const auto& s=u.environment.sky;
    const auto luma=[](auto a){return a[0]*.2126f+a[1]*.7152f+a[2]*.0722f;};
    const float scale=std::clamp(luma(s.domeColor)/std::max(luma(s.skylightColor),1e-4f),.1f,4.f);
    for(unsigned i=0;i<3;++i)ambient[i]*=s.skylightColor[i]*scale*c.ambientIntensity;
    p[5]={ambient[0],ambient[1],ambient[2],2*std::clamp(u.environment.cloudCoverage,0.f,1.f)};
    p[6]={c.bottom,c.top,c.horizonCoverage,c.shadowIntensity};
    p[7]={c.coverageScale,c.noiseScale,c.detailScale,b.stepScale};
    p[8]={float(b.shadowSteps),b.shadowStepScale,float(b.steps),c.worldShadowIntensity};
    p[9]={c.inScatter,c.outScatter,c.inVsOut,c.silverLining};
    p[10]={c.ambientOutscatter,c.rayStart,c.rayFalloff,c.distantFogOpacity};
    p[11]={c.coverageOffset[0]/10,c.coverageOffset[1]/10,c.noiseOffset[0]/10,c.noiseOffset[1]/10};
    const float unit=4096.f/float(b.shadowResolution);
    p[12]={c.detailOffset[0]/10,c.detailOffset[1]/10,
        std::floor(p[2][0]/unit)*unit,std::floor(p[2][2]/unit)*unit};
    p[13]={1,1,1,float(u.view.frame%64)};
    for(unsigned r=0;r<4;++r)for(unsigned col=0;col<4;++col){p[14+r][col]=inverse[col][r];p[18+r][col]=matrix[col][r];}
    auto z=inverse*glm::vec4(0,0,1,1);auto f=glm::normalize(glm::vec3(z)/z.w-glm::make_vec3(u.view.cameraPosition.data()));
    p[22]={f.x,f.y,f.z,4096};p[23]={p[1][2]*p[1][3],0,0,0};
    const auto model=packSkyModel(u.environment.sky,u.environment.sunDirection,u.environment.timeOfDayHours,true);
    std::copy_n(model.begin(),19,p.begin()+45);p[64]=env[0];
    for(const auto& row:p)for(float x:row)if(!std::isfinite(x))throw std::invalid_argument("Nonfinite cloud parameters");
    return p;
}
WorldCloudUniforms cloudConsumerUniforms(const WorldCloudView& u,bool resolved) {
    const auto p=packWorldClouds(u);WorldCloudUniforms out{};
    out[0]={p[12][2],p[12][3],p[6][0],p[3][3]};
    const float y=p[3][1];
    out[1]={std::abs(y)>.001f?p[3][0]/y:0,std::abs(y)>.001f?p[3][2]/y:0,p[8][3],4096};
    for(unsigned i=0;i<4;++i)out[2+i]=p[18+i];
    out[6]={p[0][0],p[0][1],resolved?1.f:0.f,1};
    out[7]=p[2];return out;
}
namespace {
unsigned hash(unsigned v){v^=v>>16;v*=2146121005u;v^=v>>15;v*=2221713035u;return v^(v>>16);}
float noise(float x,float y,float z,unsigned n,unsigned seed) {
    x*=float(n);y*=float(n);z*=float(n);const int ix=int(std::floor(x)),iy=int(std::floor(y)),iz=int(std::floor(z));
    const auto curve=[](float a){return a*a*(3-2*a);};
    const float fx=curve(x-float(ix)),fy=curve(y-float(iy)),fz=curve(z-float(iz));float total=0;
    for(unsigned k=0;k<2;++k)for(unsigned j=0;j<2;++j)for(unsigned i=0;i<2;++i){
        const unsigned key=(unsigned(ix)+i)%n+((unsigned(iy)+j)%n)*131u+((unsigned(iz)+k)%n)*17161u+seed;
        total+=float(hash(key)&65535u)/65535.f*(i?fx:1-fx)*(j?fy:1-fy)*(k?fz:1-fz);
    }return total;
}
unsigned char byte(float x){return static_cast<unsigned char>(std::lround(std::clamp(x,0.f,1.f)*255));}
}
WorldCloudAssets::WorldCloudAssets():weather(256*256*4),noise(512*512*4) {
    for(unsigned y=0;y<256;++y)for(unsigned x=0;x<256;++x){
        const float u=float(x)/256,v=float(y)/256;auto at=(y*256+x)*4;
        weather[at]=byte(.25f+.75f*engine::render::noise(u,v,0,4,417));
        weather[at+1]=byte(.3f+.7f*engine::render::noise(u,v,0,8,1143));
        weather[at+2]=byte(.6f+.4f*engine::render::noise(u,v,0,4,771));
        weather[at+3]=byte(.4f+.6f*engine::render::noise(u,v,0,8,9001));
    }
    for(unsigned z=0;z<64;++z)for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x){
        const unsigned px=(z%8)*64+x,py=((8-z/8)%8)*64+y,at=(py*512+px)*4;
        const float u=float(x)/64,v=float(y)/64,w=float(z)/64;
        for(unsigned c=0;c<3;++c)noise[at+c]=byte(engine::render::noise(u,v,w,4u<<c,1177u+c*271));
        noise[at+3]=byte(.25f+.75f*engine::render::noise(u,v,w,4,513));
    }
}
}
