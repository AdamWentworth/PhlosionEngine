// Prepared indirect transport is independent of mesh/material texture bindings.
#ifndef WORLD_GI_DATA
bool worldIndirectEnabled(){return false;}
bool worldTerrainMaterialAvailable(vec3 p){return false;}
vec4 worldTerrainMaterial(vec3 p,out vec4 normalHeight){normalHeight=vec4(0.0,1.0,0.0,0.0);return vec4(0.0,0.0,0.0,0.0);}
vec4 worldTerrainProperties(vec3 p){return vec4(1.0,.04,0.0,0.0);}
vec4 worldIndirectNear(vec3 p,vec3 n){return vec4(0.0,0.0,0.0,0.0);}
vec3 worldIndirectAmbient(vec3 p,vec3 n){return ENV_AMBIENT.rgb;}
vec3 worldIndirectDistantReflection(vec3 p,vec3 n,vec3 ray,vec3 radiance){return radiance;}
float worldIndirectReflectionScale(vec3 p,vec4 nearLight,vec3 n,vec4 identity,bool globalProbe){return 1.0;}
#else
#ifndef WORLD_GI_FORWARD
#define WORLD_GI_FORWARD false
#endif
#ifndef WORLD_GI_PRIVATE
#define WORLD_GI_PRIVATE
#endif
// Fragment-private reuse: ambient and reflection consumers share identical
// receiver queries. A different position or normal evaluates independently.
WORLD_GI_PRIVATE bool worldGiNearValid=false;
WORLD_GI_PRIVATE vec3 worldGiNearPosition,worldGiNearNormal;
WORLD_GI_PRIVATE vec4 worldGiNearValue;
#ifndef WORLD_GI_LOOP
#define WORLD_GI_LOOP
#endif
uint localLightReceiver();
bool worldIndirectEnabled(){return ENV_SUN.w>.5&&ENV_BOUNCE(0).w>1.5;}
vec4 worldTerrainMaterialPoint(vec2 cell,uint field) {
    vec4 grid=WORLD_GI_DATA(7);cell=clamp(cell,vec2(0.0,0.0),vec2(grid.y-1.0,grid.y-1.0));
    return WORLD_GI_DATA(uint(grid.x)+(uint(cell.y)*uint(grid.y)+uint(cell.x))*2u+field);
}
bool worldTerrainMaterialAvailable(vec3 p) {
    if(!worldIndirectEnabled())return false;
    vec4 grid=WORLD_GI_DATA(7);
    vec2 cell=(p.xz-WORLD_GI_DATA(8).xy)/grid.w-.5;
    return grid.y>=2.0&&min(cell.x,cell.y)>=0.0&&max(cell.x,cell.y)<=grid.y-1.0;
}
vec4 worldTerrainMaterial(vec3 p,out vec4 normalHeight) {
    normalHeight=vec4(0.0,1.0,0.0,0.0);
    if(!worldTerrainMaterialAvailable(p))return vec4(0.0,0.0,0.0,0.0);
    vec4 grid=WORLD_GI_DATA(7);
    vec2 cell=(p.xz-WORLD_GI_DATA(8).xy)/grid.w-.5;
    vec2 lo=floor(cell),f=cell-lo;
    normalHeight=mix(mix(worldTerrainMaterialPoint(lo,0u),worldTerrainMaterialPoint(lo+vec2(1.0,0.0),0u),f.x),
        mix(worldTerrainMaterialPoint(lo+vec2(0.0,1.0),0u),worldTerrainMaterialPoint(lo+vec2(1.0,1.0),0u),f.x),f.y);
    vec3 color=mix(mix(worldTerrainMaterialPoint(lo,1u),worldTerrainMaterialPoint(lo+vec2(1.0,0.0),1u),f.x),
        mix(worldTerrainMaterialPoint(lo+vec2(0.0,1.0),1u),worldTerrainMaterialPoint(lo+vec2(1.0,1.0),1u),f.x),f.y).rgb;
    return vec4(color,1.0);
}
vec4 worldTerrainPropertiesPoint(vec2 cell) {
    vec4 grid=WORLD_GI_DATA(7);cell=clamp(cell,vec2(0.0,0.0),vec2(grid.y-1.0,grid.y-1.0));
    return WORLD_GI_DATA(uint(WORLD_GI_DATA(8).z)+(uint(cell.y)*uint(grid.y)+uint(cell.x))*2u);
}
vec4 worldTerrainProperties(vec3 p) {
    if(!worldTerrainMaterialAvailable(p))return vec4(1.0,.04,0.0,0.0);
    vec4 grid=WORLD_GI_DATA(7);vec2 cell=(p.xz-WORLD_GI_DATA(8).xy)/grid.w-.5;
    vec2 lo=floor(cell),f=cell-lo;
    return mix(mix(worldTerrainPropertiesPoint(lo),worldTerrainPropertiesPoint(lo+vec2(1.0,0.0)),f.x),
        mix(worldTerrainPropertiesPoint(lo+vec2(0.0,1.0)),worldTerrainPropertiesPoint(lo+vec2(1.0,1.0)),f.x),f.y);
}
vec4 worldGiMeta(uint i){return WORLD_GI_RESULT(uint(WORLD_GI_DATA(5).w)+30u+i);}
vec3 worldGiAxes(uint base,vec3 n) {
#ifdef WORLD_GI_ISOTROPIC
    vec3 energy=vec3(0.0,0.0,0.0);
    for(uint face=0u;face<6u;++face)energy+=WORLD_GI_RESULT(base+face).rgb;
    return energy/6.0;
#else
    vec3 result=vec3(0.0,0.0,0.0);
    for(uint a=0u;a<3u;++a)result+=WORLD_GI_RESULT(base+a*2u+(n[a]<0.0?1u:0u)).rgb*n[a]*n[a];
    return result;
#endif
}
#ifndef WORLD_GI_PREPARED
vec3 worldGiProbePosition(uint probe){return WORLD_GI_DATA(uint(WORLD_GI_DATA(1).x)+probe*7u).xyz;}
vec3 worldGiVisibilityPoint(uint base,uint sampleIndex,vec3 origin,vec3 a,vec3 b,vec3 c) {
    uint y=0u,x=sampleIndex;
    for(uint row=0u;row<4u;++row){if(x<5u-row)break;x-=5u-row;++y;}
    vec3 p=(a*float(4u-x-y)+b*float(x)+c*float(y))*.25;
    float depth=WORLD_GI_DATA(base+sampleIndex/4u)[sampleIndex%4u];
    return origin+(p-origin)*depth;
}
float worldGiVisibleDistance(uint base,vec3 p,vec3 origin,vec3 a,vec3 b,vec3 c,float bias) {
    if(WORLD_GI_DATA(base+3u).w>0.0)return 100.0;
    vec3 ray=p-origin,n=cross(b-a,c-a);float denom=dot(n,ray);
    if(dot(ray,ray)<1e-10)return 0.0;
    if(abs(denom)<1e-10)return 100.0;
    vec3 q=origin+ray*(dot(n,a-origin)/denom)-a,v=b-a,w=c-a;
    float vv=dot(v,v),ww=dot(w,w),vw=dot(v,w),det=vv*ww-vw*vw;
    vec2 uv=vec2(ww*dot(q,v)-vw*dot(q,w),vv*dot(q,w)-vw*dot(q,v))/det;
    uv=max(uv,vec2(0.0,0.0));uv/=max(1.0,uv.x+uv.y);uv*=.999999;
    uint x=uint(uv.x*4.0),y=uint(uv.y*4.0),index=(11u-y)*y/2u+x,stride=5u-y;
    uint i0=index+1u,i1=index+stride,i2=index;
    if((uv.x*4.0-float(x))+(uv.y*4.0-float(y))>1.0){i0=index+stride;i1=index+1u;i2=index+stride+1u;}
    vec3 p0=worldGiVisibilityPoint(base,min(i0,14u),origin,a,b,c);
    vec3 p1=worldGiVisibilityPoint(base,min(i1,14u),origin,a,b,c);
    vec3 p2=worldGiVisibilityPoint(base,min(i2,14u),origin,a,b,c);
    vec3 crossNormal=cross(p1-p0,p2-p0);
    if(dot(crossNormal,crossNormal)<1e-16)return 100.0;
    vec3 surface=normalize(crossNormal);float plane=dot(surface,ray);
    if(abs(plane)<1e-10)return 100.0;
    float distanceMetres=length(ray),fraction=dot(surface,p0-origin)/plane;
    float offset=clamp(dot(surface,ray/distanceMetres),0.0,1.0)*bias-bias;
    return clamp(distanceMetres-offset-abs(fraction)*distanceMetres,0.0,100.0);
}
vec4 worldIndirectNearEvaluate(vec3 p,vec3 n) {
    if(!worldIndirectEnabled())return vec4(0.0,0.0,0.0,0.0);
    vec4 bounds=WORLD_GI_DATA(3),spacing=WORLD_GI_DATA(4),dims=WORLD_GI_DATA(5);
    if(abs(spacing.w-ENV_BOUNCE(2).y)>.5)return vec4(0.0,0.0,0.0,0.0);
    vec3 grid=(p-bounds.xyz)/spacing.xyz,maximum=dims.xyz-vec3(1.0,1.0,1.0);
    if(min(grid.x,min(grid.y,grid.z))<0.0||max(grid.x-maximum.x,max(grid.y-maximum.y,grid.z-maximum.z))>0.0)return vec4(0.0,0.0,0.0,0.0);
    vec3 cell=min(floor(grid),maximum-vec3(1.0,1.0,1.0));
    uint accel=uint(WORLD_GI_DATA(1).z)+uint((cell.z*maximum.y+cell.y)*maximum.x+cell.x);
    int tet=int(WORLD_GI_DATA(accel).x),last=-1;uint at=0u;vec4 bary=vec4(0.0,0.0,0.0,0.0);bool found=false;
    WORLD_GI_LOOP for(int stepIndex=0;stepIndex<16;++stepIndex) {
        if(tet<0||tet>=int(WORLD_GI_DATA(0).w))break;
        at=uint(WORLD_GI_DATA(1).y)+uint(tet)*22u;
        vec3 offset=p-WORLD_GI_DATA(at+5u).xyz;
        bary.xyz=vec3(dot(WORLD_GI_DATA(at+2u).xyz,offset),dot(WORLD_GI_DATA(at+3u).xyz,offset),dot(WORLD_GI_DATA(at+4u).xyz,offset));
        bary.w=1.0-bary.x-bary.y-bary.z;
        uint smallest=0u;for(uint a=1u;a<4u;++a)if(bary[a]<bary[smallest])smallest=a;
        int next=int(WORLD_GI_DATA(at+1u)[smallest]);
        if(bary[smallest]>=-1e-6||(next>=0&&next==last)){found=true;break;}
        last=tet;tet=next;
    }
    if(!found)return vec4(0.0,0.0,0.0,0.0);
    vec4 ids=WORLD_GI_DATA(at),score=vec4(0.0,0.0,0.0,0.0);
    for(uint a=0u;a<4u;++a) {
        vec3 origin=worldGiProbePosition(uint(ids[a]));
        uint f0=a==0u?1u:0u,f1=a<2u?2u:1u,f2=a<3u?3u:2u;
        float visibility=worldGiVisibleDistance(at+6u+a*4u,p,origin,
            worldGiProbePosition(uint(ids[f0])),worldGiProbePosition(uint(ids[f1])),worldGiProbePosition(uint(ids[f2])),ENV_BOUNCE(1).w);
        vec3 delta=origin-p;float d=length(delta);
        score[a]=visibility*ENV_BOUNCE(1).x-(d>1e-6?dot(n,delta/d):0.0)*ENV_BOUNCE(1).y+d*ENV_BOUNCE(1).z;
    }
    float best=min(min(score.x,score.y),min(score.z,score.w)),sum=.0001;
    vec3 energy=vec3(0.0,0.0,0.0);
    for(uint a=0u;a<4u;++a) {
        float weight=clamp(bary[a],0.0,1.0)*clamp(1.0-abs(score[a]-best)/ENV_BOUNCE(2).x,0.0,1.0);
        energy+=worldGiAxes(uint(ids[a])*6u,n)*weight;sum+=weight;
    }
    vec3 edge=min(grid,maximum-grid)*spacing.xyz;
    float blend=clamp(min(edge.x,min(edge.y,edge.z))/bounds.w,0.0,1.0);
    return vec4(energy/sum,blend);
}
#endif
// Forward opaque materials can reuse the same visible receiver as composition.
// Depth and evaluated-normal checks reject other surfaces, views and back-face flips.
bool worldIndirectPreparedReceiver(vec3 p,vec3 n,out vec4 value) {
    value=vec4(0.0,0.0,0.0,0.0);
#if defined(WORLD_GI_RECEIVER) && defined(CLUSTER_VIEW) && !defined(PHLOSION_WORLD_SURFACE_CAPTURE)
    if(CLUSTER_VIEW(4).w<.5 || WORLD_GI_FORWARD)return false;
    vec4 h=vec4(p,1.0);
    vec4 clip=vec4(dot(CLUSTER_VIEW(0),h),dot(CLUSTER_VIEW(1),h),dot(CLUSTER_VIEW(2),h),dot(CLUSTER_VIEW(3),h));
    if(clip.w<=0.0)return false;
    vec3 ndc=clip.xyz/clip.w;
    vec2 uv=ndc.xy*vec2(.5,-.5)+vec2(.5,.5);
    if(min(uv.x,uv.y)<0.0 || max(uv.x,uv.y)>=1.0)return false;
    ivec2 pixel=ivec2(uv*CLUSTER_VIEW(4).xy);
    if(abs(CLUSTER_DEPTH(pixel)-(ndc.z*.5+.5))>.000002)return false;
    vec4 normal=STORED_NORMAL(pixel);
    float tag=STORED_ALBEDO(pixel).a;
    if((tag>=.16 && tag<.5) || normal.a<.16 || normal.a>.84)return false;
    vec3 stored=normal.xyz*2.0-1.0;
    float alignment=dot(normalize(stored),n);
    if(abs(alignment)<.9999)return false;
    if(alignment<0.0)pixel.y+=int(CLUSTER_VIEW(4).y);
    value=WORLD_GI_RECEIVER(pixel);
    return true;
#else
    return false;
#endif
}
vec4 worldIndirectNear(vec3 p,vec3 n) {
#ifdef WORLD_GI_PREPARED
#ifdef WORLD_GI_PREPARED_REVERSE
    if(WORLD_GI_PREPARED_TWO_SIDED && dot(n,normalize(WORLD_GI_PREPARED_NORMAL))<0.0)
        return worldIndirectEnabled()?WORLD_GI_PREPARED_REVERSE:vec4(0.0,0.0,0.0,0.0);
#endif
    return worldIndirectEnabled()?WORLD_GI_PREPARED:vec4(0.0,0.0,0.0,0.0);
#else
    if(!worldIndirectEnabled())return vec4(0.0,0.0,0.0,0.0);
#if defined(WORLD_GI_RECEIVER) && defined(CLUSTER_FORWARD_FALLBACK)
    // This primary-view variant has a complete, depth-matched receiver buffer.
    // Reflections can request the opposite leaf normal without an inline GI walk.
    if(!CLUSTER_FORWARD_FALLBACK) {
        vec4 prepared;
        worldIndirectPreparedReceiver(p,n,prepared);
        return prepared;
    }
#endif
    if(worldGiNearValid && p.x==worldGiNearPosition.x && p.y==worldGiNearPosition.y && p.z==worldGiNearPosition.z && n.x==worldGiNearNormal.x && n.y==worldGiNearNormal.y && n.z==worldGiNearNormal.z)return worldGiNearValue;
    if(!worldIndirectPreparedReceiver(p,n,worldGiNearValue))worldGiNearValue=worldIndirectNearEvaluate(p,n);
    worldGiNearPosition=p;worldGiNearNormal=n;worldGiNearValid=true;
    return worldGiNearValue;
#endif
}
#ifndef WORLD_GI_NEAR_ONLY
vec4 worldGiTerrainPoint(vec2 cell,uint field) {
    vec4 grid=WORLD_GI_DATA(7);cell=clamp(cell,vec2(0.0,0.0),vec2(grid.y-1.0,grid.y-1.0));
    return WORLD_GI_RESULT(uint(grid.z)+(uint(cell.y)*uint(grid.y)+uint(cell.x))*3u+field);
}
vec4 worldGiTerrainSample(vec3 p,uint field) {
    vec4 grid=WORLD_GI_DATA(7);vec2 pixel=(p.xz-WORLD_GI_DATA(8).xy)/grid.w-.5;
    vec2 low=floor(pixel),f=pixel-low;
    return mix(mix(worldGiTerrainPoint(low,field),worldGiTerrainPoint(low+vec2(1.0,0.0),field),f.x),
        mix(worldGiTerrainPoint(low+vec2(0.0,1.0),field),worldGiTerrainPoint(low+vec2(1.0,1.0),field),f.x),f.y);
}
struct WorldGiDistant {vec3 up;vec3 down;vec3 sky;float shadow;float distanceFactor;float heightFactor;float valid;};
WORLD_GI_PRIVATE bool worldGiDistantValid=false;
WORLD_GI_PRIVATE vec3 worldGiDistantPosition,worldGiDistantNormal;
WORLD_GI_PRIVATE WorldGiDistant worldGiDistantValue;
WorldGiDistant worldGiDistantEvaluate(vec3 p,vec3 n) {
    vec4 surface=worldGiTerrainSample(p,0u),energy=worldGiTerrainSample(p,1u),mask=worldGiTerrainSample(p,2u);
    float d=length(p.xz-ENV_BOUNCE(0).xz),heightScale=ENV_BOUNCE(4).x*d+1.0;
    WorldGiDistant result;
    result.distanceFactor=clamp(d*ENV_BOUNCE(3).x+ENV_BOUNCE(3).y,0.0,1.0);
    result.heightFactor=clamp((p.y-surface.w)*ENV_BOUNCE(3).z/heightScale+ENV_BOUNCE(3).w*heightScale,0.0,1.0);
    result.shadow=max(clamp((mask.y-(p.y+10.0))/(mask.z*ENV_BOUNCE(4).z+10.0),0.0,1.0),max(-n.y,0.0));
    uint base=uint(WORLD_GI_DATA(5).w)+24u;
    result.up=worldGiAxes(base,normalize(surface.xyz))*mask.x;
    result.down=energy.rgb;
    result.sky=worldGiAxes(base,normalize(n+vec3(0.0,.66,0.0)))*mask.x;
    result.valid=mask.w;
    return result;
}
WorldGiDistant worldGiDistant(vec3 p,vec3 n) {
    if(worldGiDistantValid && p.x==worldGiDistantPosition.x && p.y==worldGiDistantPosition.y && p.z==worldGiDistantPosition.z && n.x==worldGiDistantNormal.x && n.y==worldGiDistantNormal.y && n.z==worldGiDistantNormal.z)return worldGiDistantValue;
    worldGiDistantValue=worldGiDistantEvaluate(p,n);
    worldGiDistantPosition=p;worldGiDistantNormal=n;worldGiDistantValid=true;
    return worldGiDistantValue;
}
vec3 worldGiDistantAmbient(vec3 p,vec3 n,vec3 ambient) {
    if(WORLD_GI_FORWARD||WORLD_GI_DATA(7).y<1.0)return ambient;
    WorldGiDistant d=worldGiDistant(p,n);
    vec3 energy=mix(d.down,d.up,n.y*.5+.5);
    energy*=mix(1.0-d.shadow*.4,1.0-d.shadow*ENV_BOUNCE(4).y,abs(n.y));
    vec3 weights=vec3(.333333333,.333333333,.333333333);
    ambient=mix(ambient*(dot(energy,weights)/max(dot(ambient,weights),1e-5)),energy,d.distanceFactor);
    return mix(ambient,d.sky,d.heightFactor*d.valid*clamp(n.y*10.0+2.0,0.0,1.0));
}
vec3 worldIndirectDistantReflection(vec3 p,vec3 n,vec3 ray,vec3 radiance) {
    if(WORLD_GI_FORWARD||!worldIndirectEnabled()||WORLD_GI_DATA(7).y<1.0)return radiance;
    WorldGiDistant d=worldGiDistant(p,n);
    vec3 sky=radiance*worldGiTerrainSample(p,2u).x;
    sky=mix(sky*(d.down/max(sky.b,1e-5)),sky,d.heightFactor)+d.down*max(-ray.y,0.0);
    float shadow=d.shadow*ENV_BOUNCE(4).y*mix(1.0,clamp(abs(n.y)*10.0,0.0,1.0),d.heightFactor);
    return mix(radiance,sky,max(d.heightFactor,d.distanceFactor)*d.valid)*(1.0-shadow);
}
vec3 worldGiEnvironment(vec3 p,vec3 n,vec4 nearby) {
    uint base=uint(WORLD_GI_DATA(5).w);float total=0.0,last=-1.0;vec3 result=vec3(0.0,0.0,0.0);
    for(uint probe=0u;probe<4u;++probe) {
        uint i=4u+probe*4u,m=52u+probe*4u;
        vec4 options=worldGiMeta(m+3u);
        if(worldGiMeta(i).w<1.0||((uint(options.z)&255u)&localLightReceiver())==0u)continue;
        vec3 local=vec3(dot(worldGiMeta(m),vec4(p,1.0)),dot(worldGiMeta(m+1u),vec4(p,1.0)),dot(worldGiMeta(m+2u),vec4(p,1.0)));
        vec3 edge=(vec3(1.0,1.0,1.0)-abs(local))*(worldGiMeta(i+2u).xyz-worldGiMeta(i+1u).xyz)*.5;
        float weight=clamp(min(edge.x,min(edge.y,edge.z))/worldGiMeta(i+1u).w,0.0,1.0);
        float overrideWeight=(uint(options.z)&1024u)!=0u?1.0:((uint(options.z)&512u)==0u?1.0-nearby.a:0.0);
        float replace=weight*clamp(options.x-last,0.0,1.0);last=mix(last>0.0?last:options.x,options.x,weight);
        weight*=overrideWeight;replace*=overrideWeight;
        result=result*(1.0-replace)+worldGiAxes(base+probe*6u,n)*weight*options.y;
        total=total*(1.0-replace)+weight;
    }
    if(total<1.0){result+=mix(worldGiDistantAmbient(p,n,worldGiAxes(base+24u,n)),nearby.rgb,nearby.a)*(1.0-total);total=1.0;}
    return result/max(total,.00001);
}
vec3 worldIndirectAmbient(vec3 p,vec3 n) {
    if(!worldIndirectEnabled())return ENV_AMBIENT.rgb;
    vec4 nearby=worldIndirectNear(p,n);
    return worldGiEnvironment(p,n,nearby);
}
float worldIndirectReflectionScale(vec3 p,vec4 nearby,vec3 n,vec4 identity,bool globalProbe) {
    if(!worldIndirectEnabled())return 1.0;
    uint probe=4u;
    if(!globalProbe)for(uint i=0u;i<4u;++i) {
        vec4 delta=worldGiMeta(20u+i)-identity;
        if(worldGiMeta(4u+i*4u).w>=1.0&&dot(delta,delta)==0.0){probe=i;break;}
    }
    if(!globalProbe&&probe==4u)return 1.0;
    vec3 ambient=worldGiAxes(uint(WORLD_GI_DATA(5).w)+probe*6u,n);
    if(globalProbe)ambient=worldGiDistantAmbient(p,n,ambient);
    vec3 weights=vec3(.2126,.7152,.0722);
    float energy=dot(nearby.rgb,weights);
    float dimming=clamp(energy/max(dot(ambient,weights),globalProbe?.01:1e-8)+(globalProbe?ENV_BOUNCE(2).w:worldGiMeta(55u+probe*4u).w),globalProbe?.01:0.0,1.0);
    // Blend the bounded visibility, not an artificial bright energy that clips
    // almost the entire transition to one and exposes the volume boundary.
    return mix(1.0,dimming,clamp(nearby.a,0.0,1.0));
}
#endif // WORLD_GI_NEAR_ONLY
#endif
