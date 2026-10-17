// Capture includes emissive geometry. Callers should suppress only emitters
// already represented by an analytic area source in the direct specular BRDF.
bool worldReflectionCapture() { return PLANAR(4).w < -0.5; }
bool worldReflectionFrontFace(bool front) { return worldReflectionCapture() ? !front : front; }
void worldReflectionClip(vec3 position, float materialMode) {
    if (worldReflectionCapture() && materialMode > 0.5 && position.y < PLANAR(4).x + .012)
        discard;
}
// Keep this hook for existing composed material packages. Emission selection
// belongs to the caller's capture material overrides, not a blanket shader gate.
vec3 worldReflectionEmission(vec3 emission) { return emission; }
vec3 worldPlanarTap(vec2 uv) {
    vec2 margin=PLANAR(5).xy*PLANAR(5).w;
    return PLANAR_SAMPLE(clamp(uv,margin,PLANAR(5).xy-margin)).rgb;
}
vec3 worldPlanarFiltered(vec2 uv, float roughness) {
    // Nine bilinear taps form a normalized, symmetric 2D Gaussian instead of
    // separate cross-shaped copies. This is a bounded image-space approximation,
    // not a GGX-prefiltered radiance hierarchy. Correct for viewport aspect and
    // the padded texture bucket so the footprint is circular in pixels.
    vec2 radius=PLANAR(5).xy*vec2(PLANAR(4).z,1.0)*(.02*roughness*roughness*1.182426);
    vec3 center=worldPlanarTap(uv)*.162102;
    vec3 axial=worldPlanarTap(uv+vec2(radius.x,0))+worldPlanarTap(uv-vec2(radius.x,0));
    axial+=worldPlanarTap(uv+vec2(0,radius.y))+worldPlanarTap(uv-vec2(0,radius.y));
    vec3 diagonal=worldPlanarTap(uv+radius)+worldPlanarTap(uv-radius);
    diagonal+=worldPlanarTap(uv+vec2(radius.x,-radius.y))+worldPlanarTap(uv+vec2(-radius.x,radius.y));
    return center+axial*.120259+diagonal*.0892155;
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
    vec3 radiance = worldPlanarFiltered(uv,roughness);
    // Split-sum GGX DFG in the visible hemisphere, matching local two-sided
    // lighting while leaving the legacy dry/directional normal policy intact.
    float nv=max(dot(normal,view),0.0);
    vec4 fit=roughness*vec4(-1.0,-.0275,-.572,.022)+vec4(1.0,.0425,1.04,-.04);
    float a=min(fit.x*fit.x,exp2(-9.28*nv))*fit.x+fit.y;
    vec2 dfg=vec2(-1.04,1.04)*a+fit.zw;
    vec3 specular=max(radiance,vec3(0.0,0.0,0.0))*(f0*dfg.x+dfg.y)*.44;
    return mix(fallback,specular,weight);
}
