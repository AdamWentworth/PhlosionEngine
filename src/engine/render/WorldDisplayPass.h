#pragma once
#include "WorldDisplay.h"
#include "WorldTemporalScene.h"
#include <vector>
namespace engine::render {
template<class Image> struct WorldDisplayResources {
    Image meter,histogram,reduced,lut,output,heat;
    std::array<Image,2> exposure;
    std::vector<Image> down,up;
    decltype(WorldDisplaySettings::grade) grade{};
    bool lutReady=false,exposureReady=false;
    int slot=0;
    template<class F> void each(F&& f) {
        f(meter);f(histogram);f(reduced);f(lut);f(output);f(heat);
        for(auto& i:exposure)f(i);for(auto& i:down)f(i);for(auto& i:up)f(i);
    }
};
// Format classes: FP16, FP32, unsigned compact HDR. API adapters own resources,
// binding and barriers; this graph owns identical scheduling on both backends.
template<class Image,class Allocate,class Dispatch>
void runWorldDisplay(WorldDisplayResources<Image>& r,const WorldDisplaySettings& s,
    const WorldTemporalView& view,const WorldTemporalView& previous,bool history,
    float exposure,float bloom,Image& color,Image& emission,Image& depth,
    Allocate&& allocate,Dispatch&& dispatch) {
    float distance=0;
    if(history&&view.cameraValid&&previous.cameraValid)
        for(int i=0;i<3;++i)distance+=std::pow(view.cameraPosition[i]-previous.cameraPosition[i],2.f);
    auto p=worldDisplayParameters(s,view.width,view.height,view.deltaSeconds,
        history&&r.exposureReady,view.preExposure,exposure/.6f,
        std::sqrt(distance)/std::max(view.deltaSeconds,1e-6f),bloom);
    allocate(r.meter,256,256,0);allocate(r.histogram,512,64,1);allocate(r.reduced,512,1,1);
    allocate(r.lut,2304,48,0);allocate(r.output,view.width,view.height,0);
    for(auto& i:r.exposure)allocate(i,1,1,1);
    const int current=1-r.slot;
    std::array<Image*,8> inputs;inputs.fill(&color);
    inputs[1]=&emission;inputs[2]=&depth;
    // Uninitialized history must never be bound, even behind a shader condition.
    inputs[3]=p[5]>.5f?&r.exposure[r.slot]:&color;
    dispatch(0,r.meter,inputs,p);inputs[0]=&r.meter;
    dispatch(1,r.histogram,inputs,p);inputs[0]=&r.histogram;
    dispatch(8,r.reduced,inputs,p);inputs[0]=&r.reduced;
    dispatch(2,r.exposure[current],inputs,p);
    if(!r.lutReady||r.grade!=s.grade) {
        std::array<float,32> grade{};
        for(int i=0;i<8;++i)std::copy(s.grade[i].begin(),s.grade[i].end(),grade.begin()+i*4);
        // LUT generation has no sampled dependencies.
        auto lutInputs=inputs;lutInputs.fill(&color);
        dispatch(6,r.lut,lutInputs,grade);r.grade=s.grade;r.lutReady=true;
    }
    Image* bloomImage=&color;
    if(bloom>0&&s.bloomScale>0) {
        if(r.down.size()<std::size_t(s.bloomLevels))r.down.resize(s.bloomLevels);
        if(r.up.size()<std::size_t(s.bloomLevels-1))r.up.resize(s.bloomLevels-1);
        int w=worldDisplayAlignedSize(view.width,s.bloomLevels),h=worldDisplayAlignedSize(view.height,s.bloomLevels);
        int iw=view.width,ih=view.height;inputs[0]=&color;
        for(int i=0;i<s.bloomLevels;++i){
            w=std::max(1,w/2);h=std::max(1,h/2);allocate(r.down[i],w,h,2);
            p[0]=float(w);p[1]=float(h);p[2]=float(iw);p[3]=float(ih);
            dispatch(i?4:3,r.down[i],inputs,p);inputs[0]=&r.down[i];iw=w;ih=h;
        }
        for(int i=s.bloomLevels-2;i>=0;--i){
            w*=2;h*=2;allocate(r.up[i],w,h,2);
            inputs[1]=&r.down[i];p[0]=float(w);p[1]=float(h);p[2]=float(iw);p[3]=float(ih);
            dispatch(5,r.up[i],inputs,p);inputs[0]=&r.up[i];iw=w;ih=h;
        }
        bloomImage=&r.up[0];
    }
    inputs[0]=&color;inputs[1]=bloomImage;inputs[2]=&r.exposure[current];inputs[4]=&r.lut;
    p[0]=float(view.width);p[1]=float(view.height);
    p[24]=view.jitterPixels[0];p[25]=view.jitterPixels[1];
    dispatch(7,r.output,inputs,p);r.slot=current;r.exposureReady=true;
}
}
