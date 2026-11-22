#ifdef LAYERED_HLSL
static uint fixtureIndex;
#else
uint fixtureIndex;
#endif
WorldLayerParameters worldReadLayerParameters(uint layer) {
    WorldLayerParameters p;
    p.tintOpacity=vec4(1.0,1.0,1.0,.05+float(layer%5u)*.1);
    p.roughnessRemap=vec4(1.0,0.0,1.0,0.0);
    p.metalnessRemap=p.roughnessRemap;p.colorMaskRemap=p.roughnessRemap;
    p.contrastInverse=1.0;p.borderStrength=0.0;p.normalStrength=1.0;
    return p;
}
WorldLayerSamples worldReadLayerSamples(uint layer) {
    WorldLayerSamples s;
    s.color=vec3(float(layer)/31.0,float(layer%3u)*.25,.7);
    s.normalXY=vec2(.01*float(layer),-.005*float(layer));
    s.microblend=vec4(0.0,0.0,1.0,.5);
    s.roughness=.2+float(layer%7u)*.1;s.metalness=float(layer%2u);
    return s;
}
float worldReadLayerCoverage(uint layer) {return float((layer+fixtureIndex)%4u)/3.0;}
void computeFixture(uint i,out vec4 first,out vec4 second) {
    fixtureIndex=i;
    uint bits=i==0u?0u:(i==1u?0x80000000u:(i==2u?0xffffffffu:(0xa5000081u^(1u<<i))));
    WorldLayeredResult r=worldEvaluateLayers(bits,vec3(0.0,0.0,1.0),uvec2(4u,20u));
    first=vec4(r.color,r.roughness);
    second=vec4(r.metalness,float(r.dominantLayer),r.dominantWeight,r.selectedRangeWeight);
}
