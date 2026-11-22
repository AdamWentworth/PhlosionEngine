#include "engine/render/WorldDeformation.h"
extern "C" __declspec(dllexport) void makeDeformation(engine::render::WorldDeformation* output) {
    engine::render::WorldDeformationSet producer;
    float values[16]{};values[0]=42;
    *output=producer.publish(0,values);
}
