void computeFixture(uint i,out vec4 a,out vec4 b) {
    float distance=2.0+float(i%4u),roughness=.08+.92*float(i%8u)/7.0;
    float metal=(i%3u)==0u?1.0:0.0;
    vec3 albedo=vec3(.4,.2,.1),f0=vec3(.04,.04,.04)*(1.0-metal)+albedo*metal;
    bool transparent=i>=16u;
    vec4 axis=vec4(0.0,-1.0,0.0,1.0+((i&1u)==0u?2.0:0.0));
    vec4 shape=vec4(0.0,0.0,(i%5u)==0u?.7:1.0,(i%7u)==0u?.03:0.0);
    vec4 angular=vec4(1.0,0.0,1.0,1.0);
    vec3 delta=vec3(0.0,distance,0.0);
    FiniteLightResponse r=nativeLightResponse(delta,12.0,vec3(.3,2.0,.7),axis,shape,angular,
        axis.xyz,false,vec3(0.0,1.0,0.0),vec3(0.0,1.0,0.0),albedo,f0,roughness,metal,transparent);
    a=vec4(r.diffuse,nativeLightAttenuation(delta,12.0,axis,shape,angular,axis.xyz,false));
    float extra=0.0;
    if(i<8u)extra=nativeLightCone(vec3(1.0,1.0,0.0),vec3(.1*float(i),0.0,0.0),
        vec3(0.0,-1.0,0.0),vec4(.9,.2,2.0,1.0));
    else if(i<16u) {
        float edge=10.0+.4*float(i-8u);
        extra=nativeLightFalloff(edge*edge,12.0,vec4(0.0,0.0,0.0,3.0),vec4(0.0,0.0,1.0,0.0));
    }
    else if(i<24u) {
        vec4 directed=vec4(0.0,-1.0,0.0,11.0);
        extra=nativeLightResponse(vec3(1.0,distance,2.0),12.0,vec3(1.0,1.0,1.0),directed,
            vec4(0.0,0.0,1.0,0.0),angular,directed.xyz,false,vec3(0.0,1.0,0.0),
            vec3(0.0,1.0,0.0),albedo,vec3(.04,.04,.04),.5,0.0,false).diffuse.x;
    }
    else {
        float extent=.2*float(i-23u);
        extra=nativeLightAttenuation(vec3(1.0,distance,.5),12.0,vec4(1.0,0.0,0.0,3.0),
            vec4(.1,extent*2.0,1.0,0.0),angular,vec3(0.0,-1.0,0.0),true);
    }
    b=vec4(r.specular,extra);
}
