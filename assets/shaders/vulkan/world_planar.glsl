// Scene radiance only: analytic source reflections remain in the direct BRDF.
// Capture excludes emission, preventing a second copy of the same light energy.
bool worldReflectionCapture() { return PLANAR(4).w < -0.5; }
bool worldReflectionFrontFace(bool front) { return worldReflectionCapture() ? !front : front; }
void worldReflectionClip(vec3 position, float materialMode) {
    if (worldReflectionCapture() && materialMode > 0.5 && position.y < PLANAR(4).x + .012)
        discard;
}
vec3 worldReflectionEmission(vec3 emission) {
    return worldReflectionCapture() ? vec3(0.0,0.0,0.0) : emission;
}
vec3 worldPlanarTap(vec2 uv) {
    vec2 margin=PLANAR(5).xy*PLANAR(5).w;
    return PLANAR_SAMPLE(clamp(uv,margin,PLANAR(5).xy-margin)).rgb;
}
vec3 worldPlanarSpecular(vec3 fallback, vec3 position, vec3 normal, vec3 view, vec3 f0, float roughness) {
    if (PLANAR(4).w < .5 || ENV_WEATHER.x <= 0.0 || abs(normal.y) < .8 ||
        abs(position.y - PLANAR(4).x) > PLANAR(4).y || roughness >= .55) return fallback;
    if (dot(normal,view)<0.0) normal=-normal;
    float wet = ENV_WEATHER_MATERIAL.y > .5 ? 1.0 : environmentSurfaceWetness(position, normal);
    float weight = wet * (1.0-smoothstep(.25,.55,roughness));
    vec4 planePosition = vec4(position.x,PLANAR(4).x,position.z,1.0);
    vec4 clip = vec4(dot(PLANAR(0),planePosition),dot(PLANAR(1),planePosition),dot(PLANAR(2),planePosition),dot(PLANAR(3),planePosition));
    if (clip.w <= .001) return fallback;
    vec2 uv = clip.xy / clip.w * .5 + .5;
    // Small normal perturbations break up the silhouette without sliding the
    // whole reflection. Fade at the view boundary rather than clamp-smearing.
    uv += normal.xz * .025;
    float edge = min(min(uv.x,uv.y),min(1.0-uv.x,1.0-uv.y));
    weight *= smoothstep(0.0,.035,edge);
    if (weight <= 0.0) return fallback;
    if (PLANAR(5).z > .5) uv.y = 1.0-uv.y;
    uv *= PLANAR(5).xy;
    vec2 radius = PLANAR(5).xy * PLANAR(4).z * roughness * roughness;
    vec3 radiance = worldPlanarTap(uv) * .4;
    radiance += worldPlanarTap(uv+vec2(radius.x,0)) * .15;
    radiance += worldPlanarTap(uv-vec2(radius.x,0)) * .15;
    radiance += worldPlanarTap(uv+vec2(0,radius.y)) * .15;
    radiance += worldPlanarTap(uv-vec2(0,radius.y)) * .15;
    // Split-sum GGX DFG in the visible hemisphere, matching local two-sided
    // lighting while leaving the legacy dry/directional normal policy intact.
    float nv=max(dot(normal,view),0.0);
    vec4 fit=roughness*vec4(-1.0,-.0275,-.572,.022)+vec4(1.0,.0425,1.04,-.04);
    float a=min(fit.x*fit.x,exp2(-9.28*nv))*fit.x+fit.y;
    vec2 dfg=vec2(-1.04,1.04)*a+fit.zw;
    vec3 specular=max(radiance,vec3(0.0,0.0,0.0))*(f0*dfg.x+dfg.y)*.44;
    return mix(fallback,specular,weight);
}
