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
    return result;
}
