// Current-view, perspective-correct depth traversal. Source radiance is captured
// before tracing, then blended before the receiver's DFG/material response.
// This finite, single-layer trace/filter is independent of the retail raymarcher.
#ifndef SCREEN_REFLECTION
bool worldScreenReflectionCapture() { return false; }
bool worldScreenReflectionBuildingTiles() { return false; }
vec4 worldScreenReflectionTileBounds(vec2 pixel) { return vec4(0.0,0.0,0.0,0.0); }
bool worldPreparedScreenReflectionRadiance(vec3 fallback,vec3 position,float roughness,out vec3 radiance) { radiance=fallback;return false; }
vec3 worldScreenReflectionRadiance(vec3 fallback,vec3 position,vec3 normal,vec3 view,float roughness) { return fallback; }
#else
#ifndef SCREEN_REFLECTION_LOOP
#define SCREEN_REFLECTION_LOOP
#endif
bool worldScreenReflectionCapture() { return SCREEN_REFLECTION(8).w<-.5; }
bool worldScreenReflectionBuildingTiles() { return SCREEN_REFLECTION(8).w<-1.5; }
vec4 screenReflectionTransform(vec4 p,int offset) {
    return vec4(dot(SCREEN_REFLECTION(offset),p),dot(SCREEN_REFLECTION(offset+1),p),
        dot(SCREEN_REFLECTION(offset+2),p),dot(SCREEN_REFLECTION(offset+3),p));
}
float screenReflectionDepth(vec2 uv) {
    if(min(uv.x,uv.y)<0.0 || max(uv.x,uv.y)>1.0)return 1e20;
    vec2 dimensions=SCREEN_REFLECTION(8).xy;
    vec2 pixel=clamp(floor(vec2(uv.x,1.0-uv.y)*dimensions),vec2(0.0,0.0),dimensions-vec2(1.0,1.0));
    float depth=SCREEN_REFLECTION_DEPTH(pixel);
    if(depth>=.999999)return 1e20;
    vec2 center=(pixel+.5)/dimensions;center.y=1.0-center.y;
    // Applying VP after its inverse cancels. Clip W is the reciprocal of the
    // inverse transform's homogeneous W; no world-position round trip is needed.
    return 1.0/dot(SCREEN_REFLECTION(7),vec4(center*2.0-1.0,depth*2.0-1.0,1.0));
}
vec4 worldScreenReflectionTileBounds(vec2 pixel) {
    vec2 dimensions=SCREEN_REFLECTION(8).xy;
    vec2 base=floor(pixel)*8.0;
    float nearest=1e20,farthest=0.0;
    SCREEN_REFLECTION_LOOP for(int y=0;y<8;++y) SCREEN_REFLECTION_LOOP for(int x=0;x<8;++x) {
        vec2 p=base+vec2(float(x)+.5,float(y)+.5);
        if(p.x>=dimensions.x || p.y>=dimensions.y)continue;
        float z=screenReflectionDepth(vec2(p.x/dimensions.x,1.0-p.y/dimensions.y));
        if(z>=1e19)continue;
        if(z<=0.0 || z>60000.0)return vec4(0.0,65504.0,1.0,1.0);
        nearest=min(nearest,z);farthest=max(farthest,z);
    }
    if(farthest==0.0)return vec4(-1.0,-1.0,0.0,1.0);
    // Expand beyond half-float rounding and reconstruction error. A coarse
    // bound may admit extra work, but must never reject a thin depth sample.
    return vec4(max(0.0,nearest*.998-.002),farthest*1.002+.002,0.0,1.0);
}
float screenReflectionBudget() { return SCREEN_REFLECTION(10).z>0.0?SCREEN_REFLECTION(10).z:4096.0; }
float screenReflectionRoughnessCutoff() { return SCREEN_REFLECTION(10).w>0.0?SCREEN_REFLECTION(10).w:.75; }
float screenReflectionRoughnessWeight(float roughness) {
    float end=screenReflectionRoughnessCutoff();
    return 1.0-smoothstep(end-(screenReflectionBudget()<4096.0?.2:.3),end,roughness);
}
vec3 screenReflectionFiltered(vec2 uv,float roughness,float depth,float travel) {
    vec2 dimensions=SCREEN_REFLECTION(8).xy;
    float radius=roughness*roughness*min(24.0,travel*dimensions.y/max(depth,1.0)*.5);
    vec3 sum=vec3(0.0,0.0,0.0);float weight=0.0;
    SCREEN_REFLECTION_LOOP for(int y=-1;y<=1;++y) SCREEN_REFLECTION_LOOP for(int x=-1;x<=1;++x) {
        vec2 tap=uv+vec2(float(x),float(y))*radius/dimensions;
        if(min(tap.x,tap.y)<=0.0 || max(tap.x,tap.y)>=1.0)continue;
        float z=screenReflectionDepth(tap);
        float w=exp2(-float(x*x+y*y))*
            (1.0-smoothstep(SCREEN_REFLECTION(9).x,SCREEN_REFLECTION(9).x+max(.02,roughness*travel*.15),max(0.0,depth-z)));
        vec2 nativePixel=clamp(vec2(tap.x,1.0-tap.y)*dimensions,vec2(.5,.5),dimensions-vec2(.5,.5));
        vec2 nativeUv=nativePixel*(SCREEN_REFLECTION(10).xy/dimensions);
        sum+=max(SCREEN_REFLECTION_SAMPLE(nativeUv).rgb,vec3(0.0,0.0,0.0))*w;weight+=w;
    }
    return sum/max(weight,.000001);
}
// The ordinary material consumer reads the prepared result. Keep the legacy
// reference marcher outside this call graph, including during shader compilation.
bool worldPreparedScreenReflectionRadiance(vec3 fallback,vec3 position,float roughness,out vec3 radiance) {
    radiance=fallback;
    if(SCREEN_REFLECTION(8).w<.5 || roughness>=screenReflectionRoughnessCutoff() || !WORLD_SCENE_RADIANCE)return false;
#ifdef SCREEN_REFLECTION_PREPARED
    if(SCREEN_REFLECTION(8).w>2.5 && SCREEN_REFLECTION_PREPARED_ELIGIBLE) {
        vec4 clip=screenReflectionTransform(vec4(position,1.0),0);
        if(clip.w>.05) {
            vec2 uv=clip.xy/clip.w*.5+.5;
            vec2 p=floor(vec2(uv.x,1.0-uv.y)*SCREEN_REFLECTION(8).xy);
            if(min(p.x,p.y)>=0.0 && p.x<SCREEN_REFLECTION(8).x && p.y<SCREEN_REFLECTION(8).y) {
                vec4 resolved=SCREEN_REFLECTION_PREPARED(p);
#ifdef SCREEN_REFLECTION_CONFIDENCE
                if(SCREEN_REFLECTION(8).w>3.5)resolved.a=SCREEN_REFLECTION_CONFIDENCE(p);
#endif
                float depth=screenReflectionDepth(uv);
                if(resolved.a>=0.0 && abs(depth-clip.w)<=max(.002,clip.w*.0002)) {
                    radiance=mix(fallback,max(resolved.rgb,vec3(0.0,0.0,0.0)),clamp(resolved.a,0.0,1.0));
                    return true;
                }
            }
        }
    }
#endif
    return false;
}
vec3 worldScreenReflectionRadiance(vec3 fallback,vec3 position,vec3 normal,vec3 view,float roughness) {
    if(SCREEN_REFLECTION(8).w<.5 || roughness>=screenReflectionRoughnessCutoff() || !WORLD_SCENE_RADIANCE)return fallback;

    vec3 prepared;
    if(worldPreparedScreenReflectionRadiance(fallback,position,roughness,prepared))return prepared;
    // Unenrolled receivers use probes, never the old inline search.
    if(SCREEN_REFLECTION(8).w>3.5)return fallback;
    if(dot(normal,view)<0.0)normal=-normal;
    vec3 ray=reflect(-view,normal);
    vec3 origin=position+(normal+ray)*SCREEN_REFLECTION(9).y;
    vec4 start=screenReflectionTransform(vec4(origin,1.0),0);
    vec4 end=screenReflectionTransform(vec4(origin+ray*SCREEN_REFLECTION(8).z,1.0),0);
    if(start.w<=.05)return fallback;
    // Clip against the near plane before dividing. Rays towards the camera are
    // valid, but the unobservable part behind the near plane must never wrap.
    float nearStart=start.z+start.w,nearEnd=end.z+end.w;
    float clippedFraction=nearEnd<.001?clamp((nearStart-.001)/(nearStart-nearEnd),0.0,1.0):1.0;
    end=mix(start,end,clippedFraction);
    if(end.w<=.001)return fallback;
    vec2 a=start.xy/start.w*.5+.5,b=end.xy/end.w*.5+.5;
    vec2 delta=b-a;
    float limit=1.0;
    if(delta.x>0.0)limit=min(limit,(.999-a.x)/delta.x);
    if(delta.x<0.0)limit=min(limit,(.001-a.x)/delta.x);
    if(delta.y>0.0)limit=min(limit,(.999-a.y)/delta.y);
    if(delta.y<0.0)limit=min(limit,(.001-a.y)/delta.y);
    if(limit<=0.0)return fallback;
    vec2 dimensions=SCREEN_REFLECTION(8).xy;
    float pixels=max(abs(delta.x)*dimensions.x,abs(delta.y)*dimensions.y)*limit;
    if(pixels<2.0)return fallback;
    // Reference quality retains pixel-sized steps. Bounded quality spans the
    // same ray with fewer samples; missed thin sources use the local fallback.
    float steps=min(screenReflectionBudget(),ceil(pixels));
    float increment=limit/steps;
    float previousZ=start.w;
    vec3 pending=fallback;
    float pendingEnd=-1.0,pendingConfidence=-1.0;
    SCREEN_REFLECTION_LOOP for(int i=1;i<=4096;++i) {
        if(float(i)>steps)break;
        float t=float(i)*increment;
        if(pendingEnd>=0.0 && t>pendingEnd+increment)return pending;
        vec2 uv=a+delta*t;
        if(SCREEN_REFLECTION(9).z>0.0 && pendingEnd<0.0 &&
           min(uv.x,uv.y)>=0.0 && max(uv.x,uv.y)<1.0) {
            vec2 p=vec2(uv.x,1.0-uv.y)*dimensions;
            vec2 stride=vec2(delta.x,-delta.y)*dimensions*increment;
            vec2 tile=floor(p/8.0);
            vec2 boundary=(tile+step(vec2(0.0,0.0),stride))*8.0;
            vec2 distance=vec2(abs(stride.x)>.000001?(boundary.x-p.x)/stride.x:1e20,
                               abs(stride.y)>.000001?(boundary.y-p.y)/stride.y:1e20);
            int skip=int(min(steps-float(i),floor(min(distance.x,distance.y)-.001)));
            if(skip>0) {
                vec2 address=(tile+vec2(.5,dimensions.y+.5))*(SCREEN_REFLECTION(10).xy/dimensions);
                vec4 bounds=SCREEN_REFLECTION_SAMPLE(address);
                float lastT=float(i+skip)*increment;
                float lastZ=1.0/mix(1.0/start.w,1.0/end.w,lastT);
                if(bounds.z<.5 && (bounds.x<0.0 || max(previousZ,lastZ)<bounds.x ||
                   min(previousZ,lastZ)>bounds.y+SCREEN_REFLECTION(9).x)) {
                    i+=skip;previousZ=lastZ;continue;
                }
            }
        }
        float z=1.0/mix(1.0/start.w,1.0/end.w,t);
        float sceneZ=screenReflectionDepth(uv);
        float thickness=SCREEN_REFLECTION(9).x;
        if(float(i)*pixels/steps>2.0 && max(previousZ,z)>=sceneZ && min(previousZ,z)<=sceneZ+thickness) {
            // Solve the candidate's perspective depth, then validate at that
            // location. Bisecting the depth texture can jump to the background
            // when a thin foreground surface occupies only part of the step.
            float reciprocalDelta=1.0/end.w-1.0/start.w;
            float hit=abs(reciprocalDelta)>.00000001?
                (1.0/sceneZ-1.0/start.w)/reciprocalDelta:t;
            float surfaceHit=hit;
            hit=clamp(hit,t-increment,t);
            uv=a+delta*hit;
            float hitZ=1.0/mix(1.0/start.w,1.0/end.w,hit);
            sceneZ=screenReflectionDepth(uv);
            if(abs(hitZ-sceneZ)<=thickness) {
                float edge=min(min(uv.x,uv.y),min(1.0-uv.x,1.0-uv.y));
                // Captured ordinary SSR border factor: a linear five-percent
                // fade. Other confidence factors remain independent choices.
                float weight=clamp(edge*20.0,0.0,1.0)*screenReflectionRoughnessWeight(roughness);
                float worldFraction=(hit/end.w)/mix(1.0/start.w,1.0/end.w,hit);
                // World ray length uses perspective-correct interpolation of
                // the endpoints; the clipped endpoint may be closer than range.
                float rangeFade=1.0-smoothstep(.85,1.0,worldFraction*clippedFraction);
                float travel=SCREEN_REFLECTION(8).z*worldFraction*clippedFraction;
                // Thickness admits uncertain hits near silhouettes. Fade that
                // uncertainty into the local source instead of giving the whole
                // depth slab full mirror strength and widening thin geometry.
                float confidence=1.0-smoothstep(thickness*.05,thickness,abs(hitZ-sceneZ));
                vec3 result=mix(fallback,screenReflectionFiltered(uv,roughness,sceneZ,travel),weight*rangeFade*confidence);
                if(surfaceHit<=t)return result;
                // Approaching rays enter the thickness band before crossing the
                // surface. Seek that crossing, retaining the best band sample
                // if a subpixel source disappears before its exact intersection.
                if(pendingEnd<0.0)pendingEnd=surfaceHit;
                if(confidence>pendingConfidence) { pending=result;pendingConfidence=confidence; }
            }
        }
        previousZ=z;
    }
    return pendingEnd>=0.0?pending:fallback;
}
#ifdef SCREEN_SURFACE_NORMAL
vec4 traceWorldSurfaceReflection(vec3 position,vec3 normal,vec3 view,float roughness) {
    if(SCREEN_REFLECTION(8).w<.5 || roughness>=screenReflectionRoughnessCutoff() || !WORLD_SCENE_RADIANCE)return vec4(0.0,0.0,0.0,0.0);
    // Unenrolled receivers use probes, never the old inline search.
    if(SCREEN_REFLECTION(8).w>3.5)return vec4(0.0,0.0,0.0,0.0);
    if(dot(normal,view)<0.0)normal=-normal;
    vec3 ray=reflect(-view,normal);
    vec3 origin=position+(normal+ray)*SCREEN_REFLECTION(9).y;
    vec4 start=screenReflectionTransform(vec4(origin,1.0),0);
    vec4 end=screenReflectionTransform(vec4(origin+ray*SCREEN_REFLECTION(8).z,1.0),0);
    if(start.w<=.05)return vec4(0.0,0.0,0.0,0.0);
    // Clip against the near plane before dividing. Rays towards the camera are
    // valid, but the unobservable part behind the near plane must never wrap.
    float nearStart=start.z+start.w,nearEnd=end.z+end.w;
    float clippedFraction=nearEnd<.001?clamp((nearStart-.001)/(nearStart-nearEnd),0.0,1.0):1.0;
    end=mix(start,end,clippedFraction);
    if(end.w<=.001)return vec4(0.0,0.0,0.0,0.0);
    vec2 a=start.xy/start.w*.5+.5,b=end.xy/end.w*.5+.5;
    vec2 delta=b-a;
    float limit=1.0;
    if(delta.x>0.0)limit=min(limit,(.999-a.x)/delta.x);
    if(delta.x<0.0)limit=min(limit,(.001-a.x)/delta.x);
    if(delta.y>0.0)limit=min(limit,(.999-a.y)/delta.y);
    if(delta.y<0.0)limit=min(limit,(.001-a.y)/delta.y);
    if(limit<=0.0)return vec4(0.0,0.0,0.0,0.0);
    vec2 dimensions=SCREEN_REFLECTION(8).xy;
    float pixels=max(abs(delta.x)*dimensions.x,abs(delta.y)*dimensions.y)*limit;
    if(pixels<2.0)return vec4(0.0,0.0,0.0,0.0);
    // Reference quality retains pixel-sized steps. Bounded quality spans the
    // same ray with fewer samples; missed thin sources use the local fallback.
    float steps=min(screenReflectionBudget(),ceil(pixels));
    float increment=limit/steps;
    float previousZ=start.w;
    vec4 pending=vec4(0.0,0.0,0.0,0.0);
    float pendingEnd=-1.0,pendingConfidence=-1.0;
    SCREEN_REFLECTION_LOOP for(int i=1;i<=4096;++i) {
        if(float(i)>steps)break;
        float t=float(i)*increment;
        if(pendingEnd>=0.0 && t>pendingEnd+increment)return pending;
        vec2 uv=a+delta*t;
        if(SCREEN_REFLECTION(9).z>0.0 && pendingEnd<0.0 &&
           min(uv.x,uv.y)>=0.0 && max(uv.x,uv.y)<1.0) {
            vec2 p=vec2(uv.x,1.0-uv.y)*dimensions;
            vec2 stride=vec2(delta.x,-delta.y)*dimensions*increment;
            vec2 tile=floor(p/8.0);
            vec2 boundary=(tile+step(vec2(0.0,0.0),stride))*8.0;
            vec2 distance=vec2(abs(stride.x)>.000001?(boundary.x-p.x)/stride.x:1e20,
                               abs(stride.y)>.000001?(boundary.y-p.y)/stride.y:1e20);
            int skip=int(min(steps-float(i),floor(min(distance.x,distance.y)-.001)));
            if(skip>0) {
                vec2 address=(tile+vec2(.5,dimensions.y+.5))*(SCREEN_REFLECTION(10).xy/dimensions);
                vec4 bounds=SCREEN_REFLECTION_SAMPLE(address);
                float lastT=float(i+skip)*increment;
                float lastZ=1.0/mix(1.0/start.w,1.0/end.w,lastT);
                if(bounds.z<.5 && (bounds.x<0.0 || max(previousZ,lastZ)<bounds.x ||
                   min(previousZ,lastZ)>bounds.y+SCREEN_REFLECTION(9).x)) {
                    i+=skip;previousZ=lastZ;continue;
                }
            }
        }
        float z=1.0/mix(1.0/start.w,1.0/end.w,t);
        float sceneZ=screenReflectionDepth(uv);
        float thickness=SCREEN_REFLECTION(9).x;
        if(float(i)*pixels/steps>2.0 && max(previousZ,z)>=sceneZ && min(previousZ,z)<=sceneZ+thickness) {
            // Solve the candidate's perspective depth, then validate at that
            // location. Bisecting the depth texture can jump to the background
            // when a thin foreground surface occupies only part of the step.
            float reciprocalDelta=1.0/end.w-1.0/start.w;
            float hit=abs(reciprocalDelta)>.00000001?
                (1.0/sceneZ-1.0/start.w)/reciprocalDelta:t;
            float surfaceHit=hit;
            hit=clamp(hit,t-increment,t);
            uv=a+delta*hit;
            float hitZ=1.0/mix(1.0/start.w,1.0/end.w,hit);
            sceneZ=screenReflectionDepth(uv);
            if(abs(hitZ-sceneZ)<=thickness) {
                float edge=min(min(uv.x,uv.y),min(1.0-uv.x,1.0-uv.y));
                // Captured ordinary SSR border factor: a linear five-percent
                // fade. Other confidence factors remain independent choices.
                float weight=clamp(edge*20.0,0.0,1.0)*screenReflectionRoughnessWeight(roughness);
                float worldFraction=(hit/end.w)/mix(1.0/start.w,1.0/end.w,hit);
                // World ray length uses perspective-correct interpolation of
                // the endpoints; the clipped endpoint may be closer than range.
                float rangeFade=1.0-smoothstep(.85,1.0,worldFraction*clippedFraction);
                float travel=SCREEN_REFLECTION(8).z*worldFraction*clippedFraction;
                // Thickness admits uncertain hits near silhouettes. Fade that
                // uncertainty into the local source instead of giving the whole
                // depth slab full mirror strength and widening thin geometry.
                float confidence=1.0-smoothstep(thickness*.05,thickness,abs(hitZ-sceneZ));
                vec4 result=vec4(screenReflectionFiltered(uv,roughness,sceneZ,travel),weight*rangeFade*confidence);
                if(surfaceHit<=t)return result;
                // Approaching rays enter the thickness band before crossing the
                // surface. Seek that crossing, retaining the best band sample
                // if a subpixel source disappears before its exact intersection.
                if(pendingEnd<0.0)pendingEnd=surfaceHit;
                if(confidence>pendingConfidence) { pending=result;pendingConfidence=confidence; }
            }
        }
        previousZ=z;
    }
    return pendingEnd>=0.0?pending:vec4(0.0,0.0,0.0,0.0);
}
#endif

#ifdef SCREEN_SURFACE_NORMAL
vec4 resolveWorldSurfaceReflection(vec2 pixel) {
    vec2 p=floor(pixel),dimensions=SCREEN_REFLECTION(8).xy;
    // Diagnostic display shares the float-sampling fullscreen pass. CopyResource/
    // vkCmdCopyImage cannot convert packed surface images into HDR scene color.
    if(SCREEN_REFLECTION(10).w<-.5) {
        vec3 value=SCREEN_SURFACE_NORMAL(p).rgb;
        if(SCREEN_REFLECTION(10).w> -1.5)value*=value;
        return vec4(value,1.0);
    }
    vec4 invalid=vec4(0.0,0.0,0.0,-1.0);
    if(min(p.x,p.y)<0.0 || p.x>=dimensions.x || p.y>=dimensions.y ||
       SCREEN_SURFACE_NORMAL(p).w<.1 || SCREEN_SURFACE_NORMAL(p).w>.9)return invalid;
    float depth=SCREEN_SURFACE_DEPTH(p);
    if(depth>=.999999 || abs(depth-SCREEN_REFLECTION_DEPTH(p))>.00001)return invalid;
    vec4 packed=SCREEN_SURFACE_NORMAL(p);
    vec3 normal=packed.xyz*2.0-1.0;
    if(dot(normal,normal)<.25)return invalid;
    vec2 uv=(p+.5)/dimensions;uv.y=1.0-uv.y;
    vec4 world=screenReflectionTransform(vec4(uv*2.0-1.0,depth*2.0-1.0,1.0),4);
    vec4 camera=screenReflectionTransform(vec4(0.0,0.0,1.0,0.0),4);
    if(abs(world.w)<.000001 || abs(camera.w)<.000001)return invalid;
    vec3 position=world.xyz/world.w;
    vec3 view=normalize(camera.xyz/camera.w-position);
    return traceWorldSurfaceReflection(position,normalize(normal),view,SCREEN_SURFACE_MATERIAL(p).w);
}
#endif

#endif
