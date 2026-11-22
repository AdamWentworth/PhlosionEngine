vec4 worldWindSample(vec3 position,float seconds){
    if(seconds<0.0||WIND_WORD(0).w!=7391.0)return vec4(0,0,0,0);
    bool previous=abs(seconds-WIND_WORD(5).x)<abs(seconds-WIND_WORD(4).x);
    vec3 origin=WIND_WORD(previous?1:0).xyz;
    vec3 uv=(position-origin)/32.0+.5;
    if(any(lessThan(uv,vec3(0,0,0)))||any(greaterThan(uv,vec3(1,1,1))))return vec4(0,0,0,0);
    vec3 p=uv*64.0-.5;ivec3 cell=ivec3(floor(p));vec3 f=fract(p);vec4 value=vec4(0,0,0,0);
    uint base=64u+(previous?262144u:0u);
    for(int z=0;z<2;++z)for(int y=0;y<2;++y)for(int x=0;x<2;++x){
        ivec3 q=clamp(cell+ivec3(x,y,z),ivec3(0,0,0),ivec3(63,63,63));
        float weight=(x==0?1-f.x:f.x)*(y==0?1-f.y:f.y)*(z==0?1-f.z:f.z);
        value+=WIND_WORD(base+uint(q.x+q.y*64+q.z*4096))*weight;
    }
    return value;
}
float worldWindShelter(vec3 position,float seconds){
    if(seconds<0.0||WIND_WORD(0).w!=7391.0)return 1.0;
    bool previous=abs(seconds-WIND_WORD(5).x)<abs(seconds-WIND_WORD(4).x);
    vec3 velocity=WIND_WORD(previous?3:2).xyz;float speed=length(velocity);
    if(speed<1e-6)return 1.0;vec3 ray=-velocity/speed;float exposure=1.0;
    int count=int(WIND_WORD(previous?5:4).z),base=previous?32:8;
    for(int i=0;i<count;++i){
        vec4 low=WIND_WORD(base+i*2),high=WIND_WORD(base+i*2+1);
        float nearT=0.0,farT=high.w;bool hit=true;
        for(int axis=0;axis<3;++axis){
            if(abs(ray[axis])<1e-6){if(position[axis]<low[axis]||position[axis]>high[axis])hit=false;}
            else{
                float a=(low[axis]-position[axis])/ray[axis],b=(high[axis]-position[axis])/ray[axis];
                nearT=max(nearT,min(a,b));farT=min(farT,max(a,b));
            }
        }
        if(hit&&nearT<=farT)exposure=min(exposure,mix(low.w,1.0,smoothstep(0.0,max(high.w,.001),nearT)));
    }
    return exposure;
}
vec3 worldWindBend(vec3 position,float height,float inverseHeight,float seconds){
    vec4 impulse=worldWindSample(position,seconds);
    vec3 direction=impulse.xyz;direction.y*=.5;
    float speed=length(direction);if(speed<1e-4)return vec3(0,0,0);
    float amount=clamp(impulse.w/1.6,0.0,1.0);
    return direction/speed*(.4*max(height,0.0)*clamp(height*inverseHeight,0.0,1.0)*amount);
}
