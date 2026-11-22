#pragma once
#include "WorldTemporalScene.h"
#include <string>
#include <vector>
namespace engine::render {
struct WorldHeatHazeSettings {
    float strength=0,startDistance=5,maxDistance=600,patternScale=1,movementSpeed=1,seconds=0;
    bool operator==(const WorldHeatHazeSettings&) const = default;
};
struct WorldHeatHazeNoise {
    std::string key;
    int width=0,height=0,layers=0;
    // Linear RGBA8 array slices stacked vertically. Slice selection is discrete;
    // bilinear wrap never crosses into a neighbouring layer.
    std::vector<unsigned char> rgba;
    void validate() const {
        if(key.empty()||key.size()>256||width<1||height<1||layers<1||
           width>128||height>128||layers>128||
           rgba.size()!=std::size_t(width)*height*layers*4)
            throw std::invalid_argument("Invalid heat-haze noise");
    }
};
inline void validateWorldHeatHaze(const WorldHeatHazeSettings& s) {
    for(float v:{s.strength,s.startDistance,s.maxDistance,s.patternScale,s.movementSpeed,s.seconds})
        if(!std::isfinite(v))throw std::invalid_argument("Nonfinite heat haze");
    if(s.strength<0||s.strength>16||s.startDistance<0||s.maxDistance<=s.startDistance||
       s.patternScale<0||s.patternScale>1000||std::abs(s.movementSpeed)>1000||s.seconds<0)
        throw std::invalid_argument("Invalid heat haze");
}
inline std::array<float,32> worldHeatHazeParameters(const WorldHeatHazeSettings& s,
    const WorldTemporalView& view,const WorldHeatHazeNoise& noise) {
    validateWorldHeatHaze(s);noise.validate();
    if(!view.cameraValid||view.width<1||view.height<1)
        throw std::invalid_argument("Heat haze needs the primary camera");
    (void)worldTemporalCameraConstants(view,{},false);
    auto inverse=glm::inverse(glm::make_mat4(view.viewProjection.data()));
    // Noise uses a horizontal XY plane and a vertical slice coordinate.
    glm::mat4 basis(1);basis[1]={0,0,1,0};basis[2]={0,-1,0,0};inverse=basis*inverse;
    std::array<float,32> out{};
    for(int row=0;row<4;++row)for(int col=0;col<4;++col)out[row*4+col]=inverse[col][row];
    const auto camera=basis*glm::vec4(glm::make_vec3(view.cameraPosition.data()),1);
    out[16]=camera.x;out[17]=camera.y;out[18]=camera.z;out[19]=s.seconds;
    out[20]=s.strength;out[21]=s.startDistance;out[22]=s.maxDistance;
    out[23]=1/(std::max(s.patternScale,.001f)*10);
    out[24]=30*s.movementSpeed*std::max(s.patternScale,.001f);
    out[25]=float(noise.width);out[26]=float(noise.height);out[27]=float(noise.layers);
    out[28]=float(view.width);out[29]=float(view.height);
    return out;
}
}
