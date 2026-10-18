// Synthetic inputs only. Keep the same fixture body on all three shader APIs.
void computeFixture(uint index,out vec4 a,out vec4 b) {
    if(index>=16u) {
        vec3 delta=vec3(.4,3,.6),u=vec3(1.2,0,0),v=vec3(0,0,.06);
        vec3 view=normalize(vec3(.1,1,2)),normal=vec3(0,1,0),radiance=vec3(10,4,2),f0=vec3(.04,.04,.04);
        float roughness=.5,metallic=0.0;
        if(index==17u) radiance=vec3(0,0,0);
        if(index==18u) v=-v;
        if(index==19u) {delta=vec3(-.6,3,.4);u=vec3(0,0,1.2);v=vec3(-.06,0,0);view=normalize(vec3(-2,1,.1));}
        if(index==20u) radiance*=2.0;
        if(index==21u) f0=vec3(.16,.16,.16);
        if(index==22u) u=vec3(.06,0,0);
        if(index==23u) delta=vec3(0,0,0);
        if(index==24u) {normal=vec3(1,0,0);view=normal;roughness=1.0;}
        if(index==25u) roughness=0.0;
        if(index==26u) roughness=.04;
        if(index==27u) {normal=vec3(0,-1,0);view=normal;}
        if(index==28u) delta=vec3(1.14,.00001,0);
        if(index==29u) u=vec3(.06000001,0,0);
        if(index==30u) view=-normalize(delta);
        if(index==31u) {metallic=1.0;f0=vec3(.4,.2,.1);}
        FiniteLightResponse r=finiteLightRectangleResponse(delta,u,v,12.0,radiance,normal,view,
            vec3(.4,.2,.1),f0,roughness,metallic);
        a=vec4(r.diffuse,0);b=vec4(r.specular,1);return;
    }
    vec3 normal=vec3(0,1,0),view=normalize(vec3(.1,1,2));
    vec3 delta=index==8u?vec3(3,4,2):vec3(.4,3,.6);
    vec3 halfAxis=index==7u?vec3(0,0,0):vec3(1.2,0,0);
    float roughness=index%3u==0u?.04:index%3u==1u?.5:1.0;
    float metallic=index==6u?1.0:index==9u?.5:0.0;
    bool extended=index>=3u&&index<=10u;
    if(index==11u) normal=-normal;
    FiniteLightResponse response=finiteLightResponse(normal,view,delta,halfAxis,
        index==10u?0.0:.06,index==7u?0.0:2.4,roughness,vec3(.4,.2,.1),metallic,extended);
    a=vec4(response.diffuse,finiteLightDistanceSquared(delta,halfAxis,extended));
    b=vec4(response.specular,1);
    if(index==12u) {
        vec3 intensity=vec3(10,4,2);
        a=vec4(finiteLightFalloff(9,1.0/12.0,true,false,1,intensity),
               finiteLightFalloff(9,1.0/12.0,false,false,1,intensity),
               finiteLightFalloff(9,1.0/12.0,true,true,.02,intensity),
               finiteLightFalloff(144,1.0/12.0,true,false,1,intensity));
        b=vec4(finiteLightFalloff(9,1.0/12.0,true,false,1,vec3(.001,.001,.001)),0,0,1);
    }
    if(index==13u) {
        a=vec4(finiteLightMomentVisibility(vec2(.3,.1),.2,1),
               finiteLightMomentVisibility(vec2(.3,.1),.5,1),
               finiteLightMomentVisibility(vec2(.3,.1),.5,.25),
               finiteLightMomentVisibility(vec2(.3,.09),.9,1));
        b=vec4(finiteLightMomentVisibility(vec2(.3,.09),.9,0),0,0,1);
    }
    if(index==14u) {
        vec3 intensity=vec3(10,4,2);
        a=vec4(finiteLightFalloff(100,1.0/12.0,true,false,1,intensity),
               finiteLightFalloff(143,1.0/12.0,true,false,1,intensity),
               finiteLightFalloff(144,1.0/12.0,true,false,1,intensity),
               finiteLightFalloff(169,1.0/12.0,true,false,1,intensity));
        b=vec4(0,0,0,1);
    }
    if(index==15u) {
        a=vec4(finiteLightDistanceSquared(vec3(.6,.4,3),vec3(0,1.2,0),true),
               finiteLightDistanceSquared(vec3(.4,3,.6),vec3(-1.2,0,0),true),
               finiteLightDistanceSquared(vec3(.4,3,.6),vec3(0,0,0),false),0);
        b=vec4(0,0,0,1);
    }
}
