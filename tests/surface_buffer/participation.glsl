void computeFixture(uint i,out vec4 first,out vec4 second) {
    float f=float(i)/31.0;
    WorldSurfaceMaterial s;
    s.albedo=vec3(f,.25,.75);s.normal=vec3(0.0,0.0,1.0);s.orm=vec4(0.0,0.0,0.0,0.0);
    s.metallic=0.0;s.roughness=.2+.8*f;s.occlusion=1.0;s.dielectricSpecular=.04;
    storeWorldClusteredForwardSurface(s,vec3(0.0,0.0,0.0));
    WorldSurfaceMaterial restored=unpackWorldSurface(worldSurfaceOutput);
    first=vec4(worldSurfaceOutput.albedo.a,worldSurfaceOutput.normalClass.a,
               restored.albedo.x,restored.roughness);
    float valid=worldSurfaceValid?1.0:0.0;
    storeWorldSurface(s,vec3(0.0,0.0,0.0));float composed=worldSurfaceOutput.albedo.a;
    storeWorldForwardSurface(s,vec3(0.0,0.0,0.0));float specialized=worldSurfaceOutput.albedo.a;
    second=vec4(composed,specialized,valid,restored.dielectricSpecular);
}
