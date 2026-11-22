void computeFixture(uint i,out vec4 a,out vec4 b) {
    float h=float(i%4u)*.5,d=float((i/4u)%4u)*100.0;
    vec3 tint=i<16u?vec3(.2,.4,.8):vec3(-1.0,-1.0,-1.0);
    vec3 color=worldFoliageTint(vec3(.8,.4,.2),tint,.5,vec2(.6,.25));
    vec3 normal=vec3(0.0,0.0,1.0);
    float transmission=.5;
    worldFoliageTerrainBlend(color,normal,transmission,vec3(.1,.2,.3),
        vec3(0.0,1.0,0.0),.04,h,1.0,d,300.0);
    a=vec4(color,transmission);b=vec4(normal,worldFoliageDisplacedPosition(vec3(3.0,4.0,10.0),vec3(0.0,0.0,0.0),vec3(0.0,0.0,1.0),float(i%2u),-2.0).x);
}
