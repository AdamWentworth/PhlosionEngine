#define WORLD_SUN(i) GI_PARAM(162+i)
#define WORLD_SHADOW_Y -1.0
#define WORLD_SUN_COMPARE(uv,z) GI_SUN_COMPARE(uv,z)
#define WORLD_SUN_VOLUME
#include "world_sun_shadow.glsl"
// Independently authored prepared-surface transport. All values are scene-linear.
GI_SHARED vec4 giReduction[64];
GI_SHARED uint giLightMask;
vec3 giAxis(uint face) {
    if(face==0u)return vec3(1,0,0);if(face==1u)return vec3(-1,0,0);
    if(face==2u)return vec3(0,1,0);if(face==3u)return vec3(0,-1,0);
    if(face==4u)return vec3(0,0,1);return vec3(0,0,-1);
}
vec3 giAxisPrevious(uint probe,vec3 normal) {
    vec3 value=vec3(0,0,0);
    for(uint a=0u;a<3u;++a)value+=GI_PREVIOUS(probe*6u+a*2u+(normal[a]<0.0?1u:0u)).rgb*normal[a]*normal[a];
    return value;
}
vec3 giAmbientAxis(uint probe,vec3 normal) {
    uint base=uint(GI_DATA(5).w)+probe*6u;
    vec3 value=vec3(0,0,0);
    for(uint a=0u;a<3u;++a)value+=GI_OUTPUT(base+a*2u+(normal[a]<0.0?1u:0u)).rgb*normal[a]*normal[a];
    return value;
}
vec3 giCubeAddress(vec3 d) {
    vec3 a=abs(d);vec2 uv;uint face;
    if(a.x>=a.y&&a.x>=a.z){face=d.x>=0.0?0u:1u;uv=vec2(d.x>=0.0?d.z:-d.z,d.y)/a.x;}
    else if(a.y>=a.z){face=d.y>=0.0?2u:3u;uv=vec2(-d.x,d.y>=0.0?-d.z:d.z)/a.y;}
    else {face=d.z>=0.0?4u:5u;uv=vec2(d.z>=0.0?-d.x:d.x,d.y)/a.z;}
    return vec3(uv.x*.5+.5,.5-uv.y*.5,float(face));
}
vec3 giProbeTexture(vec3 d,float size,vec2 scale,vec2 offset) {
    vec3 c=giCubeAddress(d);
    float small=size*.125,stride=small+2.0;
    vec2 base=vec2(6.0*(size*.5-size*.25+2.0),2.0*(size+2.0)+2.0*(size*.5+2.0));
    vec2 pixel=base+vec2(float(uint(c.z)%3u),float(uint(c.z)/3u))*stride+vec2(1,1)+c.xy*small;
    return GI_ENVIRONMENT(pixel*(scale/size)+offset);
}
vec3 giSourceAmbient(uint probe,vec3 direction) {
    float size;vec2 scale,offset,old;float blend;
    if(probe==4u) {
        vec4 global=GI_PARAM(85),where=GI_PARAM(86);
        if(global.w<1.0)return GI_PARAM(3).rgb;
        size=global.z;scale=global.xy;offset=where.xy;old=where.zw;blend=clamp(global.w-1.0,0.0,1.0);
    } else {
        uint i=88u+probe*4u;vec4 atlas=GI_PARAM(i+3u);
        if(GI_PARAM(i).w<1.0)return GI_PARAM(3).rgb;
        size=GI_PARAM(i+2u).w;scale=atlas.xy;offset=atlas.zw;blend=clamp(GI_PARAM(i).w-1.0,0.0,1.0);
        float tile=floor(offset.x/(4.0*scale.x)+.5);
        old=offset+vec2((tile-2.0*floor(tile*.5))<.5?4.0*scale.x:-4.0*scale.x,0.0);
    }
    vec3 value=giProbeTexture(direction,size,scale,offset);
    if(blend<1.0)value=mix(giProbeTexture(direction,size,scale,old),value,blend);
    return max(value,vec3(0,0,0));
}
vec3 giFallbackAmbient(vec3 position,vec3 normal) {
    float total=0.0,last=-1.0;vec3 result=vec3(0,0,0);
    for(uint probe=0u;probe<4u;++probe) {
        uint i=88u+probe*4u,m=136u+probe*4u;
        if(GI_PARAM(i).w<1.0||((uint(GI_PARAM(m+3u).z)&255u)&uint(GI_PARAM(0).w))==0u)continue;
        vec3 local=vec3(dot(GI_PARAM(m),vec4(position,1)),dot(GI_PARAM(m+1u),vec4(position,1)),dot(GI_PARAM(m+2u),vec4(position,1)));
        vec3 edge=(vec3(1,1,1)-abs(local))*(GI_PARAM(i+2u).xyz-GI_PARAM(i+1u).xyz)*.5;
        float weight=clamp(min(edge.x,min(edge.y,edge.z))/GI_PARAM(i+1u).w,0.0,1.0);
        vec4 options=GI_PARAM(m+3u);
        float replace=weight*clamp(options.x-last,0.0,1.0);last=mix(last>0.0?last:options.x,options.x,weight);
        result=result*(1.0-replace)+giAmbientAxis(probe,normal)*weight*options.y;
        total=total*(1.0-replace)+weight;
    }
    if(total<1.0){result+=giAmbientAxis(4u,normal)*(1.0-total);total=1.0;}
    return result/max(total,.00001);
}
float giCloudVisibility(vec3 position){
    if(GI_PARAM(160).w<.5)return 1.0;
    vec2 uv=(position.xz-GI_PARAM(160).xy-(position.y-GI_PARAM(160).z)*GI_PARAM(161).xy)/GI_PARAM(161).w+.5;
    return 1.0-clamp(GI_CLOUD_SHADOW(uv),0.0,1.0)*GI_PARAM(161).z;
}
float giSunVisibility(vec3 position,vec3 normal,float previous) {
    if(WORLD_SUN(67).x>.5)return sunCascadeVisibility(position,normal);
    if(GI_PARAM(156).x<=0.0)return 1.0;
    vec4 p=vec4(position+normal*GI_PARAM(156).z,1);
    vec4 clip=vec4(dot(GI_PARAM(152),p),dot(GI_PARAM(153),p),dot(GI_PARAM(154),p),dot(GI_PARAM(155),p));
    if(clip.w<=.000001)return previous;
    vec3 ndc=clip.xyz/clip.w;
    if(max(abs(ndc.x),max(abs(ndc.y),abs(ndc.z)))>=1.0)return previous;
    vec2 uv=ndc.xy*vec2(.5,-.5)+vec2(.5,.5);
    float depth=ndc.z*.5+.5-GI_PARAM(156).y;
    float visible=step(depth,GI_SHADOW(GI_PARAM(157).xy+uv*GI_PARAM(157).zw));
    return mix(1.0,visible,GI_PARAM(156).x);
}
vec3 giNormal(uint packed) {
    vec3 n=vec3(float(int(packed<<24u)>>24),float(int(packed<<16u)>>24),float(int(packed<<8u)>>24))/127.0;
    return normalize(n);
}
vec3 giAlbedo(uint packed) {
    vec3 c=vec3(float(packed&255u),float((packed>>8u)&255u),float((packed>>16u)&255u))/255.0;return c*c;
}
vec3 giEmission(uint packed) {
    return vec3(float(packed&511u),float((packed>>9u)&511u),float((packed>>18u)&511u))*exp2(float(packed>>27u)-24.0);
}
vec4 giTerrainPoint(vec2 pixel,uint field) {
    vec4 grid=GI_DATA(7);vec2 p=clamp(floor(pixel),vec2(0,0),vec2(grid.y-1.0,grid.y-1.0));
    return GI_DATA(uint(grid.x)+(uint(p.y)*uint(grid.y)+uint(p.x))*2u+field);
}
vec4 giTerrainLinear(vec2 pixel) {
    vec2 low=floor(pixel),f=fract(pixel);
    return mix(mix(giTerrainPoint(low,0u),giTerrainPoint(low+vec2(1,0),0u),f.x),
        mix(giTerrainPoint(low+vec2(0,1),0u),giTerrainPoint(low+vec2(1,1),0u),f.x),f.y);
}
vec3 giTerrainPrevious(vec2 pixel) {
    vec4 grid=GI_DATA(7);vec2 p=clamp(pixel,vec2(0,0),vec2(grid.y-1.0,grid.y-1.0));
    return GI_PREVIOUS(uint(grid.z)+(uint(p.y)*uint(grid.y)+uint(p.x))*3u+1u).rgb;
}
void giDistant(uint id) {
    vec4 grid=GI_DATA(7);uint size=uint(grid.y);if(id>=size*size)return;
    vec2 pixel=vec2(float(id%size),float(id/size));
    vec4 surface=giTerrainPoint(pixel,0u),albedo=giTerrainPoint(pixel,1u);
    vec3 bounce=vec3(0,0,0);
    if(GI_PARAM(0).x>.5)for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x)
        bounce+=giTerrainPrevious(pixel+vec2(x,y))*(x==0&&y==0?1.0:x==0||y==0?.75:.5);
    bounce*=GI_PARAM(159).w/6.0;
    vec3 direction=GI_PARAM(1).xyz/max(max(abs(GI_PARAM(1).x),abs(GI_PARAM(1).z)),.01);
    float shadow=1.0;
    for(int stepIndex=1;stepIndex<32;++stepIndex) {
        vec2 p=pixel+direction.xz*float(stepIndex);
        float height=surface.w+1.0+direction.y*float(stepIndex)*grid.w;
        shadow*=clamp(1.0-(giTerrainPoint(p,0u).w-height)*.5,0.0,1.0);
    }
    vec3 sky=giAmbientAxis(4u,surface.xyz)*albedo.w*GI_PARAM(5).y;
    vec3 outgoing=albedo.rgb*(GI_PARAM(2).rgb*(shadow*giCloudVisibility(vec3(GI_DATA(8).x+pixel.x*grid.w,surface.w,GI_DATA(8).y+pixel.y*grid.w))*max(dot(GI_PARAM(1).xyz,surface.xyz),0.0)*.31830988618)+bounce+sky);
    float minimum=surface.w;
    for(int y=-5;y<5;y+=2)for(int x=-5;x<5;x+=2) {
        vec2 delta=vec2(x,y);float sampleHeight=giTerrainLinear(pixel+delta).w;
        minimum=mix(minimum,min(minimum,sampleHeight),clamp(1.0-length(delta)*.05,0.0,1.0));
    }
    float a=giTerrainLinear(pixel+vec2(0,-.75)).w,b=giTerrainLinear(pixel+vec2(-.75,0)).w;
    float c=giTerrainLinear(pixel+vec2(.75,0)).w,d=giTerrainLinear(pixel+vec2(0,.75)).w;
    float low=min(surface.w,min(min(a,b),min(c,d))),gradient=abs(low-(a+b+c+d)*.25);
    uint outIndex=uint(grid.z)+id*3u;
    GI_OUTPUT(outIndex)=vec4(surface.xyz,minimum);
    GI_OUTPUT(outIndex+1u)=vec4(clamp(outgoing,0.0,64512.0),1);
    GI_OUTPUT(outIndex+2u)=vec4(albedo.w,low,gradient,1);
}
void indirectCompute(uint group,uint lane) {
#if GI_STAGE == 3
    giDistant(group*64u+lane);
#else
#if GI_STAGE == 0
    if(group==0u)for(uint i=lane;i<68u;i+=64u)GI_OUTPUT(uint(GI_DATA(5).w)+30u+i)=GI_PARAM(84u+i);
    if(GI_PARAM(0).x>.5&&GI_PARAM(5).w<.5) {
        if(lane==0u)GI_OUTPUT(uint(GI_DATA(5).w)+group)=GI_PREVIOUS(uint(GI_DATA(5).w)+group);
        return;
    }
    uint face=group%6u,probe=group/6u;
    vec3 n=giAxis(face),t=normalize(cross(abs(n.y)<.99?vec3(0,1,0):vec3(1,0,0),n)),b=cross(n,t);
    vec3 sum=vec3(0,0,0);
    GI_LOOP for(uint sampleIndex=lane;sampleIndex<2048u;sampleIndex+=64u) {
        vec2 xi=fract(vec2(.5,.5)+float(sampleIndex)*vec2(.7548776662466927,.5698402909980532));
        float phi=6.28318530718*xi.y,r=sqrt(xi.x);
        vec3 direction=t*(r*cos(phi))+b*(r*sin(phi))+n*sqrt(max(0.0,1.0-xi.x));
        sum+=giSourceAmbient(probe,direction);
    }
    giReduction[lane]=vec4(sum,0);
#elif GI_STAGE == 1
    uint brick=uint(GI_DATA(2).x)+group*2u;
    vec4 info=GI_DATA(brick);uint first=uint(info.w),count=uint(GI_DATA(brick+1u).x);
    if(lane==0u) {
        giLightMask=0u;
        for(uint light=0u;light<uint(GI_PARAM(5).z);++light) {
            vec4 l=GI_PARAM(12u+light*2u);
            if(length(l.xyz-info.xyz)<l.w+3.0)giLightMask|=1u<<light;
        }
    }
    GI_BARRIER;
    vec3 sum=vec3(0,0,0);
    GI_LOOP for(uint i=lane;i<count;i+=64u) {
        uint id=first+i,offset=uint(GI_DATA(1).w)+id*2u;
        vec4 position=GI_DATA(offset),material=GI_DATA(offset+1u);
        uint flags=uint(material.w),channels=(flags>>6u)&255u;
        vec3 normal=giNormal(floatBitsToUint(material.x)),albedo=giAlbedo(floatBitsToUint(material.y));
        vec3 origin=position.xyz+normal*.125;
        float cached=GI_PARAM(0).x>.5?GI_PREVIOUS(uint(GI_DATA(2).w)+id).x:1.0;
        if((flags&16384u)!=0u)cached=0.0;else cached=giSunVisibility(origin,normal,cached);
        GI_OUTPUT(uint(GI_DATA(2).w)+id)=vec4(cached,0,0,0);
        vec3 light=GI_PARAM(2).rgb*max(dot(normal,GI_PARAM(1).xyz),0.0)*cached*giCloudVisibility(origin);
        for(uint l=0u;l<uint(GI_PARAM(5).z);++l) {
            if((giLightMask&(1u<<l))==0u)continue;
            vec4 p=GI_PARAM(12u+l*2u),color=GI_PARAM(13u+l*2u);
            if((uint(color.w)&channels)==0u)continue;
            vec3 delta=p.xyz-origin;float distance2=dot(delta,delta);
            float attenuation=clamp(1.0-distance2*distance2/(p.w*p.w*p.w*p.w),0.0,1.0);
            float falloff=attenuation*attenuation/max(distance2,.0001);
            light+=color.rgb*(falloff*max(dot(normal,delta*inversesqrt(max(distance2,.00001))),0.0))*GI_PARAM(5).x;
        }
        vec3 ambient=GI_PARAM(0).x>.5?clamp(giAxisPrevious(uint(position.w),normal),0.0,1000.0)*GI_PARAM(4).x:vec3(0,0,0);
        vec3 emission=giEmission(floatBitsToUint(material.z));int channel=int(flags&63u)-1;
        if(channel>=0)emission*=GI_PARAM(52u+uint(channel)).rgb;
        sum+=albedo*light*.31830988618+mix(vec3(1,1,1),albedo,GI_PARAM(4).y)*ambient+
            light*GI_PARAM(4).z+emission*GI_PARAM(4).w;
    }
    giReduction[lane]=vec4(sum,0);
#elif GI_STAGE == 2
    uint probe=group/6u,side=group%6u;
    uint offset=uint(GI_DATA(1).x)+probe*7u;vec4 face=GI_DATA(offset+1u+side);
    vec4 sum=vec4(0,0,0,0);
    GI_LOOP for(uint i=lane;i<uint(face.y);i+=64u) {
        vec4 factor=GI_DATA(uint(GI_DATA(2).y)+uint(face.x)+i);
        sum+=vec4(GI_OUTPUT(uint(GI_DATA(2).z)+uint(factor.x)).rgb,1.0)*factor.y;
    }
    giReduction[lane]=sum;
#endif
    GI_BARRIER;
    for(uint stride=32u;stride>0u;stride>>=1u){if(lane<stride)giReduction[lane]+=giReduction[lane+stride];GI_BARRIER;}
    if(lane==0u) {
#if GI_STAGE == 0
        GI_OUTPUT(uint(GI_DATA(5).w)+group)=vec4(giReduction[0].rgb/2048.0,1);
#elif GI_STAGE == 1
        GI_OUTPUT(uint(GI_DATA(2).z)+group)=vec4(giReduction[0].rgb/max(float(count),1.0),1);
#elif GI_STAGE == 2
        vec4 energy=giReduction[0];
        vec3 finalEnergy=energy.rgb+giAmbientAxis(4u,giAxis(side))*face.z*GI_PARAM(5).y;
        float missing=clamp(1.0-energy.w-face.z,0.0,1.0);
        vec3 fallback=clamp(giFallbackAmbient(GI_DATA(offset).xyz,giAxis(side)),0.0,1000.0);
        vec3 luminance=vec3(.2126,.7152,.0722);
        float limit=dot(finalEnergy/max(1.0-missing,.01),luminance)*1.2;
        fallback*=min(limit/(dot(fallback,luminance)+.01),1.0);
        GI_OUTPUT(probe*6u+side)=vec4(clamp(finalEnergy+fallback*missing,0.0,64512.0),face.z);
#endif
    }
#endif
}
