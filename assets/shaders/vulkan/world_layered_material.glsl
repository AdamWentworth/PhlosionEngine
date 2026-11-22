// Texture/mask residency and sampling belong to the caller. Samples use
// linear color and decoded tangent-space normal XY.
struct WorldLayerParameters {
    vec4 tintOpacity;
    vec4 roughnessRemap;
    vec4 metalnessRemap;
    vec4 colorMaskRemap;
    float contrastInverse;
    float borderStrength;
    float normalStrength;
};
struct WorldLayerSamples {
    vec3 color;
    vec2 normalXY;
    vec4 microblend;
    float roughness;
    float metalness;
};
struct WorldLayerAccumulation {
    vec3 color;
    vec2 normalXY;
    vec2 borderXY;
    float roughness;
    float metalness;
    float remaining;
    float borderCoverage;
    float borderWeight;
};
WorldLayerAccumulation worldLayerBegin() {
    WorldLayerAccumulation a;
    a.color=vec3(0.0,0.0,0.0);
    a.normalXY=vec2(0.0,0.0);
    a.borderXY=vec2(0.0,0.0);
    a.roughness=0.0; a.metalness=0.0; a.remaining=1.0;
    a.borderCoverage=0.0; a.borderWeight=0.0;
    return a;
}
float worldLayerRemap(float sampleValue,vec4 remap) {
    float inputValue=clamp(sampleValue*remap.x+remap.y,0.0,1.0);
    return clamp(inputValue*remap.z+remap.w,0.0,1.0);
}
float worldLayerAccumulate(inout WorldLayerAccumulation a,WorldLayerParameters p,
                           WorldLayerSamples s,float coverage) {
    float shaped=clamp(mix(1.0-s.microblend.w,coverage,p.contrastInverse),0.0,1.0);
    float edge=sqrt(max(1.0-abs(2.0*shaped-1.0),0.0))*p.tintOpacity.w;
    float border=clamp(edge-a.borderCoverage,0.0,1.0);
    float amount=shaped*p.tintOpacity.w;
    a.borderCoverage+=amount;
    a.borderXY=mix(a.borderXY,s.microblend.xy*p.borderStrength,border);
    a.borderWeight=max(a.borderWeight,border*abs(p.borderStrength));
    float contribution=min(a.remaining,amount);
    a.remaining-=contribution;
    a.color+=s.color*mix(vec3(1.0,1.0,1.0),p.tintOpacity.xyz,
                        worldLayerRemap(s.roughness,p.colorMaskRemap))*contribution;
    a.roughness+=worldLayerRemap(s.roughness,p.roughnessRemap)*contribution;
    a.metalness+=worldLayerRemap(s.metalness,p.metalnessRemap)*contribution;
    a.normalXY+=s.normalXY*(p.normalStrength*contribution);
    return contribution;
}
vec3 worldLayerReconstruct(vec2 xy) {
    return vec3(xy,sqrt(max(1.0-dot(xy,xy),0.0)));
}
vec3 worldLayerDetail(vec3 base,vec3 detail) {
    vec3 tilted=vec3(base.xy,base.z+1.0);
    vec3 reflected=vec3(-detail.xy,detail.z);
    vec3 n=tilted*dot(tilted,reflected)-reflected*tilted.z;
    return n*inversesqrt(max(dot(n,n),1e-20));
}
vec3 worldLayerFinishNormal(WorldLayerAccumulation a,vec3 globalNormal,bool multiple) {
    vec3 detail=worldLayerReconstruct(a.normalXY);
    if(multiple)detail=mix(detail,worldLayerReconstruct(a.borderXY),a.borderWeight);
    return worldLayerDetail(globalNormal,detail);
}
// These terrain functions use source Z-up coordinates. The host converts at
// the boundary. Select one projection, preserving its unwrapped derivatives.
vec3 worldTerrainProjectionWeights(vec3 normal) {
    vec3 square=normal*normal;
    vec3 weights=square*square;
    return weights/max(weights.x+weights.y+weights.z,1e-20);
}
uint worldTerrainProjectionAxis(vec3 weights) {
    float largest=max(weights.x,max(weights.y,weights.z));
    return weights.y==largest?1u:(weights.z==largest?2u:0u);
}
vec2 worldTerrainPlanar(vec3 position,uint axis) {
    if(axis==1u)return vec2(-position.x,position.z)/512.0;
    if(axis==2u)return vec2(position.y,-position.x)/512.0;
    return position.yz/512.0;
}
vec3 worldTerrainLayerNormal(vec3 globalNormal,vec3 detail,float strength) {
    vec3 d=normalize(vec3(detail.xy*strength,detail.z));
    float height=abs(d.z);
    vec3 weights=worldTerrainProjectionWeights(globalNormal);
    vec3 nx=vec3(height*globalNormal.x,d.y+globalNormal.y,-d.x+globalNormal.z);
    vec3 ny=vec3(-d.x+globalNormal.x,height*globalNormal.y,-d.y+globalNormal.z);
    vec3 nz=vec3(d.y+globalNormal.x,d.x+globalNormal.y,height*globalNormal.z);
    return normalize(nx*weights.x+ny*weights.y+nz*weights.z);
}
vec2 worldLayerMaskAtlasUv(uint packed,vec2 flippedUv,vec2 resolution,
                          float tileSize,vec2 atlasSize,vec2 tileToUv) {
    uvec2 shift=uvec2((packed>>20u)&15u,(packed>>24u)&15u);
    vec2 layerSize=vec2(uvec2(resolution)>>shift);
    vec2 tile=vec2(packed&1023u,(packed>>10u)&1023u);
    return tile*tileToUv+(fract(flippedUv*layerSize/tileSize)*tileSize+vec2(1.0,1.0))/atlasSize;
}
