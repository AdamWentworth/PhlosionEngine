layout(std430,set=7,binding=0) readonly buffer WorldWindField {vec4 worldWindWords[];};
#define WIND_WORD(i) worldWindWords[i]
#include "world_wind_sample.glsl"
#undef WIND_WORD
