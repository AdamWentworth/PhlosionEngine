
void computeFixture(uint i, out vec4 first, out vec4 second) {
    uint c = i / 4u;
    uint plane = i % 4u;
    float f = float(c);
    WorldSurfaceMaterial s;
    s.albedo = vec3(f / 8.0, 0.25, 0.75);
    s.normal = vec3(f - 3.0, 2.0, -1.0);
    s.normal *= inversesqrt(dot(s.normal, s.normal));
    s.orm = vec4(0.0,0.0,0.0,0.0);
    s.metallic = f / 7.0;
    s.roughness = 0.04 + f * 0.12;
    s.occlusion = f * 0.2;
    s.dielectricSpecular = 0.02 + f * 0.01;
    WorldSurfaceBuffer packed = packWorldSurface(s, vec3(f * 4.0, 0.5, 1000.0));
    WorldSurfaceMaterial restored = unpackWorldSurface(packed);
    if (plane == 0u) {
        first = packed.albedo;
        second = vec4(restored.albedo, restored.occlusion);
    } else if (plane == 1u) {
        first = packed.normalClass;
        second = vec4(restored.normal, restored.roughness);
    } else if (plane == 2u) {
        first = packed.material;
        second = vec4(restored.metallic, restored.dielectricSpecular, restored.orm.r, restored.orm.g);
    } else {
        first = packed.emission;
        second = vec4(restored.orm.b, restored.orm.a, 0.0, 0.0);
    }
}
