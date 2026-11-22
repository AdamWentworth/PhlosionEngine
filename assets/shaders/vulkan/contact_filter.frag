#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_control_flow_attributes : require
layout(set=1,binding=0) uniform ContactView {
    layout(offset=12304) vec4 contact[24];
} viewState;
layout(set=3,binding=0) uniform sampler2D currentDepth;
layout(set=6,binding=0) uniform sampler2D currentVisibility;
layout(set=6,binding=1) uniform sampler2D previousVisibility;
layout(set=6,binding=2) uniform sampler2D auxiliary;
layout(location=0) out vec4 outColor;
#define CONTACT(index) viewState.contact[index]
#define CONTACT_LOOP [[dont_unroll]]
#define CONTACT_DEPTH(p) texelFetch(currentDepth,ivec2(p),0).r
#define CONTACT_BUFFER_FETCH(p) texelFetch(currentVisibility,ivec2(p),0)
#define CONTACT_PREPARED_FETCH(p) texelFetch(previousVisibility,ivec2(p),0)
#define CONTACT_HISTORY_SAMPLE(uv) textureLod(previousVisibility,uv,0.0)
#define CONTACT_AUX_FETCH(p) texelFetch(auxiliary,ivec2(p),0)
#define ENV_SUN vec4(0.0,0.0,0.0,1.0)
#include "world_contact.glsl"
void main() { outColor=worldContactFilter(gl_FragCoord.xy); }
