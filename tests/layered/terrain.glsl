void computeFixture(uint i,out vec4 first,out vec4 second) {
    vec3 n=normalize(vec3(float(int(i%3u)-1),float(int((i/3u)%3u)-1),.5+float(i%2u)));
    vec3 w=worldTerrainProjectionWeights(n);
    uint axis=worldTerrainProjectionAxis(w);
    vec2 uv=worldTerrainPlanar(vec3(-137.0+float(i),91.0,-35.0),axis);
    vec3 result=worldTerrainLayerNormal(n,vec3(.2,-.3,sqrt(.87)),1.6);
    first=vec4(uv,float(axis),w.z);second=vec4(result,0.04*.65);
}
