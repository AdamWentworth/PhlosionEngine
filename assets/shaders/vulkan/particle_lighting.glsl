#define ENV_SUN PP(2)
#define ENV_AMBIENT PP(4)
#define ENV_BOUNCE(i) PP(20+i)
#define WORLD_GI_FORWARD true
uint localLightReceiver(){return uint(PP(1).w);}
#include "world_indirect.glsl"
#include "world_finite_light.glsl"
// Particle response is a separate shader variant: ordinary surface programs
// retain their original finite-light implementation and register lifetime.
FiniteLightResponse finiteLightResponseExtended(vec3 normal,vec3 view,vec3 delta,vec3 halfAxis,
        float radius,float sourceLength,float roughness,vec3 albedo,float metallic,bool finiteSource,vec3 grazing,bool transparent,float particleWrap,float particleBack) {
    FiniteLightResponse result;result.diffuse=vec3(0.0,0.0,0.0);result.specular=result.diffuse;
    if (dot(delta,delta)<1e-12) return result;
    vec3 light=delta*finiteLightRsqrt(dot(delta,delta));
    vec3 halfway=finiteLightUnit(light+view);
    float nl=clamp(dot(normal,light),0.0,1.0),lh=clamp(dot(light,halfway),0.0,1.0);
    float specularScale=1.0,diffuseScale=1.0;
    float centerNl=dot(normal,light),backNl=-centerNl,viewLight=dot(view,light);
    float roughness2=roughness*roughness;
    if (finiteSource) {
        vec3 reflection=-view+2.0*dot(view,normal)*normal;
        float dominantBlend=(sqrt(1.0-roughness2)+roughness2)*(1.0-roughness2);
        vec3 dominant=finiteLightUnit(normal+(reflection-normal)*dominantBlend);
        vec3 a=delta-halfAxis,b=delta+halfAxis,representative=delta;
        float lineScale=1.0;
        if (sourceLength>0.0) {
            vec3 segment=halfAxis*2.0;
            float projected=dot(dominant,segment);
            float denominator=sourceLength*sourceLength-projected*projected;
            float along=denominator>1e-12 ? dot(a,projected*dominant-segment)/denominator
                : -dot(a,segment)/max(dot(segment,segment),1e-12);
            along=clamp(along,0.0,1.0);
            representative=a+segment*along;
            lineScale=roughness/(roughness+sourceLength/sqrt(dot(delta,delta)));
            lineScale*=1.5707963705062866*dot(normal,finiteLightUnit(representative));
        }
        float effectiveRadius=max(radius,.01);
        vec3 offset=dominant*dot(representative,dominant)-representative;
        representative+=offset*clamp(effectiveRadius/sqrt(max(dot(offset,offset),1e-20)),0.0,1.0);
        float spread=.5*clamp(effectiveRadius/sqrt(max(dot(representative,representative),1e-20)),0.0,1.0);
        float diffuseFactor=1.0/clamp(1.0+spread,0.0,1.0);
        diffuseScale=diffuseFactor*diffuseFactor;
        float specularFactor=roughness2/clamp(roughness2+spread,0.0,1.0);
        specularScale=specularFactor*specularFactor*lineScale*clamp(radius*100.0,0.0,1.0);
        light=finiteLightUnit(representative); halfway=finiteLightUnit(light+view);
        vec3 na=finiteLightUnit(a),nb=finiteLightUnit(b);
        nl=.5*(dot(normal,na)+dot(normal,nb));
        lh=clamp(.5*(dot(na,finiteLightUnit(na+view))+dot(nb,finiteLightUnit(nb+view))),0.0,1.0);
        if (radius>0.0) {
            float inverseProduct=inversesqrt(max(dot(a,a)*dot(b,b),1e-20));
            float wrap=sqrt(clamp(radius*radius*inverseProduct/max(.5*dot(a,b)*inverseProduct+.5,1e-20),0.0,1.0));
            if (nl<wrap) { float facing=max(nl,-wrap)+wrap; nl=facing*facing/(4.0*wrap); }
        }
        // Opposite hemisphere uses the same finite-source angular integral.
        backNl=-.5*(dot(normal,na)+dot(normal,nb));
        if(radius>0.0) {
            float invProduct=inversesqrt(max(dot(a,a)*dot(b,b),1e-20));
            float angularWidth=sqrt(clamp(radius*radius*invProduct/max(.5*dot(a,b)*invProduct+.5,1e-20),0.0,1.0));
            if(backNl<angularWidth){float f=max(backNl,-angularWidth)+angularWidth;backNl=f*f/max(4.0*angularWidth,1e-20);}
        }
        backNl=clamp(backNl,0.0,1.0);
        nl=clamp(nl,0.0,1.0);
    }
    float particleAttenuation=1.0;
    if(particleWrap>=0.0) {
        float original=finiteSource?nl:centerNl;
        nl=clamp((original+particleWrap)/pow(1.0+particleWrap,2.0),0.0,1.0);
        particleAttenuation=mix(nl,centerNl>0.0?nl:nl+backNl*particleBack,clamp(-viewLight,0.0,1.0));
    }
    float nv=clamp(dot(normal,view),.00001,1.0);
    float nh=clamp(dot(normal,halfway),0.0,1.0),vh=clamp(dot(view,halfway),0.0,1.0);
    float diffuse90=(2.0*lh*lh+.5)*roughness-1.0;
    float diffuse=(1.0+diffuse90*finiteLightPow5(1.0-nl))*(1.0-.33774834871292114*roughness)
        *(1.0+diffuse90*finiteLightPow5(1.0-nv))*nl*diffuseScale/3.1415927410125732;
    float roughness4=roughness2*roughness2;
    float denominator=nh*nh*(roughness4-1.0)+1.0;
    float distribution=roughness4/(3.1415927410125732*denominator*denominator);
    float visibility=.5/(nl*sqrt(nv*nv*(1.0-roughness4)+roughness4)
        +nv*sqrt(nl*nl*(1.0-roughness4)+roughness4));
    vec3 fresnel=grazing+vec3(.04,.04,.04)+(vec3(.96,.96,.96)-grazing)*finiteLightPow5(1.0-vh);
    result.diffuse=diffuse*(albedo-albedo*metallic);
    result.specular=fresnel*min(distribution*nl*visibility,100.0)*specularScale;
    if(transparent) {
        float k=roughness2*.5;
        float geometry=.25/max((nv+nl)*(1.0-k)+2.0*k,1e-8);
        float fc=exp2((-5.55473*vh-6.98316)*vh);
        vec3 f0=grazing+vec3(.04,.04,.04);
        result.diffuse=(1.0-metallic)*albedo*((particleWrap>=0.0?particleAttenuation:nl)*diffuseScale/3.141592653589793);
        result.specular=(vec3(fc,fc,fc)+(1.0-fc)*f0)*distribution*geometry*specularScale;
    }
    return result;
}
vec4 particleTransform(uint row,vec4 p){return vec4(dot(PP(row),p),dot(PP(row+1u),p),dot(PP(row+2u),p),dot(PP(row+3u),p));}
float particleSunShadow(vec3 p){
    if(PP(18).x<=0.0)return 1.0;
    vec4 h=particleTransform(14u,vec4(p,1));if(h.w<=1e-8)return 1.0;
    vec3 q=h.xyz/h.w;if(max(abs(q.x),max(abs(q.y),abs(q.z)))>=1.0)return 1.0;
    vec2 uv=q.xy*vec2(.5,-.5)+vec2(.5,.5);float sum=0.0;
    for(int y=0;y<2;++y)for(int x=0;x<2;++x){
        vec2 f=uv/PP(18).w-.5,b=floor(f),w=mix(1.0-fract(f),fract(f),vec2(x,y));
        vec2 tap=(b+vec2(x,y)+.5)*PP(18).w;
        float depth=min(tap.x,tap.y)<0.0||max(tap.x,tap.y)>1.0?1.0:PL_SHADOW(PP(19).xy+tap*PP(19).zw);
        sum+=step(q.z*.5+.5-PP(18).y,depth)*w.x*w.y;
    }
    return mix(1.0,sum,PP(18).x);
}
float particleLocalShadow(int index,vec3 p) {
    if(index<0)return 1.0;uint at=200u+uint(index)*6u;vec4 params=PP(at+4u),tile=PP(at+5u);
    if(params.x<=0.0)return 1.0;
    vec4 h=PP(at)*p.x+PP(at+1u)*p.y+PP(at+2u)*p.z+PP(at+3u);
    if(h.w<=1e-8)return 1.0;vec3 q=h.xyz/h.w;
    if(max(abs(q.x),abs(q.y))>=1.0 || (params.w>=0.0 && abs(q.z)>=1.0))return 1.0;
    vec2 uv=q.xy*vec2(.5,-.5)+vec2(.5,.5),moments=vec2(0,0);float vis=0.0;
    float size=abs(params.w);vec2 t=uv/size-.5,b=floor(t),fraction=fract(t);
    if(params.w<0.0){tile.x=(tile.x-.5)*2.0;tile.z*=2.0;}
    for(int y=0;y<2;++y)for(int x=0;x<2;++x) {
        vec2 tap=clamp((b+vec2(x,y)+.5)*size,vec2(size*.5,size*.5),vec2(1.0-size*.5,1.0-size*.5));
        vec2 w=mix(1.0-fraction,fraction,vec2(x,y));
        if(params.w<0.0)moments+=PL_MOMENT(tile.xy+tap*tile.zw)*w.x*w.y;
        else vis+=step(q.z*.5+.5-params.y,PL_SHADOW(tile.xy+tap*tile.zw))*w.x*w.y;
    }
    if(params.w<0.0)return finiteLightMomentVisibility(moments,clamp(h.w*params.y*sqrt(1.0+dot(q.xy*params.z,q.xy*params.z)),0.0,1.0),params.x);
    return mix(1.0,vis,params.x);
}
PL_SHARED uint particleMask;
PL_SHARED uint particleOccluded;
bool particleHidden(uint base) {
    if(PP(25).x<.5)return false;
    vec2 lo=vec2(1e20,1e20),hi=-lo;float closest=1e20;
    for(uint c=0u;c<4u;++c) {
        vec3 p=PQ(base+c).xyz;vec4 h=particleTransform(6u,vec4(p,1));
        if(h.w<=1e-5)return false;
        vec2 screen=(h.xy/h.w*vec2(.5,-.5)+vec2(.5,.5))*PP(0).xy;
        lo=min(lo,screen);hi=max(hi,screen);
        closest=min(closest,dot(p-PP(1).xyz,PP(26).xyz));
    }
    if(closest<=0.0||closest>=PP(26).w)return false;
    ivec2 first=ivec2(clamp(floor(lo/16.0),vec2(0,0),ceil(PP(0).xy/16.0)-1.0));
    ivec2 last=ivec2(clamp(floor(hi/16.0),vec2(0,0),ceil(PP(0).xy/16.0)-1.0));
    // The shared producer stores the furthest opaque depth in every 16x16 tile.
    // Scan every overlapped tile: gaps and partially covered quads stay visible.
    PL_LOOP for(int y=first.y;y<=last.y;++y)PL_LOOP for(int x=first.x;x<=last.x;++x)
        if(closest<=PL_DEPTH(ivec2(x,y))+.01)return false;
    return true;
}
void particleCull(uint index) {
    uint base=index*6u;vec3 lo=PQ(base).xyz,hi=lo;
    for(uint c=1u;c<4u;++c){lo=min(lo,PQ(base+c).xyz);hi=max(hi,PQ(base+c).xyz);}
    uint mask=0u;
    for(uint i=0u;i<uint(PP(24).x);++i){
        uint at=32u+i*8u;vec4 l=PP(at);vec3 d=max(max(lo-l.xyz,l.xyz-hi),vec3(0,0,0));
        float radius=l.w+PP(at+3u).y*.5+PP(at+3u).x;
        if(dot(d,d)<=radius*radius)mask|=1u<<i;
    }
    particleMask=mask;
}
vec3 particleDirect(vec3 p,vec3 n,vec3 view,float wrap,float back,float footprintVariance) {
    const float pi=3.141592653589793;vec3 result=worldIndirectAmbient(p,n)/pi;
    vec3 l=finiteLightUnit(PP(2).xyz);
    float nl=dot(n,l),wrapped=clamp((nl+wrap)/pow(1.0+wrap,2.0),0.0,1.0);
    float a=mix(wrapped,nl>0.0?wrapped:wrapped-nl*back,clamp(-dot(view,l),0.0,1.0));
    FiniteLightResponse sun=finiteLightResponseExtended(n,view,l,vec3(0,0,0),0.0,0.0,1.0,
        vec3(1,1,1),0.0,false,vec3(-.04,-.04,-.04),true,-1.0,0.0);
    result+=PP(3).rgb*(vec3(a/pi,a/pi,a/pi)+sun.specular)*particleSunShadow(p);
    uint mask=particleMask;
    PL_LOOP for(uint i=0u;i<uint(PP(24).x);++i)if((mask&(1u<<i))!=0u){
        uint at=32u+i*8u;vec4 light=PP(at),energy=PP(at+1u),axis=PP(at+2u),shape=PP(at+3u),cone=PP(at+4u),face=PP(at+5u);
        vec3 delta=light.xyz-p;
        if(face.w>.5&&dot(-delta,face.xyz)<=0.0)continue;
        bool area=PP(at+6u).y>.5;vec3 direction=area?face.xyz:axis.xyz;
        float attenuation=nativeLightAttenuation(delta,light.w,axis,shape,cone,direction,area);
        if(face.w>.5) {
            vec3 halfAxis=axis.xyz*shape.y*.5,segment=halfAxis*2.0,start=delta-halfAxis;
            float along=clamp(-dot(start,segment)/max(dot(segment,segment),1e-12),0.0,1.0);
            float angular=max(0.0,-dot(finiteLightUnit(start+segment*along),face.xyz));
            attenuation=angular*finiteLightFalloff(finiteLightDistanceSquared(delta,halfAxis,true),
                1.0/light.w,true,false,0.0,energy.rgb);
        }
        // A lighting texel represents an area of a billboard, not a point.
        // Regularize its inverse-square peak before interpolation spreads a
        // source crossing over that area. Fixed-normal surface splashes keep
        // their existing response.
        if(footprintVariance>0.0 && (face.w>.5 || (uint(axis.w)&2u)!=0u)) {
            float distanceSquared=finiteLightDistanceSquared(delta,area?axis.xyz*shape.y*.5:vec3(0,0,0),area&&shape.y>0.0);
            attenuation*=max(distanceSquared,.0001)/(max(distanceSquared,.0001)+footprintVariance+shape.x*shape.x);
        }
        if(attenuation<=0.0)continue;
        if((uint(axis.w)&8u)!=0u)delta=-direction*max(length(delta),1e-6);
        FiniteLightResponse response=finiteLightResponseExtended(n,view,delta,area?axis.xyz*shape.y*.5:vec3(0,0,0),
            shape.x,shape.y,clamp(shape.z,.04,1.0),vec3(1,1,1),0.0,area,vec3(-.04,-.04,-.04),true,wrap,back);
        vec3 lobes=vec3(0,0,0);uint flags=uint(energy.w);
        if((flags&1u)!=0u)lobes+=response.diffuse;
        if((flags&2u)!=0u)lobes+=response.specular;
        result+=max(lobes,vec3(0,0,0))*energy.rgb*(attenuation*particleLocalShadow(int(PP(at+6u).x),p));
    }
    return max(result,vec3(0,0,0));
}
void particleLighting(uint index,uint lane) {
    if(index>=uint(PP(0).w))return;
    if(lane==0u){particleOccluded=particleHidden(index*6u)?1u:0u;if(particleOccluded==0u)particleCull(index);}PL_BARRIER;
    uint base=index*6u;vec4 tile=PQ(base+4u),settings=PQ(base+5u);uint size=uint(tile.z);
    vec3 center=(PQ(base).xyz+PQ(base+1u).xyz+PQ(base+2u).xyz+PQ(base+3u).xyz)*.25;
    vec3 face=finiteLightUnit(cross(PQ(base+1u).xyz-PQ(base).xyz,PQ(base+2u).xyz-PQ(base).xyz));
    vec3 view=finiteLightUnit(PP(1).xyz-center);if(dot(face,view)<0.0)face=-face;
    vec3 spacingU=(PQ(base+1u).xyz-PQ(base).xyz)/float(size-1u);
    vec3 spacingV=(PQ(base+3u).xyz-PQ(base).xyz)/float(size-1u);
    float footprintVariance=tile.w>.5?(dot(spacingU,spacingU)+dot(spacingV,spacingV))/12.0:0.0;
    PL_LOOP for(uint pixel=lane;pixel<size*size;pixel+=64u){
        vec2 uv=vec2(float(pixel%size),float(pixel/size))/float(size-1u);
        if(particleOccluded!=0u){PL_STORE(ivec2(tile.xy)+ivec2(int(pixel%size),int(pixel/size)),vec4(0,0,0,0));continue;}
        vec3 position=mix(mix(PQ(base).xyz,PQ(base+3u).xyz,uv.y),mix(PQ(base+1u).xyz,PQ(base+2u).xyz,uv.y),uv.x);
        vec4 clip=particleTransform(6u,vec4(position,1));
        if(abs(clip.w)>1e-8) {
            vec3 border=vec3(1.0-2.0/PP(0).x,1.0-2.0/PP(0).y,1.0);
            clip.xyz=clip.w*clamp(clip.xyz/clip.w,-border,border);
            vec4 world=particleTransform(10u,clip);position=world.xyz/max(abs(world.w),1e-8)*sign(world.w);
        }
        vec3 normal=face;
        if(tile.w>.5){
            normal=view;vec3 tangent=position-center;tangent-=dot(tangent,normal)*normal;tangent=finiteLightUnit(tangent);
            normal=finiteLightUnit(mix(normal,tangent,clamp(1.4142135623730951*length(uv-.5),0.0,1.0)));
        }
        vec3 radiance=PP(2).w>.5?particleDirect(position,normal,finiteLightUnit(PP(1).xyz-position),settings.y,settings.x,footprintVariance):vec3(0,0,0);
        PL_STORE(ivec2(tile.xy)+ivec2(int(pixel%size),int(pixel/size)),vec4(min(radiance/64.0,vec3(65504,65504,65504)),1));
    }
}
