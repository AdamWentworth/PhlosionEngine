#define WORLD_FOG_VIEW(i) CP(32+i)
#if CL_STAGE != 4
#define WORLD_FOG_SKY_ATLAS(uv) CL_FOG_SKY(uv)
#endif
#define WORLD_FOG_SAMPLE(uv,distant) CL_FOG(uv,distant)
#include "world_fog.glsl"
vec4 cloudTransform(int base,vec4 p){return vec4(dot(CP(base),p),dot(CP(base+1),p),dot(CP(base+2),p),dot(CP(base+3),p));}
vec3 cloudRay(vec2 pixel){
    vec4 h=cloudTransform(14,vec4((pixel+.5)/CP(0).xy*vec2(2,-2)+vec2(-1,1),1,1));
    return normalize(h.xyz/h.w-CP(2).xyz);
}
float cloudSceneDepth(ivec2 pixel){
    float z=CL_DEPTH(clamp(pixel,ivec2(0,0),ivec2(CP(0).xy)-ivec2(1,1)));
    if(z>=.999999)return 1e7;
    vec4 h=cloudTransform(14,vec4((vec2(pixel)+.5)/CP(0).xy*vec2(2,-2)+vec2(-1,1),z*2.0-1.0,1));
    return max(dot(h.xyz/h.w-CP(2).xyz,CP(22).xyz),0.0);
}
#include "world_cloud_integration.glsl"
void cloudShadow(ivec2 pixel){
    if(any(greaterThanEqual(pixel,ivec2(CP(1).x,CP(1).x))))return;
    vec2 xz=(vec2(pixel)/CP(1).x-.5)*4096.0+CP(12).zw;
    vec4 shadow=vec4(0,0,0,0);
    if(CP(3).w>.5)for(int i=0;i<4;++i)
        shadow[i]=cloudShadowSample(vec3(xz.x,mix(CP(6).x,CP(6).y,float(i)*.25),xz.y));
    CL_STORE(pixel,clamp(shadow/10.0,0.0,1.0));
}
float cloudJitter(ivec2 pixel){
    vec2 p=vec2(pixel)+vec2(5.588238,5.588238)*CP(13).w;
    return fract(52.9829189*fract(dot(p,vec2(.06711056,.00583715))));
}
void cloudVisible(ivec2 tile){
    if(any(greaterThanEqual(tile,ivec2(CP(1).zw))))return;
    if(all(equal(tile,ivec2(0,0))))CL_ARGS();
    if(CP(3).w<.5)return;
    bool visible=CP(2).y>=CP(6).x;
    int size=8*int(CP(1).y);
    CL_LOOP for(int y=0;y<size&&!visible;++y)for(int x=0;x<size&&!visible;++x){
        ivec2 pixel=tile*size+ivec2(x,y);
        if(any(greaterThanEqual(pixel,ivec2(CP(0).xy))))continue;
        float depth=cloudSceneDepth(pixel);
        vec3 ray=cloudRay(vec2(pixel));
        visible=depth>=1e6||CP(2).y+ray.y*depth/max(dot(ray,CP(22).xyz),1e-6)>=CP(6).x;
    }
    if(visible){uint at=CL_APPEND();CL_TILE_STORE(at+3u,uint(tile.y*int(CP(1).z)+tile.x));}
}
void cloudIntegrate(uint group,ivec2 local){
    uint tile=CL_TILE(group+3u);
    ivec2 pixel=ivec2(int(tile)%int(CP(1).z),int(tile)/int(CP(1).z))*8+local;
    if(any(greaterThanEqual(pixel,ivec2(CP(0).zw))))return;
    ivec2 source=pixel*int(CP(1).y),chosen=source;float depth=1e8;int sky=0;
    for(int y=0;y<int(CP(1).y);++y)for(int x=0;x<int(CP(1).y);++x){
        ivec2 p=source+ivec2(x,y);float value=cloudSceneDepth(p);sky+=value>=1e6?1:0;
        if(value<depth){depth=value;chosen=p;}
    }
    CL_DEPTH_STORE(pixel,depth);
    vec3 ray=cloudRay(vec2(chosen));float distance=depth/max(dot(ray,CP(22).xyz),1e-6),j=cloudJitter(pixel);
    float first=cloudInitialDistance(ray)+j*12.0*CP(7).w;
    if(sky>0&&sky<int(CP(1).y*CP(1).y)&&first>=distance)distance=1e7;
    CL_STORE(pixel,cloudMarch(ray,distance,j));
}
void cloudResolve(ivec2 pixel){
    if(any(greaterThanEqual(pixel,ivec2(CP(0).xy))))return;
    vec3 ray=cloudRay(vec2(pixel));float depth=cloudSceneDepth(pixel);
    float first=cloudInitialDistance(ray)+cloudJitter(pixel/int(CP(1).y))*12.0*CP(7).w;
    if(CP(3).w<.5||first>=depth/max(dot(ray,CP(22).xyz),1e-6)){CL_STORE(pixel,vec4(0,0,0,1));return;}
    vec2 q=(vec2(pixel)+.5)/CP(1).y-.5;ivec2 base=ivec2(floor(q));vec2 f=fract(q);
    vec4 value=vec4(0,0,0,0);float weight=0.0;
    for(int y=0;y<2;++y)for(int x=0;x<2;++x){
        ivec2 at=clamp(base+ivec2(x,y),ivec2(0,0),ivec2(CP(0).zw)-ivec2(1,1));
        float w=(x==0?1.0-f.x:f.x)*(y==0?1.0-f.y:f.y)/(4.57088e-6+abs(depth-CL_LOW_DEPTH(at)));
        value+=CL_INTEGRATED(at)*w;weight+=w;
    }
    CL_STORE(pixel,value/max(weight,1e-20));
}

#define ENV_SKY(i) CP(45+i)
#include "world_sky_model.glsl"
#undef ENV_SKY
void cloudSkyModel(ivec2 pixel) {
    if(any(greaterThanEqual(pixel,ivec2(128,64))))return;
    // Box-filtered levels 1..4 of the 128-square paraboloid share one view atlas.
    int level=pixel.x<64?1:pixel.x<96?2:pixel.x<112?3:4;
    int width=128/(1<<level),origin=level==1?0:level==2?64:level==3?96:112;
    ivec2 at=pixel-ivec2(origin,0);
    if(at.x>=width||at.y>=width){CL_STORE(pixel,vec4(0,0,0,0));return;}
    int footprint=1<<level;
    vec3 radiance=vec3(0,0,0);
    CL_LOOP for(int y=0;y<footprint;++y)for(int x=0;x<footprint;++x) {
        vec2 uv=(vec2(at*footprint+ivec2(x,y))+.5)/128.0;
        vec2 xy=vec2(1,1)-uv*2.0;float r2=dot(xy,xy);
        vec3 ray=vec3(2.0*xy.x,1.0-r2,2.0*xy.y)/(1.0+r2);
        radiance+=max(environmentAnalyticSky(ray,CP(64).xyz),vec3(0,0,0))*CP(55).rgb;
    }
    CL_STORE(pixel,vec4(radiance/float(footprint*footprint),1));
}
