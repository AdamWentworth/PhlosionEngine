// Preserve the original clip depth for motion while rasterizing depth billboards.
#ifdef PHLOSION_FOLIAGE_HLSL
static
#endif
bool worldFoliageDepthProxy=false;
#ifdef PHLOSION_FOLIAGE_HLSL
static
#endif
float worldFoliageOriginalClipZ=0.0;
// Authored panel orientation; the captured normal basis stays unchanged.
vec3 worldFoliageUnit(vec3 v,vec3 fallback) {
    return dot(v,v)>1e-12?v/sqrt(dot(v,v)):fallback;
}
float worldFoliageBillboardCoverage(vec3 plane,vec3 toCamera,vec3 up,float views) {
    if(dot(plane,up)>.5)return -2.0;
    vec3 a=worldFoliageUnit(plane-up*dot(plane,up),vec3(1.0,0.0,0.0));
    vec3 b=worldFoliageUnit(toCamera-up*dot(toCamera,up),a);
    float angle=acos(clamp(dot(a,b),-1.0,1.0));
    float middle=3.141592653589793/max(views,1.0),width=.13962634015954636;
    return angle>middle+width?-2.0:sqrt(clamp((middle-angle+width)/(2.0*width),0.0,1.0));
}
vec3 worldFoliageBillboardPosition(vec3 local,vec3 localNormal,vec4 localTangent,
        vec3 c0,vec3 c1,vec3 c2,vec3 root,vec3 camera,vec3 forward,
        float perspective,float crownRadius,out float coverage,float views) {
    float scale=max(length(c2),.0001);
    vec3 up=c2/scale;
    vec3 n=worldFoliageUnit(c0*localNormal.x+c1*localNormal.y+c2*localNormal.z,up);
    vec3 t=worldFoliageUnit(c0*localTangent.x+c1*localTangent.y+c2*localTangent.z,c0/scale);
    vec3 b=cross(t,n)*localTangent.w;
    vec3 toward=worldFoliageUnit(camera-perspective*root,-forward);
    coverage=worldFoliageBillboardCoverage(n,toward,up,views);
    vec3 horizontal=worldFoliageUnit(toward-up*dot(toward,up),n);
    vec3 right=cross(up,horizontal);
    vec3 original=c0*local.x+c1*local.y+c2*local.z;
    vec3 rotated=right*dot(original,t)+up*dot(original,b)-horizontal*dot(original,n);
    vec3 away=perspective>.5?worldFoliageUnit(root+rotated-camera,forward):forward;
    float correction=perspective>.5?1.0/max(dot(forward,away),.5):1.0;
    rotated+=away*(crownRadius*scale*.5*correction);
    return vec3(dot(rotated,c0),dot(rotated,c1),dot(rotated,c2))/(scale*scale);
}
