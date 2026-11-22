#if defined(WORLD_FOG_VIEW) && defined(WORLD_FOG_SKY_ATLAS)
vec3 distantFogSkyLevel(vec2 uv,int level) {
    float width=128.0/exp2(float(level));
    float origin=level==1?0.0:level==2?64.0:level==3?96.0:112.0;
    return WORLD_FOG_SKY_ATLAS((vec2(origin,0)+clamp(uv*width,vec2(.5,.5),vec2(width-.5,width-.5)))/vec2(128,64));
}
vec4 distantFogAt(vec3 position) {
    if(WORLD_FOG_VIEW(12).w<=0.0)return vec4(0,0,0,1);
    vec3 delta=position-WORLD_FOG_VIEW(3).xyz;
    float distance=length(delta),start=WORLD_FOG_VIEW(11).w;
    if(distance<=start)return vec4(0,0,0,1);
    float segment=distance-start,dy=delta.y*segment/distance;
    float height=WORLD_FOG_VIEW(3).y+delta.y*start/distance,integral=0.0;
    for(int i=0;i<2;++i) {
        vec3 layer=WORLD_FOG_VIEW(11+i).xyz;
        float x=max(-127.0,layer.z*dy);
        float average=abs(x)>.01?(1.0-exp2(-x))/x:.69314718056-.24022650696*x;
        integral+=layer.x*exp2(-layer.z*(height-layer.y))*average*segment;
    }
    float transmission=clamp(exp2(-integral),0.0,1.0);
    // Comparisons also reject NaN without API-specific finite intrinsics.
    if(!(transmission>=0.0&&transmission<=1.0))transmission=0.0;
    transmission=mix(1.0,transmission,WORLD_FOG_VIEW(12).w);
    float horizontal=length(delta.xz);
    float blend=clamp(horizontal*WORLD_FOG_VIEW(10).w+WORLD_FOG_VIEW(9).w,0.0,1.0);
    vec3 direction=normalize(vec3(delta.x*blend/max(horizontal,.0001),
        mix(.2,1.0,(1.0-blend)*(1.0-blend)),delta.z*blend/max(horizontal,.0001)));
    vec2 uv=-.5*direction.xz/(1.0+direction.y)+.5;
    float lod=4.0-3.0*blend;int lo=int(floor(lod)),hi=min(lo+1,4);
    vec3 sky=mix(distantFogSkyLevel(uv,lo),distantFogSkyLevel(uv,hi),lod-float(lo));
    vec3 albedo=mix(WORLD_FOG_VIEW(9).rgb,WORLD_FOG_VIEW(10).rgb,blend*blend);
    return vec4(sky*albedo*(1.0-transmission),transmission);
}
#endif
// Scene-linear integrated fog; all consumers use the same per-view volumes.
vec4 worldFogAt(vec3 position) {
#ifdef WORLD_FOG_VIEW
    if(WORLD_FOG_VIEW(2).z<.5)return vec4(0,0,0,1);
    vec4 fogPoint=vec4(position,1);
    vec4 h=vec4(dot(WORLD_FOG_VIEW(5),fogPoint),dot(WORLD_FOG_VIEW(6),fogPoint),dot(WORLD_FOG_VIEW(7),fogPoint),dot(WORLD_FOG_VIEW(8),fogPoint));
    if(h.w<=0.0)return vec4(0,0,0,1);
    vec2 uv=h.xy/h.w*vec2(.5,-.5)+vec2(.5,.5);
    if(min(uv.x,uv.y)<0.0||max(uv.x,uv.y)>1.0)return vec4(0,0,0,1);
    float depth=max(dot(position-WORLD_FOG_VIEW(3).xyz,WORLD_FOG_VIEW(4).xyz),0.0);
    vec4 total=vec4(0,0,0,1);
    for(int fogDomain=0;fogDomain<2;++fogDomain) {
        if(fogDomain==1&&depth<=WORLD_FOG_VIEW(2).x)break;
        vec4 dim=WORLD_FOG_VIEW(fogDomain);
        if(min(dim.x,min(dim.y,dim.z))<=0.0)continue;
        float fogDepth=fogDomain==0?depth:(depth-WORLD_FOG_VIEW(2).x)/20.0;
        float z=clamp(pow(max(fogDepth/WORLD_FOG_VIEW(2).x,0.0),WORLD_FOG_VIEW(2).y)*dim.z-.5,0.0,dim.z-1.0);
        float a=floor(z),b=min(a+1.0,dim.z-1.0);
        vec2 xy=clamp(uv*dim.xy,vec2(.5,.5),dim.xy-vec2(.5,.5));
        vec2 atlas=dim.xy*vec2(8,ceil(dim.z/8.0));
        vec2 first=(xy+vec2(a-floor(a/8.0)*8.0,floor(a/8.0))*dim.xy)/atlas;
        vec2 second=(xy+vec2(b-floor(b/8.0)*8.0,floor(b/8.0))*dim.xy)/atlas;
        vec4 sampleValue=mix(WORLD_FOG_SAMPLE(first,fogDomain==1),WORLD_FOG_SAMPLE(second,fogDomain==1),z-a);
        total.rgb+=total.a*sampleValue.rgb;total.a*=sampleValue.a;
    }
#ifdef WORLD_FOG_SKY_ATLAS
    vec4 tail=distantFogAt(position);
    total.rgb+=total.a*tail.rgb;total.a*=tail.a;
#endif
    return total;
#else
    return vec4(0,0,0,1);
#endif
}
