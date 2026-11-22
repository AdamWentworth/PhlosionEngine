// Current renderer has four point and sixteen finite/LTC sources: one lossless
// 20-bit list per cluster. Never silently drop a light when expanding the API.
uint clusterTilesX(){return uint(CP(8).z);}
uint clusterTilesY(){return uint(CP(8).w);}
uint clusterCoarseX(){return (clusterTilesX()+3u)/4u;}
uint clusterCoarseY(){return (clusterTilesY()+3u)/4u;}
uint clusterCoarseOffset(){return 2u+clusterTilesX()*clusterTilesY()*32u;}
uint clusterListOffset(){return clusterCoarseOffset()+clusterCoarseX()*clusterCoarseY()*8u;}
uint clusterFineIndex(uvec3 cell){return 2u+(cell.z*clusterTilesY()+cell.y)*clusterTilesX()+cell.x;}
uint clusterCoarseIndex(uvec3 cell){return clusterCoarseOffset()+(cell.z*clusterCoarseY()+cell.y)*clusterCoarseX()+cell.x;}
vec3 clusterUnproject(vec2 uv,float depth) {
    vec4 clip=vec4(uv*vec2(2.0,-2.0)+vec2(-1.0,1.0),depth*2.0-1.0,1.0);
    vec4 world=vec4(dot(CP(4),clip),dot(CP(5),clip),dot(CP(6),clip),dot(CP(7),clip));
    return world.xyz/world.w;
}
float clusterBoundary(uint slice){return slice==0u?0.0:slice>=32u?1e7:exp2(float(slice)*.5-2.0);}
uint clusterSlice(vec3 p) {
    float depth=dot(p-CP(9).xyz,CP(10).xyz);
    return uint(clamp(floor((log2(max(depth,.25))+2.0)*2.0),0.0,31.0));
}
vec3 clusterRayPlane(vec2 uv,float distance) {
    vec3 a=clusterUnproject(uv,.25),b=clusterUnproject(uv,.75);
    float z=dot(a-CP(9).xyz,CP(10).xyz), dz=dot(b-a,CP(10).xyz);
    return a+(b-a)*((distance-z)/dz);
}
uint clusterCull(uvec3 cell,uint scale,uint candidates) {
    if(candidates==0u||CP(11).w<.5)return 0u;
    vec3 lo=vec3(1e30,1e30,1e30),hi=-lo;
    for(uint corner=0u;corner<8u;++corner) {
        vec2 pixel=vec2(cell.xy*16u*scale)+vec2(float(corner&1u),float((corner>>1u)&1u))*float(16u*scale);
        pixel=min(pixel,CP(8).xy);
        vec3 p=clusterRayPlane(pixel/CP(8).xy,clusterBoundary((cell.z+((corner>>2u)&1u))*scale));
        lo=min(lo,p);hi=max(hi,p);
    }
    uint result=0u;
    CLUSTER_LOOP while(candidates!=0u) {
        uint i=CLUSTER_FIRST(candidates);candidates&=candidates-1u;
        vec4 light=i<4u?CP(12u+2u*i):CP(32u+(i-4u)*4u);
        vec3 color=i<4u?CP(13u+2u*i).rgb:CP(35u+(i-4u)*4u).rgb;
        if(light.w<=0.0||max(color.r,max(color.g,color.b))<=0.0||CP(11).w<.5)continue;
        uint authored=uint(i<4u?CP(13u+2u*i).w:CP(35u+(i-4u)*4u).w);
        uint flags=(authored>>8u)&255u;
        if((authored&255u)==0u||(flags&4u)==0u||(flags&3u)==0u)continue;
        float radius=light.w;
        // A 0.1% range pad bounds finite-distance reciprocal-root rounding.
        if(i>=4u&&CP(33u+(i-4u)*4u).w>.5)
            radius=radius*1.001+length(CP(33u+(i-4u)*4u).xyz)+length(CP(34u+(i-4u)*4u).xyz);
        if(i>=4u&&CP(33u+(i-4u)*4u).w>1.5)
            radius=light.w*1.001+CP(97u+(i-4u)*3u).y*.5+CP(97u+(i-4u)*3u).x;
        vec3 delta=max(max(lo-light.xyz,light.xyz-hi),vec3(0.0,0.0,0.0));
        if(dot(delta,delta)<=radius*radius*1.00001)result|=1u<<i;
    }
    return result;
}
bool clusterEligible(ivec2 p) {
    if(p.x<0||p.y<0||p.x>=int(CP(8).x)||p.y>=int(CP(8).y))return false;
    float c=NORMAL_LOAD(p).a;
    float tag=ALBEDO_LOAD(p).a;
    return c>.15&&c<.85&&(tag<.15||tag>.5)&&DEPTH_LOAD(p)<1.0;
}
uint clusterClass(ivec2 p){return ALBEDO_LOAD(p).a>.84||MATERIAL_LOAD(p).x<.001?0u:1u;}
#if CLUSTER_PASS >= 4
// Depth reconstruction has a finite positional envelope. Reject an emitter's
// coplanar receivers within that envelope rather than magnifying roundoff into
// near-source self-lighting. This leaves the finite-source BRDF unchanged.
vec3 clusterPositionError(vec2 uv,float depth,vec3 position) {
    vec4 clip=vec4(uv*vec2(2.0,-2.0)+vec2(-1.0,1.0),depth*2.0-1.0,1.0);
    const float unitRoundoff=5.960464477539063e-8;
    vec4 magnitude=abs(clip),inputError=vec4(4.0*unitRoundoff,4.0*unitRoundoff,4.0*unitRoundoff,0.0);
    vec4 accumulated=vec4(dot(abs(CP(4)),magnitude),dot(abs(CP(5)),magnitude),
        dot(abs(CP(6)),magnitude),dot(abs(CP(7)),magnitude));
    vec4 error=vec4(CP(11).xyz,CP(10).w)+accumulated*(4.0*unitRoundoff/(1.0-4.0*unitRoundoff));
    error+=vec4(dot(abs(CP(4)),inputError),dot(abs(CP(5)),inputError),
        dot(abs(CP(6)),inputError),dot(abs(CP(7)),inputError));
    float w=abs(dot(CP(7),clip));
    return (error.xyz+abs(position)*error.w)/max(w-error.w,1e-20)+abs(position)*unitRoundoff;
}
bool clusterRectFront(int index,vec3 position,vec3 error) {
    if(ENV_RECT(index*4+1).w>1.5 &&
        (((uint(ENV_RECT(64+index*3).w)&4u)==0u)||ENV_RECT(66+index*3).y<0.0))return true;
    vec3 source=ENV_RECT(index*4).xyz;
    vec3 n=cross(ENV_RECT(index*4+1).xyz,ENV_RECT(index*4+2).xyz);
    float squared=dot(n,n);
    if(squared<1e-20)return false;
    n*=inversesqrt(squared);
    float planeError=dot(abs(n),error+4.76837158203125e-7*(abs(source)+abs(position)));
    return dot(n,source-position)<-max(1e-6,planeError);
}
CLUSTER_STATIC vec3 clusterX0,clusterX1,clusterY0,clusterY1;
CLUSTER_STATIC vec3 clusterNX0,clusterNX1,clusterNY0,clusterNY1;
vec3 clusterShadowCoordinate(int index,vec3 position,vec3 normal) {
    vec4 parameters=WORLD_LOCAL_SHADOW(index*6+4);
    float signToLight=dot(normal,(index<16?ENV_RECT(index*4).xyz:ENV_POINT((index-16)*2).xyz)-position)<0.0?-1.0:1.0;
    position+=normal*(signToLight*parameters.z);
    vec4 projected=WORLD_LOCAL_SHADOW(index*6)*position.x+WORLD_LOCAL_SHADOW(index*6+1)*position.y+
        WORLD_LOCAL_SHADOW(index*6+2)*position.z+WORLD_LOCAL_SHADOW(index*6+3);
    vec3 clip=projected.xyz/projected.w;
    return vec3(clip.xy*vec2(.5,-.5)+vec2(.5,.5),clip.z*.5+.5-parameters.y);
}
void worldLocalShadowDerivatives(int index,vec3 position,vec3 normal,vec2 uv,float depth,out vec3 dx,out vec3 dy) {
    dx=clusterShadowCoordinate(index,clusterX1,clusterNX1)-clusterShadowCoordinate(index,clusterX0,clusterNX0);
    dy=clusterShadowCoordinate(index,clusterY1,clusterNY1)-clusterShadowCoordinate(index,clusterY0,clusterNY0);
}
#include "world_finite_light.glsl"
#define PHLOSION_LOCAL_SHADOW_ONLY
#define PHLOSION_EXPLICIT_LOCAL_SHADOW_DERIVATIVES
#include "world_shadow.glsl"
#define PHLOSION_LOCAL_LIGHT_LOOP CLUSTER_LOOP
#define PHLOSION_EARLY_LOCAL_SHADOW 1
#include "world_local_direct.glsl"
vec3 clusterNeighbour(ivec2 p,ivec2 q,vec3 origin,vec3 normal) {
    vec2 uv=(vec2(q)+vec2(.5,.5))/CP(8).xy;
    if(clusterEligible(q)&&IDENTITY_LOAD(p)==IDENTITY_LOAD(q))
        return clusterUnproject(uv,DEPTH_LOAD(q));
    // Silhouette helper lanes are unavailable. Extend the receiver plane, never
    // use another object's depth to bias the receiver's PCF footprint.
    vec3 a=clusterUnproject(uv,.25),b=clusterUnproject(uv,.75);
    float denominator=dot(b-a,normal);
    return abs(denominator)>1e-8?a+(b-a)*(dot(origin-a,normal)/denominator):origin;
}
vec3 clusterNeighbourNormal(ivec2 p,ivec2 q,vec3 normal) {
    if(!clusterEligible(q)||IDENTITY_LOAD(p)!=IDENTITY_LOAD(q))return normal;
    vec3 n=normalize(NORMAL_LOAD(q).xyz*2.0-1.0);
    return dot(n,normal)<0.0?-n:n;
}
void clusterShade(ivec2 pixel) {
    if(!clusterEligible(pixel)||clusterClass(pixel)!=uint(CLUSTER_PASS-4))return;
    localLightReceiverOverride=LIGHT_CHANNELS_LOAD(pixel);localLightConsumer=4u;
    float depth=DEPTH_LOAD(pixel);
    vec3 position=clusterUnproject((vec2(pixel)+vec2(.5,.5))/CP(8).xy,depth);
    vec3 positionError=clusterPositionError((vec2(pixel)+vec2(.5,.5))/CP(8).xy,depth,position);
    uint mask=WORK(clusterFineIndex(uvec3(uvec2(pixel)/16u,clusterSlice(position))));
    if(mask==0u) {
        STORE_DIFFUSE(pixel,vec4(0.0,0.0,0.0,1.0));
        return;
    }
    vec3 normal=normalize(NORMAL_LOAD(pixel).xyz*2.0-1.0),view=normalize(CP(9).xyz-position);
    if(dot(normal,view)<0.0)normal=-normal;
    vec3 albedo=ALBEDO_LOAD(pixel).rgb;albedo*=albedo;
    vec4 material=MATERIAL_LOAD(pixel);
    if(ALBEDO_LOAD(pixel).a>.84){material.x=0.0;material.z=1.0;}
    float roughness=max(material.w,.16),metallic=material.x;
    vec3 f0=mix(vec3(material.y,material.y,material.y),albedo,metallic);
    if(CP(9).w>.5) {
        ivec2 left=ivec2(pixel.x&~1,pixel.y),top=ivec2(pixel.x,pixel.y&~1);
        clusterX0=clusterNeighbour(pixel,left,position,normal);clusterX1=clusterNeighbour(pixel,left+ivec2(1,0),position,normal);
        clusterY0=clusterNeighbour(pixel,top,position,normal);clusterY1=clusterNeighbour(pixel,top+ivec2(0,1),position,normal);
        clusterNX0=clusterNeighbourNormal(pixel,left,normal);clusterNX1=clusterNeighbourNormal(pixel,left+ivec2(1,0),normal);
        clusterNY0=clusterNeighbourNormal(pixel,top,normal);clusterNY1=clusterNeighbourNormal(pixel,top+ivec2(0,1),normal);
    }
    vec3 diffuse=vec3(0.0,0.0,0.0),specular=diffuse;
    WorldRectPbrContext context=prepareWorldRectPbr(normal,view,roughness);
    CLUSTER_LOOP while(mask!=0u) {
        uint i=CLUSTER_FIRST(mask);mask&=mask-1u;
        FiniteLightResponse response;
        if(i<4u)response=environmentPointPbr(int(i),position,normal,view,albedo,f0,roughness,metallic);
        else {
            int r=int(i)-4;
            if(!clusterRectFront(r,position,positionError))continue;
            float visibility=environmentLocalShadowVisibility(r,position,normal);
            response=environmentRectPbr(r,position,normal,view,albedo,f0,roughness,metallic,context,visibility);
        }
        diffuse+=response.diffuse;specular+=response.specular;
    }
    STORE_DIFFUSE(pixel,vec4(min(diffuse,vec3(65504.0,65504.0,65504.0)),1.0));
    STORE_SPECULAR(pixel,vec4(min(specular,vec3(65504.0,65504.0,65504.0)),0.0));
}
#endif
