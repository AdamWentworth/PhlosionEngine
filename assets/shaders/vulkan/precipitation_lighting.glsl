#define WORLD_SUN(i) VP(600+i)
#define WORLD_SHADOW_Y -1.0
#define WORLD_SUN_COMPARE(uv,z) VP_SUN_COMPARE(uv,z)
#define WORLD_SUN_VOLUME
#include "world_sun_shadow.glsl"
// Tiled slices retain an ordinary texture binding; explicit Z interpolation
// matches a 3D trilinear footprint without sampling across slice boundaries.
#define ENV_SUN VP(17)
#define ENV_AMBIENT VP(19)
#define ENV_BOUNCE(i) VP(20+i)
#define WORLD_GI_ISOTROPIC
uint localLightReceiver(){return uint(VP(16).w);}
#include "world_indirect.glsl"
#include "world_finite_light.glsl"
VP_SHARED uint volumeLightMask;
vec4 volumeTransform(uint row,vec4 p){return vec4(dot(VP(row),p),dot(VP(row+1u),p),dot(VP(row+2u),p),dot(VP(row+3u),p));}
float volumeDepth(float z){return VP(26).x+pow(max(z,0.0),VP(1).w)*VP(1).z;}
vec3 volumeRay(vec2 uv){
    vec4 h=volumeTransform(4u,vec4(uv*vec2(2,-2)+vec2(-1,1),1,1));
    vec3 ray=h.xyz/h.w-VP(3).xyz;return ray/max(dot(ray,VP(12).xyz),.000001);
}
vec3 volumePosition(vec3 coord){return VP(3).xyz+volumeRay(coord.xy)*volumeDepth(coord.z);}
ivec2 volumePixel(ivec3 cell){return ivec2(cell.x+(cell.z%8)*int(VP(0).x),cell.y+(cell.z/8)*int(VP(0).y));}
vec2 volumeUv(vec2 uv,int z){
    vec2 xy=clamp(uv*VP(0).xy,vec2(.5,.5),VP(0).xy-vec2(.5,.5));
    return (xy+vec2(float(z%8)*VP(0).x,float(z/8)*VP(0).y))/(VP(0).xy*vec2(8,ceil(VP(0).z/8.0)));
}
vec4 volumePrevious(vec3 uv){
    float z=clamp(uv.z*VP(0).z-.5,0.0,VP(0).z-1.0);int low=int(floor(z));
    return mix(VP_HISTORY(volumeUv(uv.xy,low)),VP_HISTORY(volumeUv(uv.xy,min(low+1,int(VP(0).z)-1))),fract(z));
}
float volumePhase(float cosine){
    float g=VP(16).y,d=abs(1.0+g*g-2.0*g*cosine);
    return clamp((1.0-g*g)/pow(max(d,1e-8),1.5)*.7853981633974483,0.0,16.0);
}
float volumeCloudShadow(vec3 position){
    if(VP(592).w<.5)return 1.0;
    float top=position.y-VP(592).z;
    vec2 uv=(position.xz-VP(592).xy-top*VP(593).xy)/VP(593).w+.5;
    return clamp(1.0-VP_CLOUD_SHADOW(uv)*(-top)*100.0*VP(593).z,0.0,1.0);
}
float volumeShadow(vec3 position){
    if(WORLD_SUN(67).x>.5)return sunCascadeVisibility(position,vec3(0,0,0));
    if(VP(164).x<=0.0)return 1.0;
    vec4 h=volumeTransform(160u,vec4(position,1));if(h.w<=.000001)return 1.0;vec3 ndc=h.xyz/h.w;
    if(max(abs(ndc.x),max(abs(ndc.y),abs(ndc.z)))>=1.0)return 1.0;
    vec2 uv=ndc.xy*vec2(.5,-.5)+vec2(.5,.5);
    float value=step(ndc.z*.5+.5-VP(164).y,VP_SHADOW(VP(165).xy+uv*VP(165).zw));
    return mix(1.0,value,VP(164).x);
}
vec3 volumeLocal(uint i,vec3 p,vec3 ray,float depth,uint channels){
    uint at=32u+6u*i;vec4 light=VP(at),energy=VP(at+1u),axis=VP(at+2u),shape=VP(at+3u),cone=VP(at+4u),face=VP(at+5u);
    if((uint(energy.w)&channels)==0u)return vec3(0,0,0);
    vec3 delta=light.xyz-p;if(dot(delta,delta)>pow(light.w+shape.y*.5,2.0))return vec3(0,0,0);
    if(face.w>.5&&dot(p-light.xyz,face.xyz)<0.0)return vec3(0,0,0);
    uint flags=uint(axis.w);vec3 halfAxis=axis.xyz*shape.y*.5;
    float d2=finiteLightDistanceSquared(delta,halfAxis,shape.y>0.0);
    // Fog keeps the source's uncut dim tail; surface cutoff is not enabled here.
    float normalized=d2/(light.w*light.w),fade=1.0-clamp(normalized*normalized,0.0,1.0);
    float attenuation=(flags&2u)!=0u?fade*fade/max(d2,.0001):1.0-clamp(sqrt(d2)/light.w,0.0,1.0);
    if(shape.w>0.0)attenuation=min(attenuation,shape.w);
    // A capsule's longitudinal axis and outward cone direction are independent.
    vec3 direction=dot(face.xyz,face.xyz)>.5?face.xyz:axis.xyz;
    if((flags&4u)!=0u)attenuation*=nativeLightCone(delta,halfAxis,direction,cone);
    // Local fog light has no directional phase; apply its modifier before the cap.
    return energy.rgb*min(attenuation*cone.w,max(4.0-depth*.01,1.0));
}
void volumeCull(ivec3 group){
    vec3 lo=vec3(1e20,1e20,1e20),hi=-lo;
    for(int i=0;i<8;++i){
        vec3 q=vec3(group*4)+vec3((i&1)!=0?4:0,(i&2)!=0?4:0,(i&4)!=0?4:0);
        vec3 p=volumePosition(q/VP(0).xyz);lo=min(lo,p);hi=max(hi,p);
    }
    uint mask=0u;
    for(uint i=0u;i<uint(VP(25).x);++i){
        vec4 l=VP(32u+i*6u);vec3 delta=max(max(lo-l.xyz,l.xyz-hi),vec3(0,0,0));
        float range=l.w+VP(35u+i*6u).y*.5;if(dot(delta,delta)<=range*range)mask|=1u<<i;
    }
    volumeLightMask=mask;
}
void volumeMaximumDepth(ivec2 cell){
    ivec2 size=ivec2(ceil(VP(1).xy/16.0));if(cell.x>=size.x||cell.y>=size.y)return;
    float maximum=0.0;
    for(int y=0;y<16;++y)for(int x=0;x<16;++x){
        ivec2 pixel=min(cell*16+ivec2(x,y),ivec2(VP(1).xy)-ivec2(1,1));
        float raw=VP_DEPTH(pixel);vec2 uv=(vec2(pixel)+.5)/VP(1).xy;
        vec4 h=volumeTransform(4u,vec4(uv*vec2(2,-2)+vec2(-1,1),raw*2.0-1.0,1));
        float depth=h.w>1e-8?dot(h.xyz/h.w-VP(3).xyz,VP(12).xyz):VP(1).z;
        maximum=max(maximum,clamp(depth,0.0,VP(1).z));
    }
    VP_STORE_DEPTH(cell,maximum);
}
struct VolumeMedium {vec3 albedo;float density;float absorption;float ambient;uint channels;};
VolumeMedium volumeMedium(vec3 p) {
    VolumeMedium m;m.albedo=vec3(0,0,0);m.density=0.0;m.absorption=0.0;m.ambient=0.0;m.channels=uint(VP(16).w);
    float remaining=1.0;
    VP_LOOP for(uint i=0u;i<uint(VP(172).x);++i) {
        uint at=176u+i*7u;vec3 q=p-VP(at).xyz;
        vec3 a=VP(at+1u).xyz,b=VP(at+2u).xyz,c=VP(at+3u).xyz;
        vec3 t=abs(vec3(dot(q,a)/dot(a,a),dot(q,b)/dot(b,b),dot(q,c)/dot(c,c)));
        float edge=1.0-max(t.x,max(t.y,t.z));if(edge<=0.0)continue;
        float blend=clamp(edge/max(VP(at+2u).w,1e-8),0.0,1.0),weight=blend*remaining;
        m.density+=VP(at).w*clamp(edge/max(VP(at+3u).w,1e-8),0.0,1.0)*weight;
        m.albedo+=VP(at+4u).rgb*weight;m.absorption+=VP(at+1u).w*weight;m.ambient+=VP(at+4u).w*weight;
        m.channels=uint(VP(at+5u).x);remaining*=1.0-blend;if(remaining<.01)break;
    }
    if(remaining>.01) {
        m.density+=VP(15).x*clamp(1.0-p.y/1000.0,0.0,1.0)*
            clamp(exp(-VP(15).z*max(p.y-VP(15).y,0.0)),0.0,VP(167).z)*remaining;
        m.albedo+=VP(168).rgb*(VP(16).x*remaining);m.absorption+=VP(15).w*remaining;m.ambient+=VP(166).w*remaining;
    }
    VP_LOOP for(uint i=0u;i<uint(VP(172).y);++i) {
        uint at=400u+i*3u;
        float mask=clamp(1.0-length(p-VP(at).xyz)/VP(at).w,0.0,1.0);
        float density=(mask>0.0?pow(mask,VP(at+2u).x):0.0)*VP(at+1u).w;
        float total=density+m.density*(1.0-density);
        m.albedo=total>0.0?VP(at+1u).rgb*sqrt(abs(density))+m.albedo*(m.density*(1.0-density)/total):vec3(0,0,0);
        m.density=total;
    }
    return m;
}
void volumeLight(ivec3 cell,ivec3 group,uint lane){
    if(lane==0u)volumeCull(group);VP_BARRIER;
    if(cell.x>=int(VP(0).x)||cell.y>=int(VP(0).y)||cell.z>=int(VP(0).z))return;
    vec3 center=(vec3(cell)+.5)/VP(0).xyz;float linearDepth=volumeDepth(center.z);
    ivec2 tile=ivec2(center.xy*VP(1).xy/16.0),size=ivec2(ceil(VP(1).xy/16.0));float maxDepth=0.0;
    if(VP(26).z>.5)maxDepth=VP(26).x+VP(1).z;
    else for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x)
        maxDepth=max(maxDepth,VP_MAX_DEPTH(clamp(tile+ivec2(x,y),ivec2(0,0),size-ivec2(1,1))));
    if(VP(26).z<.5&&volumeDepth(max(float(cell.z)-1.0,0.0)/VP(0).z)>=maxDepth+(linearDepth<2.0?1.0:0.0)){
        VP_STORE(volumePixel(cell),vec4(0,0,0,-1000));return;
    }
    vec4 history=vec4(0,0,0,0);bool valid=false;
    if(VP(2).w>.5){
        vec3 p=volumePosition(center);vec4 h=volumeTransform(8u,vec4(p,1));
        if(h.w>0.0){
            vec3 uv=vec3(h.xy/h.w*vec2(.5,-.5)+vec2(.5,.5),pow(max((dot(p-VP(13).xyz,VP(14).xyz)-VP(26).y)*VP(13).w,0.0),VP(14).w));
            if(min(uv.x,min(uv.y,uv.z))>=0.0&&max(uv.x,max(uv.y,uv.z))<=1.0){history=volumePrevious(uv);valid=history.a>0.0;}
        }
    }
    uint samples=VP(26).z>.5?1u:(valid?1u:uint(VP(25).y));vec4 result=vec4(0,0,0,0);
    float bayer=float((((cell.x^cell.y)&1)<<1)|(cell.y&1))*.25;
    VP_LOOP for(uint sampleIndex=0u;sampleIndex<samples;++sampleIndex){
        vec3 offset=fract(VP(2).xyz+vec3(.0,.0,VP(2).w>.5?(1.0-min(1.0,linearDepth/16.0))*bayer:.5)+
            fract(vec3(.5,.5,.5)+float(sampleIndex)*vec3(.8191725133961645,.6710436067037893,.5497004779019703)));
        vec3 coord=VP(26).z>.5 ?
            (vec3(cell)+.5+vec3(VP(2).xy,fract(VP(2).z+bayer)))/VP(0).xyz :
            (vec3(cell)+offset)/VP(0).xyz;
        float d=max(.001,min(volumeDepth(coord.z),maxDepth-.05));vec3 ray=volumeRay(coord.xy),p=VP(3).xyz+ray*d;
        VolumeMedium medium=volumeMedium(p);
        float shadow=volumeShadow(p)*volumeCloudShadow(p);
        float phase=volumePhase(dot(VP(17).xyz,normalize(ray)))*VP(167).x+VP(167).y;
        float globalScale=VP(166).x;vec3 ambient;
        if(VP(26).z>.5) {
            float distanceBlend=clamp(d/4000.0,0.0,1.0);
            medium.albedo=mix(VP(168).rgb,VP(169).rgb,distanceBlend)*VP(16).x;
            globalScale=mix(globalScale,VP(167).w,distanceBlend);
            vec3 ground=VP(170).rgb*VP(170).w;
            float groundLuma=dot(ground,vec3(.2126,.7152,.0722));
            ground=mix(vec3(groundLuma,groundLuma,groundLuma),ground,VP(171).z);
            float height=smoothstep(VP(171).x,VP(171).x+100.0+VP(171).y*80.0,p.y);
            ambient=(ground*height+VP(19).rgb*VP(169).w)*VP(166).z*mix(1.0,shadow,VP(168).w);
        } else ambient=worldIndirectAmbient(p,vec3(0,0,0))*(medium.ambient*VP(166).z);
        vec3 light=VP(18).rgb*(shadow*phase*globalScale)+ambient;
        VP_LOOP for(uint i=0u;i<uint(VP(25).x);++i)if((volumeLightMask&(1u<<i))!=0u)
            light+=volumeLocal(i,p,ray,d,medium.channels)*VP(166).y;
        result+=vec4(light*medium.albedo*medium.density,medium.density*medium.absorption);
    }
    result/=float(samples);if(valid)result=mix(result,history,.95);
    VP_STORE(volumePixel(cell),result);
}
void volumeScatter(ivec2 cell){
    if(cell.x>=int(VP(0).x)||cell.y>=int(VP(0).y))return;
    vec3 light=vec3(0,0,0);float transmission=1.0,previous=volumeDepth(.5/VP(0).z);
    VP_LOOP for(int z=0;z<int(VP(0).z);++z){
        float next=volumeDepth((float(z)+1.5)/VP(0).z),stepLength=next-previous;previous=next;
        vec4 value=max(VP_CURRENT(volumePixel(ivec3(cell,z))),vec4(0,0,0,0));float transmit=exp(-value.a*stepLength);
        // Preserve the supplied snapshot's segment-transmission ordering.
        float integral=value.a>1e-8?(1.0-transmit)/value.a:stepLength;
        light+=value.rgb*(integral*transmit);transmission*=transmit;
        VP_STORE(volumePixel(ivec3(cell,z)),vec4(light,transmission));
    }
}
