// Finite local-source response. The private matched-input fixture supplies the
// captured adapters; this file contains only engine-owned geometric/material math.
// Normal/view must be unit vectors; roughness is [0.04,1]. Singular geometry
// has a bounded extension so authored fixtures cannot introduce NaNs.
float finiteLightRsqrt(float value) {
    value=max(value,1e-20);
    float estimate=uintBitsToFloat(1595932665u-(floatBitsToUint(value)>>1u));
    return estimate*0.7039522528648376*(2.389244556427002-estimate*estimate*value);
}
vec3 finiteLightUnit(vec3 value) { return value*inversesqrt(max(dot(value,value),1e-20)); }
float finiteLightPow5(float value) { float square=value*value; return square*square*value; }
float finiteLightDistanceSquared(vec3 delta,vec3 halfAxis,bool extended) {
    if (!extended) return dot(delta,delta);
    vec3 a=delta-halfAxis,b=delta+halfAxis;
    float inverseProduct=finiteLightRsqrt(dot(a,a)*dot(b,b));
    return max(0.0,(.5*dot(a,b)*inverseProduct+.5)/inverseProduct);
}
float finiteLightFalloff(float distanceSquared,float inverseRange,bool inverseSquare,
                         bool capped,float cap,vec3 intensity) {
    if (intensity.r+intensity.g+intensity.b<=0.0) return 0.0;
    float normalizedSquared=inverseRange*inverseRange*distanceSquared;
    float cutoff=1.0-clamp(normalizedSquared*normalizedSquared,0.0,1.0);
    float attenuation=inverseSquare ? cutoff*cutoff/max(distanceSquared,.0001)
        : 1.0-clamp(sqrt(distanceSquared)*inverseRange,0.0,1.0);
    if (capped) attenuation=min(attenuation,cap);
    float threshold=.01/(intensity.r+intensity.g+intensity.b);
    if (attenuation<threshold*2.0) attenuation=2.0*clamp(attenuation-threshold,0.0,1.0);
    return attenuation;
}
float finiteLightMomentVisibility(vec2 moments,float receiver,float opacity) {
    if (receiver<=moments.x) return 1.0;
    float variance=max(moments.y-moments.x*moments.x,.00001);
    float delta=receiver-moments.x;
    float probability=variance/(variance+delta*delta);
    float visibility=clamp((probability-.04)*1.0416667461395264,0.0,1.0);
    return 1.0+opacity*(visibility-1.0);
}
struct FiniteLightResponse { vec3 diffuse; vec3 specular; };
FiniteLightResponse finiteLightResponseImpl(vec3 normal,vec3 view,vec3 delta,vec3 halfAxis,
        float radius,float sourceLength,float roughness,vec3 albedo,float metallic,bool finiteSource,vec3 grazing) {
    FiniteLightResponse result;result.diffuse=vec3(0.0,0.0,0.0);result.specular=result.diffuse;
    if (dot(delta,delta)<1e-12) return result;
    vec3 light=delta*finiteLightRsqrt(dot(delta,delta));
    vec3 halfway=finiteLightUnit(light+view);
    float nl=clamp(dot(normal,light),0.0,1.0),lh=clamp(dot(light,halfway),0.0,1.0);
    float specularScale=1.0,diffuseScale=1.0;
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
        nl=clamp(nl,0.0,1.0);
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
    return result;
}
FiniteLightResponse finiteLightResponse(vec3 normal,vec3 view,vec3 delta,vec3 halfAxis,
        float radius,float sourceLength,float roughness,vec3 albedo,float metallic,bool finiteSource) {
    return finiteLightResponseImpl(normal,view,delta,halfAxis,radius,sourceLength,roughness,
        albedo,metallic,finiteSource,metallic*(albedo-vec3(.04,.04,.04)));
}
// Preserve authored engine F0 (including non-default dielectric reflectance).
FiniteLightResponse finiteLightMaterialResponse(vec3 normal,vec3 view,vec3 delta,vec3 halfAxis,
        float radius,float sourceLength,float roughness,vec3 albedo,float metallic,vec3 f0) {
    return finiteLightResponseImpl(normal,view,delta,halfAxis,radius,sourceLength,roughness,
        albedo,metallic,true,f0-vec3(.04,.04,.04));
}
// Approximate a one-sided rectangle with an inscribed finite line/sphere.
// Radiance stays in the authored units: normal intensity = emitting area * L.
FiniteLightResponse finiteLightRectangleResponse(vec3 delta,vec3 u,vec3 v,float range,
        vec3 radiance,vec3 normal,vec3 view,vec3 albedo,vec3 f0,float roughness,float metallic) {
    FiniteLightResponse result;result.diffuse=vec3(0.0,0.0,0.0);result.specular=result.diffuse;
    float hu=length(u),hv=length(v);
    if (range<=0.0 || min(hu,hv)<.001 || max(radiance.r,max(radiance.g,radiance.b))<=0.0) return result;
    vec3 outward=finiteLightUnit(cross(u,v));
    if (dot(outward,delta)>=-1e-6) return result;
    float radius=min(hu,hv),longExtent=max(hu,hv);
    vec3 halfAxis=(hu>=hv?u:v)*(1.0-radius/longExtent);
    float sourceLength=2.0*(longExtent-radius);
    float distanceSquared=finiteLightDistanceSquared(delta,halfAxis,true);
    if (distanceSquared>=range*range) return result;
    vec3 intensity=radiance*(4.0*hu*hv);
    vec3 segment=2.0*halfAxis,start=delta-halfAxis;
    float along=clamp(-dot(start,segment)/max(dot(segment,segment),1e-12),0.0,1.0);
    float angular=max(0.0,-dot(finiteLightUnit(start+segment*along),outward));
    float energy=angular*finiteLightFalloff(distanceSquared,1.0/range,true,false,0.0,intensity);
    if (energy<=0.0) return result;
    result=finiteLightMaterialResponse(normal,view,delta,halfAxis,radius,sourceLength,
        clamp(roughness,.04,1.0),albedo,clamp(metallic,0.0,1.0),clamp(f0,vec3(0.0,0.0,0.0),vec3(1.0,1.0,1.0)));
    result.diffuse=max(result.diffuse,vec3(0.0,0.0,0.0))*intensity*energy;
    result.specular=max(result.specular,vec3(0.0,0.0,0.0))*intensity*energy;
    return result;
}
