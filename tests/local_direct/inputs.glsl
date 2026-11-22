#ifdef FIXTURE_GLOBAL
#define DIRECT_TEST_LOOP [loop] [fastopt]
#define LOCAL_LIGHT_STORAGE static
#else
#define DIRECT_TEST_LOOP
#endif
// Synthetic packed emitters and sampled transport exercise both native compilers.
#define PHLOSION_LOCAL_LIGHT_LOOP DIRECT_TEST_LOOP
#define PHLOSION_EARLY_LOCAL_SHADOW 1
#define ENV_SUN vec4(0.0,1.0,0.0,1.0)
#define ENV_POINT(i) testPoint(i)
#define ENV_RECT(i) testRect(i)
#define ENV_RECT_LUT(uv) testLut(uv)
vec4 testPoint(int i) {
    if(i>=8)return vec4(0.0,0.0,0.0,0.0);
    int light=i/2;
    if ((i&1)==0) return vec4(float(light)*2.0-3.0,2.0+float(light),1.0,12.0);
    return vec4(.3+float(light),2.0,.7,light==0?16777215.0:65535.0);
}
vec4 testRect(int i) {
    int light=i/4,row=i%4;
    if (row==0) return vec4(float(light%4)*2.0-3.0,2.0+float(light/4),float(light%3)-1.0,8.0);
    if (row==1) return vec4(.5+float(light%3)*.5,0.0,0.0,float(light&1));
    if (row==2) return vec4(0.0,0.0,.125,0.0);
    return vec4(.5+float(light%3),.2,1.2,65535.0);
}
vec4 testLut(vec2 uv) {
    if(uv.x<.5) return vec4(.8+uv.x,.03,.05,.7+uv.y*.3);
    return vec4(.8,.05,0.0,0.0);
}
float environmentLocalShadowVisibility(int i,vec3 position,vec3 normal) {
    return (i%4)==0 ? 0.0 : (i%4)==1 ? .25 : (i%4)==2 ? .75 : 1.0;
}
vec3 environmentDiffuseProbeIrradiance(vec3 position,vec3 normal) {
    return vec3(.1,.3,.7)*(1.0+abs(normal.y)*.3);
}
vec3 environmentDiffuseOcclusion(float visibility,vec3 albedo) {
    return clamp(vec3(visibility,visibility,visibility),0.0,1.0);
}
