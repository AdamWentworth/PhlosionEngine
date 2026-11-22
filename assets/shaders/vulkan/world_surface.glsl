// Compact evaluated surfaces: RGB10A2 sqrt-albedo, RGB10A2 normal/class,
// RGBA8 metal/F0/AO/roughness and R11G11B10F HDR emission. No lighting.
// Normal alpha: 0 empty, 1/3 opaque evaluated, 2/3 masked evaluated, 1 motion-only.
// Albedo alpha: 0 shared standard local lights with forward primary shading;
// 1/3 reflection attributes with specialized forward local lighting.
// Albedo alpha 2/3 enrolls standard primary composition; it still receives clustered light.
// Albedo alpha 1 selects composed foliage; material X carries transmission, Z carries reflection weight (AO is baked in color).
// These are Phlosion participation classes, not source stencil bit identities.
struct WorldSurfaceMaterial {
    vec3 albedo;
    vec3 normal;
    vec4 orm;
    float metallic;
    float roughness;
    float occlusion;
    float dielectricSpecular;
};
struct WorldSurfaceBuffer {
    vec4 albedo;
    vec4 normalClass;
    vec4 material;
    vec4 emission;
};
WorldSurfaceBuffer packWorldSurface(WorldSurfaceMaterial surface, vec3 emission) {
    WorldSurfaceBuffer result;
    result.albedo = vec4(sqrt(clamp(worldWetEmissiveColor(surface.albedo,emission), 0.0, 1.0)), 0.0);
    result.normalClass = vec4(surface.normal * 0.5 + 0.5, 1.0/3.0);
    // Never put roughness or AO in the two-bit alpha channels.
    result.material = clamp(vec4(surface.metallic, surface.dielectricSpecular, surface.occlusion, surface.roughness), 0.0, 1.0);
    result.emission = vec4(emission, 0.0);
    return result;
}
WorldSurfaceMaterial unpackWorldSurface(WorldSurfaceBuffer data) {
    WorldSurfaceMaterial result;
    result.albedo = data.albedo.rgb * data.albedo.rgb;
    result.occlusion = data.albedo.a>.84?1.0:data.material.z;
    result.normal = data.normalClass.rgb * 2.0 - 1.0;
    result.normal *= inversesqrt(max(dot(result.normal, result.normal), 1e-12));
    result.roughness = data.material.w;
    result.metallic = data.albedo.a>.84?0.0:data.material.x;
    result.dielectricSpecular = data.material.y;
    result.orm = vec4(result.occlusion, result.roughness, result.metallic, 1.0);
    return result;
}
#ifdef PHLOSION_WORLD_SURFACE_CAPTURE
WorldSurfaceBuffer worldSurfaceOutput;
bool worldSurfaceValid = false;
void storeWorldSurface(WorldSurfaceMaterial surface, vec3 emission) {
    worldSurfaceOutput = packWorldSurface(surface, emission);
#ifdef CLUSTER_VIEW
    if(CLUSTER_VIEW(4).z>.5)worldSurfaceOutput.albedo.a=2.0/3.0;
#endif
    worldSurfaceValid = true;
}
// Reflection/contact attributes do not imply an ordinary deferred BRDF. Keep
// this route marked so lighting dispatch retains the specialized forward shader.
void storeWorldForwardSurface(WorldSurfaceMaterial surface, vec3 emission) {
    storeWorldSurface(surface, emission);
    worldSurfaceOutput.albedo.a = 1.0/3.0;
}
// Specialized sunlight may still use standard local-light response. Keep it out
// of primary composition while enrolling its evaluated attributes in clustering.
void storeWorldClusteredForwardSurface(WorldSurfaceMaterial surface, vec3 emission) {
    worldSurfaceOutput = packWorldSurface(surface, emission);
    worldSurfaceValid = true;
}
void storeWorldFoliageSurface(WorldSurfaceMaterial surface,float transmission,float reflectionWeight) {
    storeWorldClusteredForwardSurface(surface,vec3(0.0,0.0,0.0));
#ifdef CLUSTER_VIEW
    if(CLUSTER_VIEW(4).z>.5) {
        worldSurfaceOutput.albedo.a=1.0;
        worldSurfaceOutput.material.x=clamp(transmission,0.0,1.0);
        worldSurfaceOutput.material.z=clamp(reflectionWeight,0.0,1.0);
    }
#endif
}

#endif

// Static reflection materials have no object identity or motion. Keep their
// quantized core in two integer words; authored dynamic controls are separate.
uvec2 packWorldProbeMaterial(WorldSurfaceBuffer surface,float viewDepth,vec3 right,vec3 up) {
    uvec3 color=uvec3(floor(clamp(surface.albedo.rgb,0.0,1.0)*255.0));
    vec3 n=normalize(surface.normalClass.xyz*2.0-vec3(1.0,1.0,1.0));
    n=vec3(dot(n,right),dot(n,up),-dot(n,cross(right,up)));
    uvec2 normal=uvec2(floor(clamp(n.xy+vec2(1.0,1.0),0.0,2.0)*127.0));
    uint metal=min(3u,uint(floor(clamp(surface.albedo.a>.84?0.0:surface.material.x,0.0,1.0)*255.0/80.0+.5)));
    uint rough=min(7u,uint(floor(clamp(surface.material.w,0.0,1.0)*255.0/32.0+.5)));
    uint emission=dot(surface.emission.rgb,vec3(.2126,.7152,.0722))>0.0?7u:0u;
    uint depth=uint(floor(log(1.0+clamp(viewDepth,0.0,1000.0))/7.0*8191.0));
    return uvec2(color.x|(color.y<<8u)|(color.z<<16u)|((metal|(emission<<2u)|(rough<<5u))<<24u),
        normal.x|(normal.y<<8u)|(n.z<0.0?65536u:0u)|(depth<<19u));
}
