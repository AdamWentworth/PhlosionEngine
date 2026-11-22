void computeFixture(uint i,out vec4 a,out vec4 b) {
    bool front=(i&1u)!=0u;
    float handed=(i&2u)!=0u?-1.0:1.0;
    float z=(i&4u)!=0u?-.866025403784:.866025403784;
    vec3 mapped=worldFoliageNormal(vec3(0.0,0.0,1.0),vec4(1.0,0.0,0.0,handed),
        .5+.5*vec3(.3,.4,z),front);
    float angle=float(i)*.196349540849362;
    vec3 light=vec3(sin(angle),0.0,cos(angle)),view=vec3(0.0,0.0,1.0);
    float roughness=.08+.92*float(i%8u)/7.0,transmission=float(i%3u)*.5;
    float angular=i%4u==0u?0.0:(i%4u==1u?.00466875575:(i%4u==2u?.006975:.0105));
    FiniteLightResponse r=worldFoliageDirectional(vec3(0.0,0.0,1.0),view,light,
        angular,vec3(.4,.2,.1),roughness,transmission);
    a=vec4(mapped,worldFoliageTransmission(dot(view,light),max(light.z,0.0),roughness,transmission));
    b=vec4(r.diffuse,r.specular.x);
}