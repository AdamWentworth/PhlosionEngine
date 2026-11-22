void computeFixture(uint i,out vec4 first,out vec4 second) {
    float f=float(i)/31.0;
    WorldSurfaceMaterial s;
    s.albedo=vec3(f,.25,.75);s.normal=vec3(0.0,.6,.8);s.orm=vec4(0.0,0.0,0.0,0.0);
    s.metallic=0.0;s.roughness=.2+.8*f;s.occlusion=1.0;s.dielectricSpecular=.04;
    storeWorldFoliageSurface(s,f,1.0-f);
    WorldSurfaceMaterial v=unpackWorldSurface(worldSurfaceOutput);
    first=vec4(worldSurfaceOutput.albedo.a,worldSurfaceOutput.material.x,v.metallic,worldSurfaceOutput.material.z);
    second=vec4(v.albedo.x,v.roughness,v.occlusion,v.dielectricSpecular);
}
