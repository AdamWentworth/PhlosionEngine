#ifdef WORLD_SUN
#ifdef WORLD_SUN_NOISE
WORLD_SUN_PRIVATE float sunRotation;
float sunFraction(float v) {return v-floor(v);}
vec2 sunFraction(vec2 v) {return v-floor(v);}
vec3 sunFraction(vec3 v) {return v-floor(v);}
#endif
vec3 sunShadowClip(int index,vec3 position,vec3 normal) {
    int row=index*7;vec4 bias=WORLD_SUN(row+4);
    if(dot(normal,normal)>.0000001)position+=normalize(normal)*bias.z;
    vec4 clip=WORLD_SUN(row)*position.x+WORLD_SUN(row+1)*position.y+
        WORLD_SUN(row+2)*position.z+WORLD_SUN(row+3);
    return clip.xyz/clip.w;
}
bool sunShadowContains(vec3 clip) {return max(abs(clip.x),max(abs(clip.y),abs(clip.z)))<.9999;}
float sunShadowFilter(int index,vec3 clip) {
    int row=index*7;vec4 tile=WORLD_SUN(row+5),bias=WORLD_SUN(row+4);
    vec2 uv=clip.xy*vec2(.5,WORLD_SHADOW_Y*.5)+vec2(.5,.5);
    float reference=clip.z*.5+.5-bias.y;
    float radius=WORLD_SUN(row+6).z;
#ifdef WORLD_SUN_VOLUME
    return mix(1.0,WORLD_SUN_COMPARE(tile.xy+uv*tile.zw,reference),bias.x);
#else
    float sum=0.0;
    // Original stratified disk; twelve hardware bilinear comparisons.
    for(int k=0;k<12;++k) {
        float angle=float(k)*2.39996322973;
#ifdef WORLD_SUN_NOISE
        if(index<4)angle+=sunRotation*6.28318530718;
#endif
        vec2 disk=vec2(cos(angle),sin(angle))*sqrt((float(k)+.5)/12.0);
        vec2 tap=clamp(uv+disk*radius,vec2(.5,.5)*bias.w,vec2(1.0,1.0)-vec2(.5,.5)*bias.w);
        sum+=WORLD_SUN_COMPARE(tile.xy+tap*tile.zw,reference);
    }
    return mix(1.0,sum/12.0,bias.x);
#endif
}
float sunDistantVisibility(vec3 position,vec3 normal) {
    float value=1.0;int selected=-1;
    for(int k=4;k<8;++k) {
        vec3 clip=sunShadowClip(k,position,normal);
        if(sunShadowContains(clip)){value=sunShadowFilter(k,clip);selected=k-4;break;}
    }
    if(selected>=0&&WORLD_SUN(66).w>.5&&WORLD_SUN(65).z<1.0&&selected==int(WORLD_SUN(65).w)) {
        vec3 oldClip=sunShadowClip(8,position,normal);
        if(sunShadowContains(oldClip))value=mix(sunShadowFilter(8,oldClip),value,WORLD_SUN(65).z);
        else for(int k=4;k<8;++k)if(k-4!=selected) {
            vec3 clip=sunShadowClip(k,position,normal);
            if(sunShadowContains(clip)){value=mix(sunShadowFilter(k,clip),value,WORLD_SUN(65).z);break;}
        }
    }
    return value;
}
float sunCascadeVisibility(vec3 position,vec3 normal) {
#ifdef WORLD_SUN_NOISE
    vec4 projected=WORLD_SUN(68)*position.x+WORLD_SUN(69)*position.y+WORLD_SUN(70)*position.z+WORLD_SUN(71);
    vec2 pixel=floor((projected.xy/projected.w*vec2(.5,-.5)+vec2(.5,.5))*WORLD_SUN(67).yz);
    vec3 hashPoint=sunFraction(vec3(pixel,WORLD_SUN(67).w)*.1031);
    hashPoint+=dot(hashPoint,hashPoint.yzx+vec3(33.33,33.33,33.33));
    sunRotation=sunFraction((hashPoint.x+hashPoint.y)*hashPoint.z);
    vec2 noisePixel=sunFraction(pixel/64.0)*64.0;noisePixel.y+=WORLD_SUN(67).w*64.0;
    vec4 packed=WORLD_SUN_NOISE(noisePixel);
    float dissolve=sunFraction((floor(packed.r*255.0+.5)+floor(packed.g*255.0+.5)*256.0)/65535.0+
        .625*sunFraction(WORLD_SUN(67).w/8.0)*8.0);
#endif
    float depth=dot(WORLD_SUN(64).xyz,position-WORLD_SUN(63).xyz);
    int count=int(WORLD_SUN(63).w);
    for(int k=0;k<4;++k)if(k<count) {
        vec4 range=WORLD_SUN(k*7+6);
        if(depth<range.x) {
            vec3 clip=sunShadowClip(k,position,normal);
            if(!sunShadowContains(clip))continue;
            float blend=clamp((depth-range.x*range.y)/max(range.x*(1.0-range.y),.0001),0.0,1.0);
#ifdef WORLD_SUN_NOISE
            if(blend>dissolve) {
                if(k+1<count)return sunShadowFilter(k+1,sunShadowClip(k+1,position,normal));
                return sunDistantVisibility(position,normal);
            }
#else
            float value=sunShadowFilter(k,clip);
            if(blend>0.0) {
                float next=1.0;
                if(k+1<count)next=sunShadowFilter(k+1,sunShadowClip(k+1,position,normal));
                else next=sunDistantVisibility(position,normal);
                value=mix(value,next,blend);
            }
#endif
#ifdef WORLD_SUN_NOISE
            return sunShadowFilter(k,clip);
#else
            return value;
#endif
        }
    }
    return sunDistantVisibility(position,normal);
}
#endif
