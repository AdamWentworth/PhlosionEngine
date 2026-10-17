#pragma once
namespace engine::render {
inline constexpr const char* screenFeedbackGlVs=R"GLSL(#version 330 core
out vec2 textureUv;
void main() { vec2 p=vec2((gl_VertexID==1)?3.0:-1.0,(gl_VertexID==2)?-3.0:1.0); gl_Position=vec4(p,0,1); textureUv=vec2(p.x*.5+.5,.5-p.y*.5); }
)GLSL";
inline constexpr const char* screenFeedbackGlFs=R"GLSL(#version 330 core
in vec2 textureUv; out vec4 outColor;
uniform sampler2D sceneImage,historyImage,maskImage;
uniform vec4 control; uniform vec4 passes[12]; uniform vec4 scenePost;
vec4 fetchHistory(ivec2 p) { return texelFetch(historyImage,ivec2(p.x,int(control.y)-1-p.y),0); }
vec4 sampleScene(vec2 uv) { ivec2 size=textureSize(sceneImage,0); ivec2 p=clamp(ivec2(floor(uv*vec2(size))),ivec2(0),size-1); return texelFetch(sceneImage,ivec2(p.x,size.y-1-p.y),0); }
float sampleMask(vec2 uv) { return texelFetch(maskImage,clamp(ivec2(floor(uv*vec2(textureSize(maskImage,0)))),ivec2(0),textureSize(maskImage,0)-1),0).a; }
#define SCENE_SIZE vec2(textureSize(sceneImage,0))
#define SCENE_SAMPLE(uv) sampleScene(uv)
#define SCENE_EXPOSURE scenePost.y
__PHLOSION_SCENE_BLOOM__
vec3 encodeScene(vec3 c) {
    c=clamp(c,vec3(0.0),vec3(1.0));
    return mix(1.055*pow(c,vec3(1.0/2.4))-0.055,12.92*c,lessThanEqual(c,vec3(0.0031308)));
}
vec3 historyByte(ivec2 p) {
    return floor(fetchHistory(clamp(p,ivec2(0),ivec2(control.xy)-1)).rgb*255.0+0.5);
}
vec3 sampleHistory(vec2 uv) {
    vec2 p=floor(max(uv,vec2(0))*control.xy*128.0)/128.0-0.5;
    ivec2 b=ivec2(floor(p)); vec2 f=fract(p);
    return floor(mix(mix(historyByte(b),historyByte(b+ivec2(1,0)),f.x),
                     mix(historyByte(b+ivec2(0,1)),historyByte(b+ivec2(1,1)),f.x),f.y)+0.5);
}
vec3 sceneByte(vec2 uv) {
    vec3 color=sampleScene(uv).rgb;
    if (scenePost.y>0.0) color=sceneDisplayMap(sceneBloom(uv,color,scenePost.x));
    return floor(sceneDitherDisplay(encodeScene(color),uv)*255.0+0.5);
}
vec3 composed(vec2 p) {
    vec3 dest=sceneByte(p/control.xy);
    for(int i=0;i<int(control.w);++i) {
        vec4 r=passes[i*3],uv=passes[i*3+1],option=passes[i*3+2];
        if(p.x<r.x || p.y<r.y || p.x>=r.z || p.y>=r.w) continue;
        vec2 t=(p-r.xy)/(r.zw-r.xy);
        vec3 c=option.x==2.0 ? vec3(0) : sampleHistory(mix(uv.xy,uv.zw,t));
        float a=option.y/255.0;
        if(option.x==1.0) { float m=sampleMask(t); c*=m>128.0/255.0?1.0:0.0; a*=m; }
        dest=floor(dest*(1.0-a)+c*a+0.5);
    }
    return dest;
}
vec4 feedbackOutput(vec2 uv) {
    if(control.z==0.0) return vec4(sceneByte(uv)/255.0,1);
    if(control.z==1.0) {
        vec2 b=floor(uv*control.xy);
        vec3 sum=composed(b+vec2(.25,.25))+composed(b+vec2(.75,.25))+
                 composed(b+vec2(.25,.75))+composed(b+vec2(.75,.75));
        return vec4(floor(sum*.25)/255.0,1);
    }
    return vec4(composed((floor(uv*control.xy*2.0)+.5)*.5)/255.0,1);
}

void main(){outColor=feedbackOutput(textureUv);}
)GLSL";
}
namespace engine::render {
inline constexpr const char* screenFeedbackHlslFs=R"HLSL(Texture2D<float4> sceneImage:register(t0); Texture2D<float4> historyImage:register(t1); Texture2D<float4> maskImage:register(t2);
SamplerState sceneSampler:register(s0);
cbuffer Feedback:register(b0) { float4 control; float4 passes[12]; float4 scenePost; };
float4 fetchHistory(int2 p) { return historyImage.Load(int3(p,0)); }
float4 sampleScene(float2 uv) { uint w,h;sceneImage.GetDimensions(w,h);return sceneImage.Load(int3(clamp(int2(floor(uv*float2(w,h))),int2(0,0),int2(w,h)-1),0)); }
float sampleMask(float2 uv) { uint w,h; maskImage.GetDimensions(w,h); return maskImage.Load(int3(clamp(int2(floor(uv*float2(w,h))),int2(0,0),int2(w,h)-1),0)).a; }
float2 feedbackSceneSize() { uint w,h;sceneImage.GetDimensions(w,h);return float2(w,h); }
#define SCENE_SIZE feedbackSceneSize()
#define SCENE_SAMPLE(uv) sampleScene(uv)
#define SCENE_EXPOSURE scenePost.y
__PHLOSION_SCENE_BLOOM__
float3 encodeScene(float3 c) {
    c=clamp(c,float3(0.0,0.0,0.0),float3(1.0,1.0,1.0));
    return float3(c.x<=0.0031308?12.92*c.x:1.055*pow(c.x,1.0/2.4)-0.055,c.y<=0.0031308?12.92*c.y:1.055*pow(c.y,1.0/2.4)-0.055,c.z<=0.0031308?12.92*c.z:1.055*pow(c.z,1.0/2.4)-0.055);
}
float3 historyByte(int2 p) {
    return floor(fetchHistory(clamp(p,int2(0,0),int2(control.xy)-1)).rgb*255.0+0.5);
}
float3 sampleHistory(float2 uv) {
    float2 p=floor(max(uv,float2(0,0))*control.xy*128.0)/128.0-0.5;
    int2 b=int2(floor(p)); float2 f=frac(p);
    return floor(lerp(lerp(historyByte(b),historyByte(b+int2(1,0)),f.x),
                     lerp(historyByte(b+int2(0,1)),historyByte(b+int2(1,1)),f.x),f.y)+0.5);
}
float3 sceneByte(float2 uv) {
    float3 color=sampleScene(uv).rgb;
    if (scenePost.y>0.0) color=sceneDisplayMap(sceneBloom(uv,color,scenePost.x));
    return floor(sceneDitherDisplay(encodeScene(color),uv)*255.0+0.5);
}
float3 composed(float2 p) {
    float3 dest=sceneByte(p/control.xy);
    for(int i=0;i<int(control.w);++i) {
        float4 r=passes[i*3],uv=passes[i*3+1],option=passes[i*3+2];
        if(p.x<r.x || p.y<r.y || p.x>=r.z || p.y>=r.w) continue;
        float2 t=(p-r.xy)/(r.zw-r.xy);
        float3 c=option.x==2.0 ? float3(0,0,0) : sampleHistory(lerp(uv.xy,uv.zw,t));
        float a=option.y/255.0;
        if(option.x==1.0) { float m=sampleMask(t); c*=m>128.0/255.0?1.0:0.0; a*=m; }
        dest=floor(dest*(1.0-a)+c*a+0.5);
    }
    return dest;
}
float4 feedbackOutput(float2 uv) {
    if(control.z==0.0) return float4(sceneByte(uv)/255.0,1);
    if(control.z==1.0) {
        float2 b=floor(uv*control.xy);
        float3 sum=composed(b+float2(.25,.25))+composed(b+float2(.75,.25))+
                 composed(b+float2(.25,.75))+composed(b+float2(.75,.75));
        return float4(floor(sum*.25)/255.0,1);
    }
    return float4(composed((floor(uv*control.xy*2.0)+.5)*.5)/255.0,1);
}

float4 main(float4 position:SV_POSITION,float2 uv:TEXCOORD0):SV_TARGET {return feedbackOutput(uv);}
)HLSL";
}
