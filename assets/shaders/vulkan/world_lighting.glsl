#include "world_contact.glsl"
#include "world_screen_reflection.glsl"
#include "world_indirect.glsl"
#include "world_local_reflection.glsl"

#include "world_surface.glsl"
#include "world_clustered_consumer.glsl"

// Prepared ordinary screen/probe results only; no planar or compatibility
// ray marcher is required by specialized forward material consumers.
vec3 worldPreparedReflectionRadiance(vec3 fallback,vec3 position,vec3 normal,vec3 view,float roughness) {
    vec3 radiance=worldPreparedLocalReflectionRadiance(fallback,position,normal,view,roughness);
    vec3 resolved;
    if(worldPreparedScreenReflectionRadiance(radiance,position,roughness,resolved))return resolved;
    return radiance;
}

vec3 worldReflectionRadiance(vec3 fallback,vec3 position,vec3 normal,vec3 view,float roughness) {
    vec3 planar=worldPlanarCoordinates(position,normal,view,roughness);
    // A fully weighted planar source replaces both expensive fallback traces.
    // Partial coverage retains their original evaluation and blend order.
    if(planar.z>=1.0)return max(worldPlanarFiltered(planar.xy,roughness),vec3(0.0,0.0,0.0));
    vec3 radiance=worldLocalReflectionRadiance(fallback,position,normal,view,roughness);
    radiance=worldScreenReflectionRadiance(radiance,position,normal,view,roughness);
    if(planar.z<=0.0)return radiance;
    return mix(radiance,max(worldPlanarFiltered(planar.xy,roughness),vec3(0.0,0.0,0.0)),planar.z);
}

float environmentMaterialRoughness(float authored) {
    return clamp(authored, ENV_SUN.w > .5 ? .04 : .16, 1.0);
}
float environmentSpecularScale(float previewScale) {
    // Scene captures contain linear radiance. Preserve its units through the
    // BRDF; the neutral preview's artistic scale belongs to that profile.
    return ENV_SUN.w > .5 ? 1.0 : previewScale;
}

// LTC edge integral adapted from Heitz, Dupuy, Hill and Neubelt (SIGGRAPH 2016).
// Attribution and redistribution terms: third_party/ltc/LICENSE.
vec2 environmentReflectionDfg(float normalDotView, float roughness) {
    // The existing GGX LTC table stores hemispherical magnitude (A+B) and
    // Fresnel moment B, also suitable for split-sum scene reflections.
    vec2 uv=(vec2(clamp(roughness,.04,1.0),sqrt(1.0-clamp(normalDotView,0.0,1.0)))*63.0+.5)/64.0;
    vec2 moments=ENV_RECT_LUT(vec2(.5+uv.x*.5,uv.y)).xy;
    return vec2(moments.x-moments.y,moments.y);
}
// Legacy backends retain neutral response until their parameter path is enrolled.
#ifndef ENV_REFLECTION_RESPONSE
#define ENV_REFLECTION_RESPONSE vec4(1.0,1.0,0.0,0.0)
#define ENV_REFLECTION_RESPONSE_PARAMS vec4(0.0,0.0,0.0,0.0)
#endif
vec3 environmentReflectionResponse(vec3 normal, vec3 view, vec3 f0, float roughness,
                                   vec3 albedo, float visibility, vec3 neutral) {
    if (ENV_SUN.w < .5 || ENV_REFLECTION_RESPONSE_PARAMS.x <= 0.0) return neutral;
    vec2 dfg=environmentReflectionDfg(abs(dot(normal,view)),roughness);
    float weight=clamp(ENV_REFLECTION_RESPONSE_PARAMS.x,0.0,1.0);
    float q=weight*clamp(visibility,0.0,1.0);
    vec4 controls=ENV_REFLECTION_RESPONSE;
    float a=1.0+(controls.x-1.0)*q;
    float b=1.0+(controls.y-1.0+controls.w)*q;
    float f90=mix(1.0,clamp(50.0*(albedo.r+albedo.g+albedo.b),0.0,1.0),
                  weight*ENV_REFLECTION_RESPONSE_PARAMS.y);
    float grazing=b*dfg.y*f90+controls.z*q;
    return clamp(a*dfg.x*f0+vec3(grazing,grazing,grazing),0.0,1.0);
}
// Outdoor reflection sources share one response, after radiance blending.
// Inspection lighting retains its established neutral-environment response.
void environmentIndirectScattering(vec3 normal, vec3 view, vec3 f0, float roughness,
                                    inout vec3 singleScattering, inout vec3 multiScattering) {
    if (ENV_SUN.w < .5) return;
    // World surfaces are two-sided; use the visible hemisphere just as the
    // planar receiver and local direct BRDF do, including reversed floor meshes.
    vec2 dfg=environmentReflectionDfg(abs(dot(normal,view)),roughness);
    singleScattering=f0*dfg.x+dfg.y;
    float missingEnergy=max(1.0-dfg.x-dfg.y,0.0);
    vec3 averageFresnel=f0+(vec3(1.0,1.0,1.0)-f0)*.047619;
    multiScattering=singleScattering*averageFresnel*missingEnergy/
        max(vec3(1.0,1.0,1.0)-missingEnergy*averageFresnel,vec3(.00001,.00001,.00001));
}
vec3 environmentDiffuseOcclusion(float visibility, vec3 albedo) {
    float ao=clamp(visibility,0.0,1.0);
    if (ENV_SUN.w < .5) return vec3(ao,ao,ao);
    // Jimenez et al., Practical Realtime Strategies for Accurate Indirect
    // Occlusion (2016), Eq. 10. Approximate near-field energy, not a new bake.
    vec3 a=2.0404*albedo-vec3(.3324,.3324,.3324);
    vec3 b=vec3(.6417,.6417,.6417)-4.7951*albedo;
    vec3 c=2.7552*albedo+vec3(.6903,.6903,.6903);
    return clamp(max(vec3(ao,ao,ao),((a*ao+b)*ao+c)*ao),0.0,1.0);
}
#include "world_finite_light.glsl"
#include "world_foliage.glsl"
#ifndef PHLOSION_EARLY_LOCAL_SHADOW
#define PHLOSION_EARLY_LOCAL_SHADOW 1
#endif
float environmentLocalShadowVisibility(int index, vec3 position, vec3 normal);
float environmentLocalVolumeVisibility(int index, vec3 position);
vec3 rectUnit(vec3 v);

// Analytic optical depth in the existing exponential height medium. Split at
// its base so a ray crossing below ground does not dilute the upper-air density.
float localHazeOpticalDepth(vec3 a, vec3 b) {
    float distanceMetres=length(b-a);
    float low=min(a.y,b.y)-ENV_HEIGHT.x, high=max(a.y,b.y)-ENV_HEIGHT.x;
    float falloff=ENV_HEIGHT.y;
    if (falloff<0.00001 || high<=0.0) return ENV_HAZE.w*distanceMetres;
    float span=high-low;
    if (span<0.001) return ENV_HAZE.w*distanceMetres*exp(-max(0.0,(low+high)*.5)*falloff);
    float integral=max(0.0,-low)+(exp(-max(0.0,low)*falloff)-exp(-high*falloff))/falloff;
    return ENV_HAZE.w*distanceMetres*integral/span;
}
float localHazeCorner(float x,float y,float z) {
    return atan(x*y,z*sqrt(x*x+y*y+z*z));
}
// Solid angle of the complete luminous rectangle, not a point at its centre.
float localHazeSolidAngle(vec3 delta,vec3 u,vec3 v) {
    float hu=length(u),hv=length(v);
    float x=dot(delta,u)/hu,y=dot(delta,v)/hv;
    float z=max(abs(dot(delta,cross(u,v)))/(hu*hv),0.00001);
    return max(0.0,localHazeCorner(x+hu,y+hv,z)-localHazeCorner(x-hu,y+hv,z)
        -localHazeCorner(x+hu,y-hv,z)+localHazeCorner(x-hu,y-hv,z));
}
vec3 environmentLocalHaze(vec3 position,vec3 camera) {
    vec3 result=vec3(0.0,0.0,0.0);
    if (ENV_SUN.w<.5 || ENV_HAZE.w<=0.0 || ENV_WEATHER_MATERIAL.z<=0.0) return result;
    vec3 segment=position-camera;
    float distanceMetres=length(segment);
    if (distanceMetres<.0001) return result;
    vec3 ray=segment/distanceMetres;
    // In a planar capture integrate only the real, above-water reflected leg.
    // The receiving water draw handles the leg from the water to the viewer.
    float rayStart=0.0;
    if (worldReflectionCapture()) {
        if (ray.y<=.00001) return result;
        rayStart=max(0.0,(PLANAR(4).x-camera.y)/ray.y);
    }
    vec3 origin=camera+ray*rayStart;
    for (int index=0;index<16;++index) {
        vec4 source=ENV_RECT(index*4);
        vec3 emitted=ENV_RECT(index*4+3).rgb;
        if(((uint(ENV_RECT(index*4+3).w)>>8u)&16u)==0u || ((uint(ENV_RECT(index*4+3).w)&255u)&(uint(ENV_POINT(1).w)>>16u))==0u)continue;
        if (source.w<=0.0 || max(emitted.r,max(emitted.g,emitted.b))<=0.0) continue;
        vec3 toSource=source.xyz-camera;
        float along=dot(toSource,ray);
        float radius2=source.w*source.w;
        float discriminant=radius2-dot(toSource,toSource)+along*along;
        if (discriminant<=0.0) continue;
        float reach=sqrt(discriminant);
        float begin=max(rayStart,along-reach),end=min(distanceMetres,along+reach);
        vec3 u=ENV_RECT(index*4+1).xyz,v=ENV_RECT(index*4+2).xyz;
        vec3 normal=rectUnit(cross(u,v));
        float side=dot(camera-source.xyz,normal),slope=dot(ray,normal);
        if (abs(slope)<.00001) { if (side<=0.0) continue; }
        else if (slope>0.0) begin=max(begin,-side/slope);
        else end=min(end,-side/slope);
        if (end<=begin) continue;
        // Equiangular spacing resolves the bright near-source interval without
        // metre-wide steps near a strip. A finite footprint bounds grazing rays.
        float perpendicular=sqrt(max(dot(toSource,toSource)-along*along,.04));
        float firstAngle=atan((begin-along)/perpendicular,1.0);
        float angleStep=(atan((end-along)/perpendicular,1.0)-firstAngle)/16.0;
        for (int sampleIndex=0;sampleIndex<16;++sampleIndex) {
            float angle=firstAngle+(float(sampleIndex)+.5)*angleStep;
            float tangent=tan(angle);
            float stepLength=perpendicular*(1.0+tangent*tangent)*angleStep;
            vec3 p=camera+ray*(along+perpendicular*tangent);
            vec3 delta=source.xyz-p;
            float d2=dot(delta,delta);
            float cutoff=max(0.0,1.0-d2*d2/(radius2*radius2));
            float visibility=environmentLocalVolumeVisibility(index,p);
            if (visibility<=0.0) continue;
            float density=ENV_HAZE.w*exp(-max(p.y-ENV_HEIGHT.x,0.0)*ENV_HEIGHT.y);
            float g=ENV_WEATHER_MATERIAL.w;
            float cosine=dot(ray,delta/sqrt(max(d2,.00001)));
            float denominator=max(.0001,1.0+g*g-2.0*g*cosine);
            float phase=(1.0-g*g)/(12.566370614*denominator*sqrt(denominator));
            float transmission=exp(-localHazeOpticalDepth(origin,p)-localHazeOpticalDepth(p,source.xyz));
            result+=emitted*(localHazeSolidAngle(delta,u,v)*cutoff*cutoff*visibility*
                density*ENV_WEATHER_MATERIAL.z*phase*transmission*stepLength);
        }
    }
    return result;
}

#ifdef HAZE_DIMENSIONS
#ifndef HAZE_PRECISE
#define HAZE_PRECISE
#endif
vec3 localHazeEndpoint(vec2 uv,vec3 camera) {
    vec2 pixel=clamp(floor(HAZE_COORD(uv)*HAZE_DIMENSIONS.xy),vec2(0.0,0.0),HAZE_DIMENSIONS.xy-1.0);
    float depth=HAZE_DEPTH(pixel);
    vec4 homogeneous=HAZE_UNPROJECT(vec4(uv*2.0-1.0,depth*2.0-1.0,1.0));
    vec3 position=homogeneous.xyz/max(abs(homogeneous.w),0.000001)*sign(homogeneous.w);
    return depth>=0.999999 ? camera+rectUnit(position-camera)*60000.0 : position;
}
vec4 worldFogComposition(vec2 uv,vec3 camera) {
    vec4 fog=worldFogAt(localHazeEndpoint(uv,camera));
    return vec4(fog.rgb,1.0-fog.a);
}
vec4 worldLocalHazePass(vec2 uv,vec3 camera,float mode) {
    vec3 endpoint=localHazeEndpoint(uv,camera);
    float distanceMetres=min(length(endpoint-camera),60000.0);
    if (mode==247.0) return vec4(environmentLocalHaze(endpoint,camera),distanceMetres);
    vec2 grid=HAZE_COORD(uv)*HAZE_DIMENSIONS.zw-.5;
    vec2 cell=floor(grid),fraction=grid-cell;
    vec3 color=vec3(0.0,0.0,0.0);HAZE_PRECISE float weightSum=0.0;
    for(int tap=0;tap<4;++tap) {
        vec2 offset=vec2(float(tap&1),float((tap>>1)&1));
        vec2 pixel=clamp(cell+offset,vec2(0.0,0.0),HAZE_DIMENSIONS.zw-1.0);
        vec4 sampleValue=HAZE_FETCH(pixel);
        vec2 bilinear=mix(1.0-fraction,fraction,offset);
        float tolerance=max(.1,distanceMetres*.03);
        float weight=bilinear.x*bilinear.y*(1.0-smoothstep(tolerance,tolerance*2.0,abs(sampleValue.a-distanceMetres)));
        color+=sampleValue.rgb*weight;weightSum+=weight;
    }
    // The two specializations share an exact, complementary depth predicate.
    // Ordinary reconstruction carries no full ray integration; thin silhouettes
    // retain the analytic fallback in their separate draw.
    if(mode==248.0) {
        if(weightSum<=.01) discard;
        return vec4(color/weightSum,1.0);
    }
    if(weightSum>.01) discard;
    return vec4(environmentLocalHaze(endpoint,camera),1.0);
}
#endif

// Static one-bounce irradiance, relit from live sources. No direct emitter term.
vec3 environmentDiffuseProbeIrradiance(vec3 position, vec3 normal) {
    if (ENV_SUN.w < 0.5 || ENV_BOUNCE(0).w < 0.5 || ENV_BOUNCE(0).w > 1.5) return vec3(0.0,0.0,0.0);
    vec3 origin=ENV_BOUNCE(0).xyz, spacing=ENV_BOUNCE(1).xyz, counts=ENV_BOUNCE(2).xyz;
    vec3 coord=(position-origin)/spacing;
    vec3 bounded=clamp(coord,vec3(0.0,0.0,0.0),counts-1.0);
    vec3 outside=abs((coord-bounded)*spacing);
    float fade=1.0-clamp(max(outside.x,max(outside.y,outside.z))/ENV_BOUNCE(1).w,0.0,1.0);
    if (fade<=0.0) return vec3(0.0,0.0,0.0);
    vec3 cell=min(floor(bounded),counts-2.0), fraction=bounded-cell;
    vec3 irradiance=vec3(0.0,0.0,0.0);float weightSum=0.0;
#ifdef PHLOSION_DIFFUSE_PROBE_LOOP
    PHLOSION_DIFFUSE_PROBE_LOOP
#endif
    for (int index=0;index<8;++index) {
        vec3 bits=vec3(float(index&1),float((index>>1)&1),float((index>>2)&1));
        vec3 probe=cell+bits;
        int offset=3+int((probe.z*counts.y+probe.y)*counts.x+probe.x)*15;
        vec3 delta=position-(origin+probe*spacing);
        float distance=length(delta);
        vec3 direction=distance>0.0001?delta/distance:normal;
        vec3 a=abs(direction);
        int axis=a.x>a.y?(a.x>a.z?0:2):(a.y>a.z?1:2);
        int sector=axis*2+(direction[axis]<0.0?1:0);
        vec2 moments=ENV_BOUNCE(offset+9+sector).xy;
        float excess=max(0.0,distance-moments.x), variance=max(0.0025,moments.y-moments.x*moments.x);
        float visibility=variance/(variance+excess*excess);
        vec3 w=mix(1.0-fraction,fraction,bits);
        float facing=max(0.05,0.5-0.5*dot(normal,direction));
        float weight=w.x*w.y*w.z*ENV_BOUNCE(offset).w*facing*facing*visibility*visibility*visibility;
        vec3 n=normal;
        vec3 sh=ENV_BOUNCE(offset).rgb*0.282094792+
            ENV_BOUNCE(offset+1).rgb*(0.488602512*n.y)+
            ENV_BOUNCE(offset+2).rgb*(0.488602512*n.z)+
            ENV_BOUNCE(offset+3).rgb*(0.488602512*n.x)+
            ENV_BOUNCE(offset+4).rgb*(1.092548431*n.x*n.y)+
            ENV_BOUNCE(offset+5).rgb*(1.092548431*n.y*n.z)+
            ENV_BOUNCE(offset+6).rgb*(0.315391565*(3.0*n.z*n.z-1.0))+
            ENV_BOUNCE(offset+7).rgb*(1.092548431*n.x*n.z)+
            ENV_BOUNCE(offset+8).rgb*(0.546274215*(n.x*n.x-n.y*n.y));
        irradiance+=max(sh,vec3(0.0,0.0,0.0))*weight;weightSum+=weight;
    }
    return irradiance*(fade/max(weightSum,0.05));
}

#include "world_local_direct.glsl"

// Layered/toon source bodies retain their authored specular model. Add local
// diffuse energy without introducing a generic GGX outer coat or changing emission.
vec3 environmentLocalDiffuse(vec3 position, vec3 normal, vec3 view, vec3 albedo) {
    vec3 result = vec3(0.0, 0.0, 0.0);
    if (ENV_SUN.w<.5) return result;
    if (dot(normal, view) < 0.0) normal = -normal;
    for (int index = 0; index < 4; ++index) {
        vec3 light, radiance;
        if ((localLightFlags(ENV_POINT(index*2+1).w)&1u)!=0u && environmentPointSample(index, position, light, radiance))
            result += albedo * radiance * environmentLocalShadowVisibility(16+index,position,normal) * max(dot(normal, light), 0.0) / 3.14159265;
    }
    vec3 tangent=rectTangent(normal,view);
    for (int index=0;index<16;++index) {
        if((localLightFlags(ENV_RECT(index*4+3).w)&1u)==0u)continue;
        vec3 delta,u,v,radiance;
        if (ENV_RECT(index*4+1).w>.5) {
            float visibility=environmentLocalShadowVisibility(index,position,normal);
            if (visibility>0.0) {
                FiniteLightResponse response=environmentFiniteRectangle(index,position,normal,view,
                    albedo,vec3(.04,.04,.04),1.0,0.0);
                result+=response.diffuse*visibility;
            }
            continue;
        }
#if PHLOSION_EARLY_LOCAL_SHADOW
        if (!environmentRectSample(index,position,delta,u,v,radiance)) continue;
        float visibility=environmentLocalShadowVisibility(index,position,normal);
        if (visibility<=0.0) continue;
        result+=albedo*radiance*rectIntegral(delta,u,v,normal,tangent,vec4(1.0,0.0,0.0,1.0))*visibility;
#else
        if (environmentRectSample(index,position,delta,u,v,radiance))
            result+=albedo*radiance*rectIntegral(delta,u,v,normal,tangent,vec4(1.0,0.0,0.0,1.0))*environmentLocalShadowVisibility(index,position,normal);
#endif
    }
    return result+albedo*environmentDiffuseProbeIrradiance(position,normal)/3.14159265;
}

vec3 environmentLocalPbr(vec3 position, vec3 normal, vec3 view,
                         vec3 albedo, vec3 f0, float roughness, float metallic, float occlusion,vec3 diffuseFactor,float specularFactor) {
    if (ENV_SUN.w<.5) return vec3(0.0,0.0,0.0);
    if (dot(normal,view)<0.0) normal=-normal;
    if (dot(normal,view)<=0.0) return vec3(0.0,0.0,0.0);
    vec3 direct=environmentPreparedLocalDirectPbrWeighted(position,normal,view,albedo,f0,roughness,metallic,diffuseFactor,specularFactor);
    return direct+
        (vec3(1.0,1.0,1.0)-f0)*(1.0-metallic)*albedo*
        environmentDiffuseProbeIrradiance(position,normal)*environmentDiffuseOcclusion(occlusion,albedo)/3.14159265;
}
vec3 environmentLocalPbr(vec3 position,vec3 normal,vec3 view,
        vec3 albedo,vec3 f0,float roughness,float metallic,float occlusion) {
    return environmentLocalPbr(position,normal,view,albedo,f0,roughness,metallic,occlusion,vec3(1.0,1.0,1.0),1.0);
}
vec3 environmentLocalPbr(vec3 position, vec3 normal, vec3 view,
                         vec3 albedo, vec3 f0, float roughness, float metallic) {
    return environmentLocalPbr(position,normal,view,albedo,f0,roughness,metallic,1.0);
}


#ifndef CLUSTER_FORWARD_FALLBACK
#define CLUSTER_FORWARD_FALLBACK true
#endif
// Direct-only reuse also serves custom primary shading with prepared GI.
vec3 environmentStandardLocalDirectPbr(vec3 position,vec3 normal,vec3 view,
        vec3 albedo,vec3 f0,float roughness,float metallic,vec3 diffuseFactor,float specularFactor) {
    if (ENV_SUN.w<.5) return vec3(0.0,0.0,0.0);
    if (dot(normal,view)<0.0) normal=-normal;
    if (dot(normal,view)<=0.0) return vec3(0.0,0.0,0.0);
    vec3 direct;
    if (!worldClusteredDirectWeighted(position,diffuseFactor,specularFactor,direct) && CLUSTER_FORWARD_FALLBACK)
        direct=environmentPreparedLocalDirectPbrWeighted(position,normal,view,albedo,f0,roughness,metallic,diffuseFactor,specularFactor);
    return direct;
}
vec3 environmentStandardLocalPbr(vec3 position,vec3 normal,vec3 view,
        vec3 albedo,vec3 f0,float roughness,float metallic,float occlusion,vec3 diffuseFactor,float specularFactor) {
    if (ENV_SUN.w<.5) return vec3(0.0,0.0,0.0);
    if (dot(normal,view)<0.0) normal=-normal;
    if (dot(normal,view)<=0.0) return vec3(0.0,0.0,0.0);
    return environmentStandardLocalDirectPbr(position,normal,view,albedo,f0,roughness,metallic,diffuseFactor,specularFactor)+
        (vec3(1.0,1.0,1.0)-f0)*(1.0-metallic)*albedo*
        environmentDiffuseProbeIrradiance(position,normal)*environmentDiffuseOcclusion(occlusion,albedo)/3.14159265;
}
vec3 environmentStandardLocalPbr(vec3 position,vec3 normal,vec3 view,
        vec3 albedo,vec3 f0,float roughness,float metallic,float occlusion) {
    return environmentStandardLocalPbr(position,normal,view,albedo,f0,roughness,metallic,occlusion,vec3(1.0,1.0,1.0),1.0);
}

// Evaluated leaf response shared by primary composition and other forward views.
vec3 worldLightFoliageSurface(vec3 albedo,vec3 n,vec3 view,float transmission,
        float roughness,vec3 position,vec3 camera,float reflectionWeight) {
    FiniteLightResponse direct=worldFoliageDirectional(n,view,environmentLightDirection(),
        environmentMoonIsPrimary()?ENV_CELESTIAL.y:ENV_CELESTIAL.x,albedo,roughness,clamp(transmission,0.0,1.0));
    vec4 contact=worldContactVisibility(position,n,camera);
    float visibility=worldContactDiffuseVisibility(contact,n,view);
    vec4 contactResponse=worldContactResponse(visibility,worldContactSpecularVisibility(contact,n,view,roughness),transmission);
    // Legacy diagnostic views retain their old ambient treatment.
    float diffuseVisibility=worldContactPrepared()?contactResponse.x:1.0;
    vec3 directContact=environmentDiffuseOcclusion(contactResponse.z,albedo);
    vec3 ambient=worldIndirectEnabled()?worldIndirectAmbient(position,n):ENV_AMBIENT.rgb*(0.65+0.35*max(n.y,0.0));
    vec3 color=albedo*ambient*environmentDiffuseOcclusion(diffuseVisibility,albedo);
    color+=(direct.diffuse*directContact+direct.specular*contactResponse.w*environmentGlobalSpecularTint())*environmentLightRadiance();
    if(CLUSTER_FORWARD_FALLBACK)
        color+=environmentStandardLocalPbr(position,n,view,albedo,vec3(0.04,0.04,0.04),roughness,0.0,diffuseVisibility,directContact,contactResponse.w);
    else
        color+=environmentStandardLocalDirectPbr(position,n,view,albedo,vec3(0.04,0.04,0.04),roughness,0.0,directContact,contactResponse.w);
    if(ENV_SUN.w>0.5) {
        // Reflect the prepared screen/probe radiance with the evaluated leaf
        // normal and roughness. Keep leaf transmission out of the grazing lobe.
        vec4 coefficients=mix(vec4(1.0,1.0,0.0,0.0),ENV_REFLECTION_RESPONSE,
            clamp(reflectionWeight*visibility,0.0,1.0));
        vec2 dfg=environmentReflectionDfg(abs(dot(n,view)),roughness);
        float grazing=clamp(50.0*(albedo.r+albedo.g+albedo.b),0.0,1.0);
        grazing*=clamp(1.0-clamp(transmission,0.0,1.0)*(10.0-coefficients.y),0.0,1.0);
        float responseScale=clamp(0.04*dfg.x*coefficients.x+
            grazing*dfg.y*(coefficients.y+coefficients.w)+coefficients.z,0.0,1.0);
        vec3 response=vec3(responseScale,responseScale,responseScale);
        vec3 reflected=worldPreparedReflectionRadiance(ENV_AMBIENT.rgb*0.5,position,n,view,roughness);
        color+=reflected*response*contactResponse.y;
    }
    return color;
}
