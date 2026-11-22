void computeFixture(uint i,out vec4 a,out vec4 b) {
    float angle=float(i%8u)*.39269908169872414;
    float scale=i<16u?1.0:1.7;
    float coverage;
    vec3 p=worldFoliageBillboardPosition(vec3(0.0,1.0,.5),vec3(1.0,0.0,0.0),
        vec4(0.0,1.0,0.0,-1.0),vec3(scale,0.0,0.0),vec3(0.0,0.0,-scale),
        vec3(0.0,scale,0.0),vec3(0.0,0.0,0.0),
        vec3(cos(angle),0.0,-sin(angle)),-vec3(cos(angle),0.0,-sin(angle)),
        0.0,2.0,coverage,8.0);
    float pixel=float(i%4u);
    float cutoff=worldFoliageCutoff(vec2(4.0,0.0),vec2(0.0,4.0),9.0,.3,.15,vec2(pixel,.5),float(i%4u));
    float depth=worldFoliageProjectedDepth(.8,-2.0,vec3(1.02,-2.02,1.0));
    float alpha=worldFoliageBillboardAlpha(.8,.5,vec2(pixel,0.0));
    a=vec4(p,coverage);b=vec4(cutoff,depth,alpha,
        worldFoliageProjectedDepth(.7,-2.0,vec3(.02,0.0,0.0)));
}
