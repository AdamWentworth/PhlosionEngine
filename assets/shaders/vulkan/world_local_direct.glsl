#ifndef LOCAL_LIGHT_STORAGE
#define LOCAL_LIGHT_STORAGE
#endif
LOCAL_LIGHT_STORAGE uint localLightReceiverOverride=256u;
LOCAL_LIGHT_STORAGE uint localLightConsumer=4u;
uint localLightReceiver() {
    return localLightReceiverOverride<256u?localLightReceiverOverride:(uint(ENV_POINT(1).w)>>16u);
}
uint localLightFlags(float packed){return (uint(packed)>>8u)&255u;}
bool localLightAllows(float packed) {
    uint bits=uint(packed);
    return ((bits&255u)&localLightReceiver())!=0u && (localLightFlags(packed)&localLightConsumer)!=0u;
}
vec2 localLightLobes(float packed) {
    uint flags=localLightFlags(packed);
    return vec2((flags&1u)!=0u?1.0:0.0,(flags&2u)!=0u?1.0:0.0);
}

#ifndef PHLOSION_LOCAL_LIGHT_LOOP
#define PHLOSION_LOCAL_LIGHT_LOOP
#endif

vec3 rectUnit(vec3 v) { return v / sqrt(max(dot(v,v), 1e-20)); }
float rectEdge(vec3 a, vec3 b) {
    float x = clamp(dot(a,b), -1.0, 1.0), y = abs(x);
    float v = (0.8543985 + (0.4965155 + 0.0145206*y)*y) /
              (3.4175940 + (4.1616724 + y)*y);
    return cross(a,b).z * (x > 0.0 ? v : 0.5 / sqrt(max(1.0-x*x,1e-7))-v);
}
vec3 rectTransform(vec3 q, vec3 n, vec3 t, vec3 b, vec4 transform) {
    vec3 local=vec3(dot(q,t),dot(q,b),dot(q,n));
    return vec3(transform.x*local.x+transform.z*local.z,local.y,
                transform.y*local.x+transform.w*local.z);
}
float rectClippedEdge(vec3 a, vec3 b, inout vec3 enter, inout vec3 leave) {
    if (a.z<=0.0 && b.z<=0.0) return 0.0;
    if ((a.z>0.0)!=(b.z>0.0)) {
        vec3 hit=rectUnit(a+(b-a)*(a.z/(a.z-b.z)));
        if(a.z>0.0) {leave=hit;return rectEdge(rectUnit(a),hit);}
        enter=hit;return rectEdge(hit,rectUnit(b));
    }
    return rectEdge(rectUnit(a),rectUnit(b));
}
// Exact horizon clipping without dynamically indexed local arrays. Most world
// receivers see the entire emitter, so their four-edge path is straight-line.
float rectIntegral(vec3 delta, vec3 u, vec3 v, vec3 n, vec3 t, vec4 transform) {
    vec3 b=cross(n,t);
    vec3 p0=rectTransform(delta-u-v,n,t,b,transform);
    vec3 p1=rectTransform(delta+u-v,n,t,b,transform);
    vec3 p2=rectTransform(delta+u+v,n,t,b,transform);
    vec3 p3=rectTransform(delta-u+v,n,t,b,transform);
    if(min(min(p0.z,p1.z),min(p2.z,p3.z))>0.0) {
        p0=rectUnit(p0);p1=rectUnit(p1);p2=rectUnit(p2);p3=rectUnit(p3);
        return abs(rectEdge(p0,p1)+rectEdge(p1,p2)+rectEdge(p2,p3)+rectEdge(p3,p0));
    }
    if(max(max(p0.z,p1.z),max(p2.z,p3.z))<=0.0) return 0.0;
    vec3 enter=vec3(0.0,0.0,0.0),leave=vec3(0.0,0.0,0.0);
    float sum=rectClippedEdge(p0,p1,enter,leave);
    sum+=rectClippedEdge(p1,p2,enter,leave);
    sum+=rectClippedEdge(p2,p3,enter,leave);
    sum+=rectClippedEdge(p3,p0,enter,leave);
    return abs(sum+rectEdge(leave,enter));
}
bool environmentRectSample(int index, vec3 position, out vec3 delta,
                           out vec3 u, out vec3 v, out vec3 radiance) {
    vec4 source=ENV_RECT(index*4);
    u=ENV_RECT(index*4+1).xyz; v=ENV_RECT(index*4+2).xyz;
    radiance=ENV_RECT(index*4+3).rgb; delta=source.xyz-position;
    if (!localLightAllows(ENV_RECT(index*4+3).w) || ENV_SUN.w<0.5 || source.w<=0.0 || max(radiance.r,max(radiance.g,radiance.b))<=0.0)
        return false;
    if (dot(cross(u,v),delta)>=-1e-6) return false;
    float d2=dot(delta,delta), r2=source.w*source.w;
    if (d2>=r2) return false;
    float cutoff=max(1.0-d2*d2/(r2*r2),0.0);
    radiance*=cutoff*cutoff;
    return true;
}
vec3 rectTangent(vec3 normal, vec3 view) {
    vec3 tangent=view-normal*dot(normal,view);
    if (dot(tangent,tangent)<1e-6)
        tangent=cross(abs(normal.y)<0.9?vec3(0.0,1.0,0.0):vec3(1.0,0.0,0.0),normal);
    return rectUnit(tangent);
}
FiniteLightResponse environmentFiniteRectangle(int index,vec3 position,vec3 normal,vec3 view,
        vec3 albedo,vec3 f0,float roughness,float metallic) {
    if(!localLightAllows(ENV_RECT(index*4+3).w)) {
        FiniteLightResponse zero;zero.diffuse=vec3(0.0,0.0,0.0);zero.specular=zero.diffuse;return zero;
    }
    vec4 source=ENV_RECT(index*4);
    FiniteLightResponse response;
    if(ENV_RECT(index*4+1).w>1.5) {
        vec3 direction=finiteLightUnit(cross(ENV_RECT(index*4+1).xyz,ENV_RECT(index*4+2).xyz));
        response=nativeLightResponse(source.xyz-position,source.w,ENV_RECT(index*4+3).rgb,
            ENV_RECT(64+index*3),ENV_RECT(65+index*3),ENV_RECT(66+index*3),direction,true,
            normal,view,albedo,f0,roughness,metallic,localLightConsumer==128u);
    }
    else response=finiteLightRectangleResponse(source.xyz-position,ENV_RECT(index*4+1).xyz,
        ENV_RECT(index*4+2).xyz,source.w,ENV_RECT(index*4+3).rgb,normal,view,albedo,f0,roughness,metallic);
    vec2 lobes=localLightLobes(ENV_RECT(index*4+3).w);
    if(!localLightAllows(ENV_RECT(index*4+3).w))lobes=vec2(0.0,0.0);
    response.diffuse*=lobes.x;response.specular*=lobes.y;return response;
}

// Optional world-space lights, shared by ordinary and composed materials.
bool environmentPointSample(int index, vec3 position, out vec3 light, out vec3 radiance) {
    light = vec3(0.0, 0.0, 0.0);
    radiance = vec3(0.0, 0.0, 0.0);
    vec4 source = ENV_POINT(index * 2);
    vec3 intensity = ENV_POINT(index * 2 + 1).rgb;
    if (!localLightAllows(ENV_POINT(index*2+1).w) || ENV_SUN.w < 0.5 || source.w <= 0.0 ||
        max(intensity.r, max(intensity.g, intensity.b)) <= 0.0) return false;
    vec3 delta = source.xyz - position;
    float distanceSquared = dot(delta, delta);
    float rangeSquared = source.w * source.w;
    if (distanceSquared >= rangeSquared) return false;
    light = delta / sqrt(max(distanceSquared, 1e-8));
    float cutoff = max(1.0 - distanceSquared * distanceSquared / (rangeSquared * rangeSquared), 0.0);
    radiance = intensity * cutoff * cutoff / max(distanceSquared, 0.25);
    vec4 axis=ENV_POINT(8+index*3);
    if((uint(axis.w)&1u)!=0u) {
        radiance=intensity*nativeLightAttenuation(delta,source.w,axis,ENV_POINT(9+index*3),
            ENV_POINT(10+index*3),axis.xyz,false);
        if((uint(axis.w)&8u)!=0u)light=-axis.xyz;
    }
    return true;
}


// Direct-only per-source evaluators. Shared by forward materials and the future
// C7 classified consumer; shadow visibility applies to both lobes exactly once.
// Probe/bounce transport is outside this interface. The caller enables the
// environment and supplies unit, view-facing normals with positive N dot V.
FiniteLightResponse emptyLocalLightResponse() {
    FiniteLightResponse result;
    result.diffuse=vec3(0.0,0.0,0.0);result.specular=result.diffuse;
    return result;
}
// Unlit material lobes let the forward path combine before multiplying radiance.
FiniteLightResponse environmentPointBrdf(vec3 normal,vec3 view,vec3 light,float nv,float nl,
        vec3 albedo,vec3 f0,float roughness,float metallic) {
    FiniteLightResponse result;
    vec3 halfway = view + light;
    halfway /= sqrt(max(dot(halfway, halfway), 1e-8));
    float nh = max(dot(normal, halfway), 0.0);
    float vh = clamp(dot(view, halfway), 0.0, 1.0);
    float alpha = roughness * roughness;
    float alphaSquared = alpha * alpha;
    float denominator = nh * nh * (alphaSquared - 1.0) + 1.0;
    float distribution = alphaSquared / max(3.14159265 * denominator * denominator, 1e-5);
    float k = (roughness + 1.0) * (roughness + 1.0) * 0.125;
    float geometry = nv / max(nv * (1.0-k) + k, 1e-5) *
                     nl / max(nl * (1.0-k) + k, 1e-5);
    vec3 fresnel = f0 + (vec3(1.0, 1.0, 1.0)-f0) * pow(1.0-vh, 5.0);
    vec3 specular = distribution * geometry * fresnel / max(4.0*nv*nl, 1e-4);
    vec3 diffuse = (vec3(1.0, 1.0, 1.0)-fresnel) * (1.0-metallic) * albedo / 3.14159265;
    result.diffuse = diffuse;
    result.specular = specular;
    return result;
}
FiniteLightResponse environmentPointPbr(int index,vec3 position,vec3 normal,vec3 view,
        vec3 albedo,vec3 f0,float roughness,float metallic) {
    if((uint(ENV_POINT(8+index*3).w)&1u)!=0u) {
        if(ENV_SUN.w<.5||!localLightAllows(ENV_POINT(index*2+1).w))return emptyLocalLightResponse();
        vec4 axis=ENV_POINT(8+index*3);
        FiniteLightResponse native=nativeLightResponse(ENV_POINT(index*2).xyz-position,ENV_POINT(index*2).w,
            ENV_POINT(index*2+1).rgb,axis,ENV_POINT(9+index*3),ENV_POINT(10+index*3),
            axis.xyz,false,normal,view,albedo,f0,roughness,metallic,localLightConsumer==128u);
        vec2 lobes=localLightLobes(ENV_POINT(index*2+1).w);
        float visibility=environmentLocalShadowVisibility(16+index,position,normal);
        native.diffuse*=lobes.x*visibility;native.specular*=lobes.y*visibility;return native;
    }
    vec3 light,radiance;
    if(!environmentPointSample(index,position,light,radiance))return emptyLocalLightResponse();
    float nv=max(dot(normal,view),0.0),nl=max(dot(normal,light),0.0);
    FiniteLightResponse result=environmentPointBrdf(normal,view,light,nv,nl,albedo,f0,roughness,metallic);
    vec2 lobes=localLightLobes(ENV_POINT(index*2+1).w);
    result.diffuse*=radiance*nl*lobes.x;result.specular*=radiance*nl*lobes.y;
    return result;
}
struct WorldRectPbrContext {vec3 tangent;vec4 ltc;vec2 amplitude;};
// Implicit-derivative LUT sampling belongs outside the varying light loop.
WorldRectPbrContext prepareWorldRectPbr(vec3 normal,vec3 view,float roughness) {
    float nv=max(dot(normal,view),0.0);
    vec3 tangent=rectTangent(normal,view);
    vec2 lutUv=(vec2(clamp(roughness,0.0,1.0),sqrt(1.0-clamp(nv,0.0,1.0)))*63.0+0.5)/64.0;
    vec4 ltc=ENV_RECT_LUT(vec2(lutUv.x*0.5,lutUv.y));
    vec2 amplitude=ENV_RECT_LUT(vec2(0.5+lutUv.x*0.5,lutUv.y)).xy;
    WorldRectPbrContext context;
    context.tangent=tangent;context.ltc=ltc;context.amplitude=amplitude;
    return context;
}
FiniteLightResponse environmentRectPbr(int index,vec3 position,vec3 normal,vec3 view,
        vec3 albedo,vec3 f0,float roughness,float metallic,WorldRectPbrContext context,float visibility) {
    FiniteLightResponse result=emptyLocalLightResponse();
    if (ENV_RECT(index*4+1).w>.5) {
        if (visibility>0.0) {
            result=environmentFiniteRectangle(index,position,normal,view,albedo,f0,roughness,metallic);
            result.diffuse*=visibility;result.specular*=visibility;
        }
        return result;
    }
    vec3 delta,u,v,radiance;
    if (!environmentRectSample(index,position,delta,u,v,radiance)) return result;
#if PHLOSION_EARLY_LOCAL_SHADOW
    if (visibility<=0.0) return result;
#endif
    float diffuse=rectIntegral(delta,u,v,normal,context.tangent,vec4(1.0,0.0,0.0,1.0));
    float specular=rectIntegral(delta,u,v,normal,context.tangent,context.ltc);
    vec3 fresnel=f0*context.amplitude.x+(vec3(1.0,1.0,1.0)-f0)*context.amplitude.y;
    vec2 lobes=localLightLobes(ENV_RECT(index*4+3).w);
    result.diffuse=radiance*((vec3(1.0,1.0,1.0)-f0)*(1.0-metallic)*albedo*diffuse)*visibility*lobes.x;
    result.specular=radiance*(fresnel*specular)*visibility*lobes.y;
    return result;
}
// Caller has enabled the environment and prepared a view-facing receiver.
vec3 environmentPreparedLocalDirectPbrWeighted(vec3 position,vec3 normal,vec3 view,
        vec3 albedo,vec3 f0,float roughness,float metallic,vec3 diffuseFactor,float specularFactor) {
    vec3 result=vec3(0.0,0.0,0.0);
    float nv=max(dot(normal,view),0.0);
    PHLOSION_LOCAL_LIGHT_LOOP for(int pointIndex=0;pointIndex<4;++pointIndex) {
        if((uint(ENV_POINT(8+pointIndex*3).w)&1u)!=0u) {
            FiniteLightResponse response=environmentPointPbr(pointIndex,position,normal,view,albedo,f0,roughness,metallic);
            result+=response.diffuse*diffuseFactor+response.specular*specularFactor;
            continue;
        }
        vec3 light,radiance;
        if(!environmentPointSample(pointIndex,position,light,radiance))continue;
        float nl=max(dot(normal,light),0.0);
        FiniteLightResponse response=environmentPointBrdf(normal,view,light,nv,nl,albedo,f0,roughness,metallic);
        vec2 lobes=localLightLobes(ENV_POINT(pointIndex*2+1).w);
        result+=(response.diffuse*lobes.x*diffuseFactor+response.specular*lobes.y*specularFactor)*radiance*nl;
    }
    WorldRectPbrContext context=prepareWorldRectPbr(normal,view,roughness);
    PHLOSION_LOCAL_LIGHT_LOOP for(int rectIndex=0;rectIndex<16;++rectIndex) {
        // Preserve the forward branch/accumulator lifetime. The finite evaluator
        // already exposes both lobes; apply their separate contact response.
        if(ENV_RECT(rectIndex*4+1).w>.5) {
            float visibility=environmentLocalShadowVisibility(rectIndex,position,normal);
            if(visibility>0.0) {
                FiniteLightResponse light=environmentFiniteRectangle(rectIndex,position,normal,view,
                    albedo,f0,roughness,metallic);
                result+=(light.diffuse*diffuseFactor+light.specular*specularFactor)*visibility;
            }
            continue;
        }
        vec3 delta,u,v,radiance;
        if(!environmentRectSample(rectIndex,position,delta,u,v,radiance))continue;
        float visibility=environmentLocalShadowVisibility(rectIndex,position,normal);
#if PHLOSION_EARLY_LOCAL_SHADOW
        if(visibility<=0.0)continue;
#endif
        FiniteLightResponse light=environmentRectPbr(rectIndex,position,normal,view,albedo,f0,roughness,metallic,context,visibility);
        result+=light.diffuse*diffuseFactor+light.specular*specularFactor;
    }
    return result;
}
vec3 environmentPreparedLocalDirectPbr(vec3 position,vec3 normal,vec3 view,
        vec3 albedo,vec3 f0,float roughness,float metallic) {
    return environmentPreparedLocalDirectPbrWeighted(position,normal,view,albedo,f0,roughness,metallic,vec3(1.0,1.0,1.0),1.0);
}
vec3 environmentLocalDirectPbr(vec3 position,vec3 normal,vec3 view,
        vec3 albedo,vec3 f0,float roughness,float metallic) {
    if(ENV_SUN.w<.5)return vec3(0.0,0.0,0.0);
    if(dot(normal,view)<0.0)normal=-normal;
    if(dot(normal,view)<=0.0)return vec3(0.0,0.0,0.0);
    return environmentPreparedLocalDirectPbr(position,normal,view,albedo,f0,roughness,metallic);
}
