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
bool worldScreenReflectionCapture();
vec3 worldPlanarCoordinates(vec3 position, vec3 normal, vec3 view, float roughness) {
    if (worldScreenReflectionCapture()) return vec3(0.0,0.0,0.0);
    if (PLANAR(4).w < .5 || ENV_WEATHER.x <= 0.0 || abs(normal.y) < .8 ||
        abs(position.y - PLANAR(4).x) > PLANAR(4).y || roughness >= .55) return vec3(0.0,0.0,0.0);
    if (dot(normal,view)<0.0) normal=-normal;
    float wet = ENV_WEATHER_MATERIAL.y > .5 ? 1.0 : environmentSurfaceWetness(position, normal);
    float weight = wet * (1.0-smoothstep(.25,.55,roughness));
    vec4 planePosition = vec4(position.x,PLANAR(4).x,position.z,1.0);
    vec4 clip = vec4(dot(PLANAR(0),planePosition),dot(PLANAR(1),planePosition),dot(PLANAR(2),planePosition),dot(PLANAR(3),planePosition));
    if (clip.w <= .001) return vec3(0.0,0.0,0.0);
    vec2 uv = clip.xy / clip.w * .5 + .5;
    // Small normal perturbations break up the silhouette without sliding the
    // whole reflection. Fade at the view boundary rather than clamp-smearing.
    uv += normal.xz * .025;
    float edge = min(min(uv.x,uv.y),min(1.0-uv.x,1.0-uv.y));
    weight *= smoothstep(0.0,.035,edge);
    if (weight <= 0.0) return vec3(0.0,0.0,0.0);
    if (PLANAR(5).z > .5) uv.y = 1.0-uv.y;
    uv *= PLANAR(5).xy;
    return vec3(uv,weight);
}
vec3 worldPlanarRadiance(vec3 fallback, vec3 position, vec3 normal, vec3 view, float roughness) {
    vec3 source=worldPlanarCoordinates(position,normal,view,roughness);
    if(source.z<=0.0)return fallback;
    vec3 radiance = worldPlanarFiltered(source.xy,roughness);
    // Both inputs are scene-linear radiance. Apply the receiving material and
    // occlusion once, after source blending, without fading diffuse multiscatter.
    return mix(fallback,max(radiance,vec3(0.0,0.0,0.0)),source.z);
}
