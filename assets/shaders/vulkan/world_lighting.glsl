// LTC edge integral adapted from Heitz, Dupuy, Hill and Neubelt (SIGGRAPH 2016).
// Attribution and redistribution terms: third_party/ltc/LICENSE.
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
    if (ENV_SUN.w<0.5 || source.w<=0.0 || max(radiance.r,max(radiance.g,radiance.b))<=0.0)
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

// Optional world-space lights, shared by ordinary and composed materials.
bool environmentPointSample(int index, vec3 position, out vec3 light, out vec3 radiance) {
    light = vec3(0.0, 0.0, 0.0);
    radiance = vec3(0.0, 0.0, 0.0);
    vec4 source = ENV_POINT(index * 2);
    vec3 intensity = ENV_POINT(index * 2 + 1).rgb;
    if (ENV_SUN.w < 0.5 || source.w <= 0.0 ||
        max(intensity.r, max(intensity.g, intensity.b)) <= 0.0) return false;
    vec3 delta = source.xyz - position;
    float distanceSquared = dot(delta, delta);
    float rangeSquared = source.w * source.w;
    if (distanceSquared >= rangeSquared) return false;
    light = delta / sqrt(max(distanceSquared, 1e-8));
    float cutoff = max(1.0 - distanceSquared * distanceSquared / (rangeSquared * rangeSquared), 0.0);
    radiance = intensity * cutoff * cutoff / max(distanceSquared, 0.25);
    return true;
}

// Layered/toon source bodies retain their authored specular model. Add local
// diffuse energy without introducing a generic GGX outer coat or changing emission.
vec3 environmentLocalDiffuse(vec3 position, vec3 normal, vec3 view, vec3 albedo) {
    vec3 result = vec3(0.0, 0.0, 0.0);
    if (dot(normal, view) < 0.0) normal = -normal;
    for (int index = 0; index < 4; ++index) {
        vec3 light, radiance;
        if (environmentPointSample(index, position, light, radiance))
            result += albedo * radiance * max(dot(normal, light), 0.0) / 3.14159265;
    }
    vec3 tangent=rectTangent(normal,view);
    for (int index=0;index<16;++index) {
        vec3 delta,u,v,radiance;
        if (environmentRectSample(index,position,delta,u,v,radiance))
            result+=albedo*radiance*rectIntegral(delta,u,v,normal,tangent,vec4(1.0,0.0,0.0,1.0));
    }
    return result;
}

vec3 environmentLocalPbr(vec3 position, vec3 normal, vec3 view,
                         vec3 albedo, vec3 f0, float roughness, float metallic) {
    vec3 result = vec3(0.0, 0.0, 0.0);
    if (ENV_SUN.w < 0.5) return result;
    // These draws are two-sided, including source meshes with differing winding.
    // Resolve the local BRDF in the visible hemisphere without changing the
    // established directional/inspection normal interpretation.
    if (dot(normal, view) < 0.0) normal = -normal;
    float nv = max(dot(normal, view), 0.0);
    if (nv <= 0.0) return result;
    for (int index = 0; index < 4; ++index) {
        vec3 light, radiance;
        if (!environmentPointSample(index, position, light, radiance)) continue;
        float nl = max(dot(normal, light), 0.0);
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
        result += (diffuse + specular) * radiance * nl;
    }
    vec3 tangent=rectTangent(normal,view);
    vec2 lutUv=(vec2(clamp(roughness,0.0,1.0),sqrt(1.0-clamp(nv,0.0,1.0)))*63.0+0.5)/64.0;
    vec4 ltc=ENV_RECT_LUT(vec2(lutUv.x*0.5,lutUv.y));
    vec2 amplitude=ENV_RECT_LUT(vec2(0.5+lutUv.x*0.5,lutUv.y)).xy;
    for (int index=0;index<16;++index) {
        vec3 delta,u,v,radiance;
        if (!environmentRectSample(index,position,delta,u,v,radiance)) continue;
        float diffuse=rectIntegral(delta,u,v,normal,tangent,vec4(1.0,0.0,0.0,1.0));
        float specular=rectIntegral(delta,u,v,normal,tangent,ltc);
        vec3 fresnel=f0*amplitude.x+(vec3(1.0,1.0,1.0)-f0)*amplitude.y;
        result+=radiance*((vec3(1.0,1.0,1.0)-f0)*(1.0-metallic)*albedo*diffuse+fresnel*specular);
    }
    return result;
}
