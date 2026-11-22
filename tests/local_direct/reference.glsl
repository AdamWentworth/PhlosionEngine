// Engine 702c35c forward response, retained as a regression reference.
vec3 referenceLocalPbr(vec3 position, vec3 normal, vec3 view,
                         vec3 albedo, vec3 f0, float roughness, float metallic, float occlusion) {
    vec3 result = vec3(0.0, 0.0, 0.0);
    if (ENV_SUN.w < 0.5) return result;
    // These draws are two-sided, including source meshes with differing winding.
    // Resolve the local BRDF in the visible hemisphere without changing the
    // established directional/inspection normal interpretation.
    if (dot(normal, view) < 0.0) normal = -normal;
    float nv = max(dot(normal, view), 0.0);
    if (nv <= 0.0) return result;
    DIRECT_TEST_LOOP for (int index = 0; index < 4; ++index) {
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
    DIRECT_TEST_LOOP for (int index=0;index<16;++index) {
        vec3 delta,u,v,radiance;
        if (ENV_RECT(index*4+1).w>.5) {
            float visibility=environmentLocalShadowVisibility(index,position,normal);
            if (visibility>0.0) {
                FiniteLightResponse response=environmentFiniteRectangle(index,position,normal,view,
                    albedo,f0,roughness,metallic);
                result+=(response.diffuse+response.specular)*visibility;
            }
            continue;
        }
        if (!environmentRectSample(index,position,delta,u,v,radiance)) continue;
#if PHLOSION_EARLY_LOCAL_SHADOW
        float visibility=environmentLocalShadowVisibility(index,position,normal);
        if (visibility<=0.0) continue;
#endif
        float diffuse=rectIntegral(delta,u,v,normal,tangent,vec4(1.0,0.0,0.0,1.0));
        float specular=rectIntegral(delta,u,v,normal,tangent,ltc);
        vec3 fresnel=f0*amplitude.x+(vec3(1.0,1.0,1.0)-f0)*amplitude.y;
#if PHLOSION_EARLY_LOCAL_SHADOW
        result+=radiance*((vec3(1.0,1.0,1.0)-f0)*(1.0-metallic)*albedo*diffuse+fresnel*specular)*visibility;
#else
        result+=radiance*((vec3(1.0,1.0,1.0)-f0)*(1.0-metallic)*albedo*diffuse+fresnel*specular)*environmentLocalShadowVisibility(index,position,normal);
#endif
    }
    return result+(vec3(1.0,1.0,1.0)-f0)*(1.0-metallic)*albedo*
        environmentDiffuseProbeIrradiance(position,normal)*environmentDiffuseOcclusion(occlusion,albedo)/3.14159265;
}