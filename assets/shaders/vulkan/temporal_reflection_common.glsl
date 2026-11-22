// Ordinary stochastic screen reflections. Native depth is conventional [0,1];
// marching uses reverse [0,1]. Both screen UVs and motion are top-to-bottom.
#if SSR_QUALITY == 0
#define SSR_RAYS 1
#define SSR_STEPS 8
#elif SSR_QUALITY == 1
#define SSR_RAYS 2
#define SSR_STEPS 12
#elif SSR_QUALITY == 2
#define SSR_RAYS 3
#define SSR_STEPS 12
#else
#define SSR_RAYS 4
#define SSR_STEPS 16
#endif
ivec2 reflectionBound(ivec2 p,vec2 size){return clamp(p,ivec2(0,0),ivec2(size)-ivec2(1,1));}
vec4 reflectionTransform(vec4 v,int first) {
    return vec4(dot(RP(first),v),dot(RP(first+1),v),dot(RP(first+2),v),dot(RP(first+3),v));
}
vec3 reflectionProject(vec3 world) {
    vec4 clip=reflectionTransform(vec4(world,1.),0);
    return vec3(clip.x/clip.w*.5+.5,.5-clip.y/clip.w*.5,.5-clip.z/clip.w*.5);
}
vec3 reflectionToView(vec3 v){return vec3(dot(RP(9).xyz,v),dot(RP(10).xyz,v),dot(RP(11).xyz,v));}
vec3 reflectionToWorld(vec3 v){return RP(9).xyz*v.x+RP(10).xyz*v.y+RP(11).xyz*v.z;}
vec3 reflectionFeedback(vec2 uv,float mip) {
    mip=clamp(mip,0.,RP(11).w);
    int lo=int(floor(mip));float f=mip-float(lo);
    vec3 a,b;
    if(lo==0){a=FEEDBACK0(uv);b=FEEDBACK1(uv);}
    else if(lo==1){a=FEEDBACK1(uv);b=FEEDBACK2(uv);}
    else if(lo==2){a=FEEDBACK2(uv);b=FEEDBACK3(uv);}
    else {a=FEEDBACK3(uv);b=FEEDBACK4(uv);}
    if(lo==4)a=b;
    return mix(a,b,f);
}
vec3 reflectionCone(vec2 start,vec2 hit,float roughness) {
    float exponent=2./pow(roughness,4.)-2.;
    float angle=acos(pow(.244,1./(exponent+1.)));
    float diameter=2.*tan(angle*.5)*max(length(hit-start),1e-8);
    float mip=min(log2(max(diameter*max(RP(12).x,RP(12).y),1e-8)),4.);
    // Reproject at the hit object, not at the receiving surface.
    vec4 motion=MOTION_SAMPLE(hit);
    vec2 old=hit+motion.xy;
    float inner=min(log2(max((2.*sqrt(3.)-3.)*diameter*max(RP(12).x,RP(12).y),1e-8)),4.);
    vec3 result=reflectionFeedback(old,mip);
    if(diameter>.05)result=(result+reflectionFeedback(mix(old,start,.2),inner)+reflectionFeedback(mix(old,start,.4),inner))/3.;
    return result;
}
vec4 reflectionRay(vec3 halfNormal,vec3 world,vec3 incident,float roughness,float offset,float linearDepth,vec3 start) {
    vec3 ray=reflect(incident,halfNormal);
    if(ray.z < -RP(13).w)return vec4(0.,0.,0.,0.);
    vec3 delta=reflectionProject(world+.1*reflectionToWorld(ray))-start;
    float distanceSquared=dot(delta.xy,delta.xy);
    if(distanceSquared<1e-12)return vec4(0.,0.,0.,0.);
    vec3 stride=delta*(inversesqrt(distanceSquared)/float(SSR_STEPS+1));
    vec3 cursor=start+(offset+.1)*stride;
    vec3 coneStart=cursor,hit=cursor;
    float farFactor=min(1.,linearDepth/12.);farFactor*=farFactor;farFactor*=farFactor;
    float tolerance=max(abs(stride.z)*mix(.6,2.,farFactor),1e-8);
    bool found=false;
    SSR_LOOP for(int group=0;group<SSR_STEPS;group+=4) {
        if(cursor.x<=0.||cursor.y<=0.||cursor.x>=1.||cursor.y>=1.)break;
        // Four independent tests, first hit wins. The inspected ordinary branch
        // does not update its declared "steps below" counter.
        SSR_UNROLL for(int lane=0;lane<4;++lane) {
            vec3 q=cursor+float(lane)*stride;
            ivec2 address=reflectionBound(ivec2(floor(q.xy*RP(12).xy*.25)),RP(12).zw);
            float compressed=TRACE_DEPTH(address);
            float difference=compressed*compressed-q.z;
            if(!found && abs(difference-tolerance)<tolerance){hit=q;found=true;}
        }
        if(found)break;
        cursor+=4.*stride;
    }
    if(!found || hit.x<=0.||hit.y<=0.||hit.x>=1.||hit.y>=1.)return vec4(0.,0.,0.,0.);
    // Explicit missing-producer adapter: no fabricated motion for unsupported
    // geometry. Current and previous UVs must both describe a visible pixel.
    vec4 motion=MOTION_SAMPLE(hit.xy);vec2 previous=hit.xy+motion.xy;
    if(motion.w<.5 || previous.x<=0.||previous.y<=0.||previous.x>=1.||previous.y>=1.)
        return vec4(0.,0.,0.,0.);
    float edge=min(clamp(10.-abs(hit.x-.5)*20.,0.,1.),clamp(10.-abs(hit.y-.5)*20.,0.,1.));
    float rough=clamp((roughness-RP(13).y)/(RP(13).x-RP(13).y),0.,1.);
    float backward=clamp(1.-(ray.z+RP(13).z)/(RP(13).z-RP(13).w),0.,1.);
    return vec4(reflectionCone(coneStart.xy,hit.xy,roughness),rough*edge*backward);
}
vec2 reflectionNoise(ivec2 address) {
    // Preserve both UNORM16 channels in a portable four-byte texture.
    vec4 bytes=floor(NOISE_LOAD(address)*255.+.5);
    return vec2(bytes.x+256.*bytes.y,bytes.z+256.*bytes.w)/65535.;
}
vec3 reflectionGGX(vec2 xi,float roughness) {
    float alpha=roughness*roughness;
    float cosine=sqrt((1.-xi.y)/(1.+(alpha*alpha-1.)*xi.y));
    float sine=sqrt(max(0.,1.-cosine*cosine)),phi=6.283185307179586*xi.x;
    return vec3(cos(phi)*sine,sin(phi)*sine,cosine);
}
void temporalReflection(ivec2 pixel) {
#ifdef SSR_DEPTH_PASS
    if(pixel.x>=int(RP(12).z)||pixel.y>=int(RP(12).w))return;
    // Same four Gather footprints as the reference, including edge clamp.
    ivec2 base=ivec2(floor(vec2(pixel)*RP(12).xy/RP(12).zw-.5));
    float nearest=0.;
    for(int y=0;y<4;++y)for(int x=0;x<4;++x)
        nearest=max(nearest,1.-DEPTH_LOAD(reflectionBound(base+ivec2(x,y),RP(12).xy)));
    STORE_DEPTH(pixel,sqrt(max(nearest,0.)));return;
#else
    if(pixel.x>=int(RP(12).x)||pixel.y>=int(RP(12).y))return;
    vec4 result=vec4(0.,0.,0.,0.);
    float depth=DEPTH_LOAD(pixel);vec4 normalRough=NORMAL_LOAD(pixel);
    float roughness=clamp(MATERIAL_LOAD(pixel).w,.04,1.);
    if(RP(8).w>.5 && depth<.999999 && normalRough.w>.1 && normalRough.w<.9 && roughness<=RP(13).y) {
        vec2 uv=(vec2(pixel)+.5)/RP(12).xy;
        vec4 unprojected=reflectionTransform(vec4(uv.x*2.-1.,1.-uv.y*2.,depth*2.-1.,1.),4);
        vec3 world=unprojected.xyz/unprojected.w;
        vec3 toSurface=reflectionToView(world-RP(8).xyz);
        float linearDepth=toSurface.z;
        if(linearDepth>0. && linearDepth<=1000.) {
            vec3 incident=normalize(toSurface);
            vec3 n=normalize(reflectionToView(normalRough.xyz*2.-1.));
            // Duff et al.'s stable basis, around the view-space normal.
            float signZ=n.z<0.?-1.:1.,a=-1./(signZ+n.z);
            vec3 tx=vec3(1.+signZ*n.x*n.x*a,signZ*n.x*n.y*a,-signZ*n.x);
            vec3 ty=vec3(n.x*n.y*a,signZ+n.y*n.y*a,-n.y);
            int frame=int(RP(10).w);
            vec2 noise=reflectionNoise(ivec2(pixel.x&63,(pixel.y&63)+64*(frame&63)));
            float offset=reflectionNoise(ivec2(pixel.y&63,(pixel.x&63)+64*((frame+32)&63))).x;
            int rays=clamp(SSR_RAYS+int(5.*INVALID_SAMPLE(uv)),1,16);
            vec3 start=reflectionProject(world);
            SSR_LOOP for(int i=0;i<rays;++i) {
                vec2 xi=fract(vec2(float(i)/float(rays),float(REVERSE_BITS(uint(i)))*2.3283064365386963e-10)+noise);
                if(SSR_RAYS==1 && rays==1)xi=noise; // The one-ray branch does not wrap UNORM endpoints.
                float r=(SSR_RAYS==1 && rays==1)?roughness*.75:roughness;
                vec3 h=reflectionGGX(xi,r);
                result+=reflectionRay(tx*h.x+ty*h.y+n*h.z,world,incident,roughness,offset,linearDepth,start);
                offset=fract(offset+1.61803399);
            }
            result/=float(rays);result.rgb*=RP(9).w;
        }
    }
    // Temporal scene reconstruction integrates the stochastic sample sequence.
    STORE_RADIANCE(pixel,clamp(result.rgb,vec3(0.,0.,0.),vec3(65000.,65000.,65000.)));
    STORE_CONFIDENCE(pixel,clamp(result.a,0.,1.));
#endif
}
