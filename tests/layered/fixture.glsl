void computeFixture(uint i,out vec4 first,out vec4 second) {
    WorldLayerAccumulation a=worldLayerBegin();
    WorldLayerParameters p;
    p.tintOpacity=vec4(.6,.8,1.2,.5);
    p.roughnessRemap=vec4(2.0,-.25,.8,.1);
    p.metalnessRemap=vec4(.8,.1,1.25,-.2);
    p.colorMaskRemap=vec4(1.5,-.1,.75,.2);
    p.contrastInverse=.25+float(i%4u)*.5;
    p.borderStrength=(i&4u)!=0u?-1.5:.7;
    p.normalStrength=1.1;
    WorldLayerSamples s;
    s.color=vec3(.7,.4,.2);s.normalXY=vec2(.2,-.3);
    s.microblend=vec4(.3,-.4,.86,float(i%5u)/4.0);
    s.roughness=float(i%8u)/7.0;s.metalness=.65;
    if(i<28u)worldLayerAccumulate(a,p,s,float(i%4u)/3.0);
    p.tintOpacity=vec4(.8,.5,.3,1.0);
    p.contrastInverse=1.0;p.borderStrength=.4;p.normalStrength=.8;
    s.color=vec3(.2,.5,.7);s.normalXY=vec2(-.1,.15);
    s.roughness=.8;s.metalness=.1;s.microblend=vec4(-.2,.1,.97,.5);
    worldLayerAccumulate(a,p,s,1.0);
    vec3 n=worldLayerFinishNormal(a,vec3(.1,.2,sqrt(.95)),i<28u);
    first=vec4(a.color,a.roughness);second=vec4(n,a.metalness);
}
