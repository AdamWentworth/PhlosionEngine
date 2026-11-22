#pragma once
#include "engine/render/IWorldContactVisibility.h"
#include "engine/render/WorldEnvironmentLighting.h"
#include "engine/render/IWorldLocalShadow.h"
#include <algorithm>
#include <vector>
namespace engine::render {
struct WorldClusteredLightingDiagnostic {int width=0,height=0;std::vector<std::uint32_t> words;};
struct WorldClusteredLightingView {
    WorldEnvironmentLighting environment;
    std::array<float,3> camera{},forward{0,0,-1};
};
using WorldClusteredLightingUniforms=std::array<std::array<float,4 >,264>;
using WorldClusteredConsumerUniforms=std::array<std::array<float,4>,5>;
struct WorldClusteredLayout {
    unsigned x,y,coarseX,coarseY,fineOffset=2,coarseOffset,listOffset,words;
    explicit WorldClusteredLayout(int width,int height) {
        if(width<1||height<1||width>8192||height>8192)throw std::invalid_argument("Invalid clustered extent");
        x=(width+15)/16;y=(height+15)/16;coarseX=(x+3)/4;coarseY=(y+3)/4;
        coarseOffset=fineOffset+x*y*32;listOffset=coarseOffset+coarseX*coarseY*8;
        words=listOffset+2*x*y;
    }
};
inline WorldClusteredLightingUniforms packWorldClusteredLighting(const WorldContactUniforms& surface,
        const WorldClusteredLightingView& view,const WorldLocalShadowUniforms& shadows) {
    WorldClusteredLayout layout{int(surface[8][0]),int(surface[8][1])};
    WorldClusteredLightingUniforms p{};
    std::copy_n(surface.begin(),8,p.begin());
    p[8]={surface[8][0],surface[8][1],float(layout.x),float(layout.y)};
    float length=0;
    for(int i=0;i<3;++i) {
        if(!std::isfinite(view.camera[i])||!std::isfinite(view.forward[i]))throw std::invalid_argument("Invalid clustered camera");
        p[9][i]=view.camera[i];p[10][i]=view.forward[i];length+=view.forward[i]*view.forward[i];
    }
    if(std::abs(length-1.f)>.002f)throw std::invalid_argument("Non-unit clustered camera");
    p[11]={0,0,0,view.environment.enabled?1.f:0.f};
    // Bound the uploaded inverse's error against a double-precision inversion.
    // xyz of row 11 and w of row 10 hold homogeneous-coordinate error bounds.
    glm::dmat4 matrix(0);
    for(int row=0;row<4;++row)for(int col=0;col<4;++col)matrix[col][row]=surface[row][col];
    const auto inverse=glm::inverse(matrix);
    for(int row=0;row<4;++row) {
        double error=0;
        for(int col=0;col<4;++col)error+=std::abs(double(surface[4+row][col])-inverse[col][row]);
        if(!std::isfinite(error))throw std::invalid_argument("Invalid clustered inverse");
        (row<3?p[11][row]:p[10][3])=std::nextafter(float(error),INFINITY);
    }
    const auto points=packWorldPointLights(view.environment);
    const auto rects=packWorldRectLights(view.environment);
    std::copy(points.begin(),points.end(),p.begin()+12);std::copy(rects.begin(),rects.end(),p.begin()+32);
    std::copy(shadows.begin(),shadows.end(),p.begin()+144);
    for(unsigned i=0;i<worldLocalShadowSourceCapacity;++i)if(shadows[i*6+4][0]>0 && shadows[i*6+4][3]>0)p[9][3]=1;
    return p;
}
}
