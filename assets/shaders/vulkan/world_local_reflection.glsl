// Bounded placed radiance probes plus a global sky fallback. Retained material
// capture, current relighting and prepared GGX levels are independently scheduled.
// The cube atlas is the engine's representation; SSR/planar hits replace fallback.
#ifndef LOCAL_REFLECTION
bool worldLocalReflectionCapture() { return false; }
bool worldLocalReflectionFiltering() { return false; }
vec4 worldLocalReflectionOutput(vec4 color,vec3 position,vec3 normal) { return color; }
vec3 worldLocalReflectionDownsample(vec2 pixel) { return vec3(0.0,0.0,0.0); }
vec3 worldPreparedLocalReflectionRadiance(vec3 fallback,vec3 position,vec3 normal,vec3 view,float roughness) { return fallback; }
vec3 worldLocalReflectionRadiance(vec3 fallback,vec3 position,vec3 normal,vec3 view,float roughness) { return fallback; }
#else
bool worldLocalReflectionCapture() { return LOCAL_REFLECTION(0).w<-.5; }
bool worldLocalReflectionFiltering() { return (LOCAL_REFLECTION(0).w<-1.5 && LOCAL_REFLECTION(0).w>-2.5) || LOCAL_REFLECTION(0).w<-3.5; }
vec4 worldLocalReflectionOutput(vec4 color,vec3 position,vec3 normal) {
    // Opaque capture alpha is otherwise unused. Clear alpha=1 means no surface;
    // positive radial distance+1 preserves that sentinel in the RGBA16F atlas.
    if(worldLocalReflectionCapture() && !worldLocalReflectionFiltering())
        color.a=1.0+length(position-LOCAL_REFLECTION(0).xyz);
    if(LOCAL_REFLECTION(0).w<-2.5 && LOCAL_REFLECTION(0).w>-3.5)color.rgb=normalize(normal)*.5+vec3(.5,.5,.5);
    return color;
}
vec2 localReflectionAtlasPixel(vec2 pixel,int face,float level,float base,vec2 scale,vec2 offset) {
    float size=base*exp2(-level);
    pixel=clamp(pixel,vec2(.5,.5),vec2(size-.5,size-.5));
    if(face<6) {
        pixel+=vec2(float(face%3),float(face/3))*size;
        pixel.y+=4.0*base*(1.0-exp2(-level));
    } else if(face>=12) {
        int tile=face-12;
        pixel+=vec2(float(tile<2?3:2+tile%2),float(tile<2?tile:2+(tile-2)/2))*base;
        pixel.y+=base*(1.0-exp2(1.0-level));
    } else {
        int geometry=face-6;
        pixel+=vec2(float(geometry<2?3:2+geometry%2),float(geometry<2?geometry:2+(geometry-2)/2))*size;
    }
    return pixel*(scale/base)+offset;
}
vec2 placedReflectionAtlasPixel(vec2 pixel,int face,float level,float base,vec2 scale,vec2 offset) {
    float size=base*exp2(-level),stride=size+2.0;
    bool prepared=face>=12;face=face%6;
    vec2 origin=vec2(0.0,0.0);
    if(level>.5 && level<1.5)origin=vec2(prepared?3.0*stride:0.0,2.0*(base+2.0));
    else if(level>=1.5)origin=vec2(6.0*(base*.5-base*exp2(1.0-level)+2.0*(level-2.0))+(prepared?3.0*stride:0.0),
        2.0*(base+2.0)+2.0*(base*.5+2.0));
    return (origin+vec2(float(face%3),float(face/3))*stride+vec2(1.0,1.0)+pixel)*(scale/base)+offset;
}
vec2 localReflectionPixel(vec2 pixel,int face,float level) {
    if(LOCAL_REFLECTION(4).w>.5)
        return placedReflectionAtlasPixel(pixel,face,level,LOCAL_REFLECTION(2).w,LOCAL_REFLECTION(3).xy,LOCAL_REFLECTION(4).xy);
    return localReflectionAtlasPixel(pixel,face,level,LOCAL_REFLECTION(2).w,LOCAL_REFLECTION(3).xy,LOCAL_REFLECTION(4).xy);
}
vec3 localReflectionPrefilter(vec2 pixel,int face);
vec3 localReflectionPad(vec2 pixel,int face,float level);
vec3 worldLocalReflectionDownsample(vec2 pixel) {
    int face=int(LOCAL_REFLECTION(3).z);
    if(face<0) {
        float size=LOCAL_REFLECTION(2).w*exp2(-LOCAL_REFLECTION(3).w)+(LOCAL_REFLECTION(0).w<-5.5?2.0:0.0);
        vec2 tile=floor(pixel/size);
        face=int(tile.x)+3*int(tile.y);pixel-=tile*size;
    }
    if(LOCAL_REFLECTION(0).w<-5.5)return localReflectionPad(pixel,face,LOCAL_REFLECTION(3).w);
    if(LOCAL_REFLECTION(0).w<-3.5)return localReflectionPrefilter(pixel,face);
    float level=LOCAL_REFLECTION(3).w-1.0;
    vec3 sum=LOCAL_REFLECTION_SAMPLE(localReflectionPixel(pixel*2.0+vec2(-.5,-.5),face,level)).rgb;
    sum+=LOCAL_REFLECTION_SAMPLE(localReflectionPixel(pixel*2.0+vec2(.5,-.5),face,level)).rgb;
    sum+=LOCAL_REFLECTION_SAMPLE(localReflectionPixel(pixel*2.0+vec2(-.5,.5),face,level)).rgb;
    sum+=LOCAL_REFLECTION_SAMPLE(localReflectionPixel(pixel*2.0+vec2(.5,.5),face,level)).rgb;
    return sum*.25;
}
vec3 localReflectionCoordinates(vec3 direction) {
    vec3 a=abs(direction);vec2 uv;int face;
    if(a.x>=a.y && a.x>=a.z) {
        face=direction.x>=0.0?0:1;uv=vec2(direction.x>=0.0?direction.z:-direction.z,direction.y)/a.x;
    } else if(a.y>=a.z) {
        face=direction.y>=0.0?2:3;uv=vec2(-direction.x,direction.y>=0.0?-direction.z:direction.z)/a.y;
    } else {
        face=direction.z>=0.0?4:5;uv=vec2(direction.z>=0.0?-direction.x:direction.x,direction.y)/a.z;
    }
    return vec3(uv.x*.5+.5,.5-uv.y*.5,float(face));
}
vec4 localReflectionSurfaceAt(vec3 address) {
    float base=LOCAL_REFLECTION(2).w;
    // Never interpolate distance across silhouettes or atlas faces.
    vec2 pixel=floor(address.xy*base)+vec2(.5,.5);
    return LOCAL_REFLECTION_SAMPLE(localReflectionPixel(pixel,int(address.z)+6,0.0));
}
vec4 localReflectionSurface(vec3 direction) {return localReflectionSurfaceAt(localReflectionCoordinates(direction));}
vec3 localReflectionDirection(vec3 address) {
    vec2 uv=address.xy;
    float u=uv.x*2.0-1.0,v=1.0-uv.y*2.0;int face=int(address.z);
    if(face==0)return normalize(vec3(1.0,v,u));
    if(face==1)return normalize(vec3(-1.0,v,-u));
    if(face==2)return normalize(vec3(-u,1.0,-v));
    if(face==3)return normalize(vec3(-u,-1.0,v));
    if(face==4)return normalize(vec3(-u,v,1.0));
    return normalize(vec3(u,v,-1.0));
}
vec3 localReflectionPad(vec2 pixel,int face,float level) {
    float base=LOCAL_REFLECTION(2).w,size=base*exp2(-level);
    vec3 address=vec3((pixel-vec2(1.0,1.0))/size,float(face));
    if(pixel.x<1.0 || pixel.y<1.0 || pixel.x>size+1.0 || pixel.y>size+1.0)
        address=localReflectionCoordinates(localReflectionDirection(address));
    vec2 core=clamp(floor(address.xy*size)+vec2(.5,.5),vec2(.5,.5),vec2(size-.5,size-.5));
    int storage=int(address.z)+(LOCAL_REFLECTION(4).z>.5?12:0);
    return LOCAL_REFLECTION_SAMPLE(placedReflectionAtlasPixel(core,storage,level,base,LOCAL_REFLECTION(3).xy,LOCAL_REFLECTION(4).xy)).rgb;
}
vec3 placedReflectionSample(vec3 direction,float level,float base,vec2 scale,vec2 offset,bool prepared) {
    vec3 address=localReflectionCoordinates(direction);
    int face=int(address.z)+(prepared && level>.5?12:0);
    return LOCAL_REFLECTION_SAMPLE(placedReflectionAtlasPixel(address.xy*(base*exp2(-level)),face,level,base,scale,offset)).rgb;
}
vec3 localReflectionTexelDirection(vec3 address) {
    address.xy=(floor(address.xy*LOCAL_REFLECTION(2).w)+.5)/LOCAL_REFLECTION(2).w;
    return localReflectionDirection(address);
}
bool localReflectionGeometryAt(vec3 address,out vec3 position,out vec3 normal) {
    vec4 surface=localReflectionSurfaceAt(address);
    position=LOCAL_REFLECTION(0).xyz+localReflectionTexelDirection(address)*(surface.a-1.0);
    normal=surface.rgb*2.0-vec3(1.0,1.0,1.0);
    return surface.a>1.001 && dot(normal,normal)>.5;
}
bool localReflectionGeometry(vec3 direction,out vec3 position,out vec3 normal) {
    return localReflectionGeometryAt(localReflectionCoordinates(direction),position,normal);
}
// Each bilinear tap outside a cube face is reprojected to its adjacent face.
// This preserves edge energy without borrowing unrelated atlas tiles.
vec3 localReflectionAtlasTap(vec2 texel,int face,float level,float base,vec2 scale,vec2 offset,bool prepared) {
    float size=base*exp2(-level);
    vec3 address=vec3((texel+vec2(.5,.5))/size,float(face));
    if(texel.x<0.0 || texel.y<0.0 || texel.x>=size || texel.y>=size)
        address=localReflectionCoordinates(localReflectionDirection(address));
    vec2 pixel=floor(address.xy*size)+vec2(.5,.5);
    int storage=int(address.z)+(prepared && level>.5?12:0);
    return LOCAL_REFLECTION_SAMPLE(localReflectionAtlasPixel(pixel,storage,level,base,scale,offset)).rgb;
}
vec3 localReflectionAtlasBilinear(vec3 direction,float level,float base,vec2 scale,vec2 offset,bool prepared) {
    vec3 address=localReflectionCoordinates(direction);
    vec2 q=address.xy*(base*exp2(-level))-vec2(.5,.5),lo=floor(q),f=q-lo;
    int face=int(address.z);
    float size=base*exp2(-level);
    if(q.x>=0.0 && q.y>=0.0 && q.x<=size-1.0 && q.y<=size-1.0)
        return LOCAL_REFLECTION_SAMPLE(localReflectionAtlasPixel(address.xy*size,face+(prepared && level>.5?12:0),level,base,scale,offset)).rgb;
    vec3 a=localReflectionAtlasTap(lo,face,level,base,scale,offset,prepared);
    vec3 b=localReflectionAtlasTap(lo+vec2(1.0,0.0),face,level,base,scale,offset,prepared);
    vec3 c=localReflectionAtlasTap(lo+vec2(0.0,1.0),face,level,base,scale,offset,prepared);
    vec3 d=localReflectionAtlasTap(lo+vec2(1.0,1.0),face,level,base,scale,offset,prepared);
    return mix(mix(a,b,f.x),mix(c,d,f.x),f.y);
}
vec3 localReflectionSample(vec3 direction,float lod) {
    vec3 address=localReflectionCoordinates(direction);
    int face=int(address.z);vec2 uv=address.xy*LOCAL_REFLECTION(2).w;
    float base=LOCAL_REFLECTION(2).w;
    lod=clamp(lod,0.0,min(5.0,log2(base)));
    float lower=floor(lod),upper=ceil(lod);
    bool padded=LOCAL_REFLECTION(4).w>.5;
    vec3 aColor=padded?placedReflectionSample(direction,lower,base,LOCAL_REFLECTION(3).xy,LOCAL_REFLECTION(4).xy,false):
        localReflectionAtlasBilinear(direction,lower,base,LOCAL_REFLECTION(3).xy,LOCAL_REFLECTION(4).xy,false);
    if(lower==upper)return max(aColor,vec3(0.0,0.0,0.0));
    vec3 bColor=padded?placedReflectionSample(direction,upper,base,LOCAL_REFLECTION(3).xy,LOCAL_REFLECTION(4).xy,false):
        localReflectionAtlasBilinear(direction,upper,base,LOCAL_REFLECTION(3).xy,LOCAL_REFLECTION(4).xy,false);
    return max(mix(aColor,bColor,lod-lower),vec3(0.0,0.0,0.0));
}
float localReflectionGap(vec3 location) {
    vec3 radial=location-LOCAL_REFLECTION(0).xyz;
    float distance=length(radial);
    if(distance<.001)return 1e20;
    vec3 surfacePosition,surfaceNormal;
    if(!localReflectionGeometry(radial/distance,surfacePosition,surfaceNormal))return 1e20;
    // The nearest texel has a different ray. Compare against its surface plane,
    // not its radial depth, which rejects visible grazing surfaces in bands.
    float denominator=dot(surfaceNormal,radial/distance);
    if(abs(denominator)<.0001)return 1e20;
    return dot(surfaceNormal,surfacePosition-location)/denominator;
}
vec4 localReflectionTrace(vec3 position,vec3 normal,vec3 ray,float range) {
    vec3 origin=position+normal*.025;
    float originGap=localReflectionGap(origin);
    // A single probe cannot see through its own nearest surface. Reject these
    // disoccluded receivers instead of borrowing radiance through that surface.
    if(originGap<-.05)return vec4(0.0,0.0,0.0,-1.0);
    float stepSize=range/128.0,nearest=range+1.0;vec3 result=vec3(0.0,0.0,0.0);
    vec3 previousTexel=vec3(-1.0,-1.0,-1.0);
    LOCAL_REFLECTION_LOOP for(int i=1;i<=128;++i) {
        float travel=float(i)*stepSize;
        if(travel>nearest+stepSize)break;
        vec3 radial=origin+ray*travel-LOCAL_REFLECTION(0).xyz;
        if(dot(radial,radial)<.000001)continue;
        vec3 address=localReflectionCoordinates(normalize(radial));
        vec3 texel=vec3(floor(address.xy*LOCAL_REFLECTION(2).w),address.z);
        // A consecutive visit to the same captured plane has the same ray/plane
        // candidate. Keep every distinct texel, including masked silhouettes.
        if(texel.x==previousTexel.x && texel.y==previousTexel.y && texel.z==previousTexel.z)continue;
        previousTexel=texel;
        vec3 surfacePosition,surfaceNormal;
        if(!localReflectionGeometryAt(address,surfacePosition,surfaceNormal))continue;
        float denominator=dot(surfaceNormal,ray);
        if(abs(denominator)<.0001)continue;
        float candidate=dot(surfacePosition-origin,surfaceNormal)/denominator;
        if(candidate<=.05 || candidate>=nearest || candidate>range)continue;
        vec3 hitPosition=origin+ray*candidate;
        vec3 direction=normalize(hitPosition-LOCAL_REFLECTION(0).xyz);
        vec3 checkedPosition,checkedNormal;
        if(!localReflectionGeometry(direction,checkedPosition,checkedNormal))continue;
        float tolerance=.01+length(hitPosition-LOCAL_REFLECTION(0).xyz)*.002;
        if(abs(dot(checkedNormal,hitPosition-checkedPosition))>tolerance)continue;
        nearest=candidate;result=direction;
    }
    return nearest<=range?vec4(result,nearest):vec4(0.0,0.0,0.0,0.0);
}
vec3 localReflectionConvolve(vec3 direction,float alpha) {
    // Deterministic GGX convolution (split-sum N=V=R). Every tap selects its
    // own face, so a broad lobe can cross atlas seams without sampling padding.
    vec3 tangent=normalize(cross(abs(direction.y)<.99?vec3(0.0,1.0,0.0):vec3(1.0,0.0,0.0),direction));
    vec3 bitangent=cross(direction,tangent);
    float sumWeight=0.0;vec3 sum=vec3(0.0,0.0,0.0);
    LOCAL_REFLECTION_LOOP for(int i=0;i<64;++i) {
        float turn=float(i)*.61803398875;
        float xi=(float(i)+.5)/64.0,phi=6.28318530718*(turn-floor(turn));
        float cosine=sqrt((1.0-xi)/(1.0+(alpha*alpha-1.0)*xi));
        float sine=sqrt(max(0.0,1.0-cosine*cosine));
        vec3 halfVector=tangent*(cos(phi)*sine)+bitangent*(sin(phi)*sine)+direction*cosine;
        vec3 sampleDirection=reflect(-direction,halfVector);
        float w=max(0.0,dot(direction,sampleDirection));
        if(w<=0.0)continue;
        float denominator=cosine*cosine*(alpha*alpha-1.0)+1.0;
        float pdf=alpha*alpha/(12.5663706144*denominator*denominator);
        float texelArea=12.5663706144/(6.0*LOCAL_REFLECTION(2).w*LOCAL_REFLECTION(2).w);
        float lod=.5*log2(1.0/(64.0*max(pdf,.000001)*texelArea));
        sum+=localReflectionSample(sampleDirection,lod)*w;sumWeight+=w;
    }
    return sum/max(sumWeight,.00001);
}
#include "world_probe_kernel.glsl"
vec3 localReflectionPrefilter(vec2 pixel,int face) {
    float level=LOCAL_REFLECTION(3).w,base=LOCAL_REFLECTION(2).w;
    vec3 address=vec3(pixel/(base*exp2(-level)),float(face));
    vec3 direction=localReflectionDirection(address);
    // Duff's stable basis and immutable accepted Weyl-R2 GGX directions.
    float signZ=direction.z<0.0?-1.0:1.0,a=-1.0/(signZ+direction.z);
    vec3 tangent=vec3(1.0+signZ*direction.x*direction.x*a,signZ*direction.x*direction.y*a,-signZ*direction.x);
    vec3 bitangent=vec3(direction.x*direction.y*a,signZ+direction.y*direction.y*a,-direction.y);
    vec3 range=worldProbeKernelRange(int(level)),sum=vec3(0.0,0.0,0.0);
    LOCAL_REFLECTION_LOOP for(int i=0;i<int(range.y);++i) {
        vec4 tap=worldProbeKernel[int(range.x)+i];
        vec3 sampleDirection=tangent*tap.x+bitangent*tap.y+direction*tap.z;
        sum+=localReflectionSample(sampleDirection,tap.w+log2(base/128.0))*tap.z;
    }
    return sum*range.z;
}
vec3 localReflectionPreparedSample(vec3 direction,float roughness) {
    float base=LOCAL_REFLECTION(2).w;
    float lod=sqrt(clamp(roughness,0.0,1.0))*min(5.0,log2(base));
    vec3 address=localReflectionCoordinates(direction);
    int face=int(address.z);vec2 uv=address.xy*base;
    float lower=floor(lod),upper=ceil(lod);
    vec3 aColor=localReflectionAtlasBilinear(direction,lower,base,LOCAL_REFLECTION(3).xy,LOCAL_REFLECTION(4).xy,true);
    if(lower==upper)return max(aColor,vec3(0.0,0.0,0.0));
    vec3 bColor=localReflectionAtlasBilinear(direction,upper,base,LOCAL_REFLECTION(3).xy,LOCAL_REFLECTION(4).xy,true);
    return max(mix(aColor,bColor,lod-lower),vec3(0.0,0.0,0.0));
}

uint localLightReceiver();
vec3 worldPlacedReflectionRadiance(vec3 fallback,vec3 position,vec3 normal,vec3 view,float roughness) {
    if(dot(normal,view)<0.0)normal=-normal;
    vec3 ray=reflect(-view,normal);
    vec3 safeRay=vec3(ray.x<0.0?min(ray.x,-.00001):max(ray.x,.00001),
        ray.y<0.0?min(ray.y,-.00001):max(ray.y,.00001),ray.z<0.0?min(ray.z,-.00001):max(ray.z,.00001));
    vec4 indirect=worldIndirectNear(position,normal);
    vec3 sum=vec3(0.0,0.0,0.0);float total=0.0,lastPriority=-1.0;
    LOCAL_REFLECTION_LOOP for(int probe=0;probe<4;++probe) {
        int i=4+probe*4;
        if(LOCAL_REFLECTION(i).w<.5)continue;
        int meta=52+probe*4;
        vec4 options=LOCAL_REFLECTION(meta+3);
        uint flags=uint(options.z+.5);
        if(((flags&255u)&localLightReceiver())==0u)continue;
        vec4 r0=LOCAL_REFLECTION(meta),r1=LOCAL_REFLECTION(meta+1),r2=LOCAL_REFLECTION(meta+2);
        vec3 local=vec3(dot(r0,vec4(position,1.0)),dot(r1,vec4(position,1.0)),dot(r2,vec4(position,1.0)));
        vec3 halfSize=(LOCAL_REFLECTION(i+2).xyz-LOCAL_REFLECTION(i+1).xyz)*.5;
        vec3 edge=(vec3(1.0,1.0,1.0)-abs(local))*halfSize;
        float weight=clamp(min(edge.x,min(edge.y,edge.z))/LOCAL_REFLECTION(i+1).w,0.0,1.0);
        if(weight<=.0001)continue;
        vec3 direction=ray;
        if((flags&256u)!=0u) {
            vec3 localRay=vec3(dot(r0.xyz,ray),dot(r1.xyz,ray),dot(r2.xyz,ray));
            vec3 safeLocal=vec3(localRay.x<0.0?min(localRay.x,-.000001):max(localRay.x,.000001),
                localRay.y<0.0?min(localRay.y,-.000001):max(localRay.y,.000001),
                localRay.z<0.0?min(localRay.z,-.000001):max(localRay.z,.000001));
            vec3 exitDistance=max((-vec3(1.0,1.0,1.0)-local)/safeLocal,(vec3(1.0,1.0,1.0)-local)/safeLocal);
            float range=min(exitDistance.x,min(exitDistance.y,exitDistance.z));
            direction=normalize(position+ray*range-LOCAL_REFLECTION(i).xyz);
        }
        vec3 address=localReflectionCoordinates(direction);
        int face=int(address.z);float base=LOCAL_REFLECTION(i+2).w;
        vec2 pixel=address.xy*base;
        float lod=sqrt(clamp(roughness,0.0,1.0))*min(5.0,log2(base)),lower=floor(lod),upper=ceil(lod);
        vec4 atlas=LOCAL_REFLECTION(i+3);
        vec3 a=placedReflectionSample(direction,lower,base,atlas.xy,atlas.zw,true);
        vec3 b=placedReflectionSample(direction,upper,base,atlas.xy,atlas.zw,true);
        float blend=clamp(LOCAL_REFLECTION(i).w-1.0,0.0,1.0);
        if(blend<1.0) {
            float tile=floor(atlas.z/(4.0*atlas.x)+.5);
            vec2 previous=atlas.zw+vec2((tile-2.0*floor(tile*.5))<.5?4.0*atlas.x:-4.0*atlas.x,0.0);
            vec3 oldA=placedReflectionSample(direction,lower,base,atlas.xy,previous,true);
            vec3 oldB=placedReflectionSample(direction,upper,base,atlas.xy,previous,true);
            a=mix(oldA,a,blend);b=mix(oldB,b,blend);
        }
        float overrideWeight=weight*clamp(options.x-lastPriority,0.0,1.0);
        lastPriority=mix(lastPriority>0.0?lastPriority:options.x,options.x,weight);
        sum=sum*(1.0-overrideWeight)+max(mix(a,b,lod-lower),vec3(0.0,0.0,0.0))*(weight*options.y*worldIndirectReflectionScale(position,indirect,normal,LOCAL_REFLECTION(20+probe),false));
        total=total*(1.0-overrideWeight)+weight;
    }
    if(total<1.0) {
    if(LOCAL_REFLECTION(1).w>=1.0) {
        vec4 global=LOCAL_REFLECTION(1),offset=LOCAL_REFLECTION(2);
        float level=sqrt(clamp(roughness,0.0,1.0))*5.0,lo=floor(level),hi=ceil(level);
        vec3 a=placedReflectionSample(ray,lo,global.z,global.xy,offset.xy,true);
        vec3 b=placedReflectionSample(ray,hi,global.z,global.xy,offset.xy,true);
        float blend=clamp(global.w-1.0,0.0,1.0);
        if(blend<1.0) {
            a=mix(placedReflectionSample(ray,lo,global.z,global.xy,offset.zw,true),a,blend);
            b=mix(placedReflectionSample(ray,hi,global.z,global.xy,offset.zw,true),b,blend);
        }
        fallback=max(mix(a,b,level-lo),vec3(0.0,0.0,0.0));
    }
        fallback=worldIndirectDistantReflection(position,normal,ray,fallback);
        fallback*=worldIndirectReflectionScale(position,indirect,normal,vec4(0.0,0.0,0.0,0.0),true);
        sum+=fallback*(1.0-total);total=1.0;
    }
    return sum/max(total,.00001);
}
vec3 worldPreparedLocalReflectionRadiance(vec3 fallback,vec3 position,vec3 normal,vec3 view,float roughness) {
    if(LOCAL_REFLECTION(0).w<=1.5 || !WORLD_SCENE_RADIANCE)return fallback;
    if(LOCAL_REFLECTION(0).w>2.5)return worldPlacedReflectionRadiance(fallback,position,normal,view,roughness);
    vec3 lo=LOCAL_REFLECTION(1).xyz,hi=LOCAL_REFLECTION(2).xyz;
    vec3 edge=min(position-lo,hi-position);
    float weight=smoothstep(0.0,LOCAL_REFLECTION(1).w,min(edge.x,min(edge.y,edge.z)));
    if(weight<=0.0)return fallback;
    if(dot(normal,view)<0.0)normal=-normal;
    vec3 ray=reflect(-view,normal);
    vec3 safeRay=vec3(ray.x<0.0?min(ray.x,-.00001):max(ray.x,.00001),
        ray.y<0.0?min(ray.y,-.00001):max(ray.y,.00001),ray.z<0.0?min(ray.z,-.00001):max(ray.z,.00001));
    vec3 exitDistance=max((lo-position)/safeRay,(hi-position)/safeRay);
    float range=min(exitDistance.x,min(exitDistance.y,exitDistance.z));
    vec3 direction=normalize(position+ray*range-LOCAL_REFLECTION(0).xyz);
    // Prepared maps are a box-projected fallback, not a visibility trace.
    // Screen and planar reflections still provide surface-accurate hits.
    return mix(fallback,localReflectionPreparedSample(direction,roughness),weight);
}
vec3 worldLocalReflectionRadiance(vec3 fallback,vec3 position,vec3 normal,vec3 view,float roughness) {
    if(LOCAL_REFLECTION(0).w<.5 || !WORLD_SCENE_RADIANCE)return fallback;
    if(LOCAL_REFLECTION(0).w>1.5)return worldPreparedLocalReflectionRadiance(fallback,position,normal,view,roughness);
    vec3 lo=LOCAL_REFLECTION(1).xyz,hi=LOCAL_REFLECTION(2).xyz;
    vec3 edge=min(position-lo,hi-position);
    float weight=smoothstep(0.0,LOCAL_REFLECTION(1).w,min(edge.x,min(edge.y,edge.z)));
    if(weight<=0.0)return fallback;
    if(dot(normal,view)<0.0)normal=-normal;
    vec3 ray=reflect(-view,normal);
    vec3 safeRay=vec3(ray.x<0.0?min(ray.x,-.00001):max(ray.x,.00001),
        ray.y<0.0?min(ray.y,-.00001):max(ray.y,.00001),ray.z<0.0?min(ray.z,-.00001):max(ray.z,.00001));
    vec3 exitDistance=max((lo-position)/safeRay,(hi-position)/safeRay);
    float range=min(exitDistance.x,min(exitDistance.y,exitDistance.z));
    vec3 direction=normalize(position+ray*range-LOCAL_REFLECTION(0).xyz);
    vec4 hit=localReflectionTrace(position,normal,ray,range+.05);
    if(hit.w<0.0)return fallback;
    float angularScale=1.0;
    if(hit.w>0.0) {
        direction=hit.xyz;
        // Convert the receiver's lobe footprint to the probe's angular domain.
        float captureDistance=max(.05,localReflectionSurface(direction).a-1.0);
        angularScale=clamp(hit.w/captureDistance,.1,4.0);
    }
    // A rough lobe can still contain sources when its central ray misses. Keep
    // the broad box-filter approximation there, but never invent a mirror hit.
    else weight*=smoothstep(.1,.35,roughness);
    if(weight<=0.0)return fallback;
    return mix(fallback,localReflectionConvolve(direction,max(.001,roughness*roughness*angularScale)),weight);
}
#endif
