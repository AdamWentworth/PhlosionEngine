// Thin-leaf material response. Tangent and bitangent retain their orientation
// on the back face; only the geometric normal changes hemisphere.
vec3 worldFoliageNormal(vec3 normal,vec4 tangent,vec3 texel,bool frontFace) {
    vec3 n=finiteLightUnit(normal);
    vec3 t=finiteLightUnit(tangent.xyz-n*dot(n,tangent.xyz));
    vec3 b=cross(n,t)*tangent.w;
    return finiteLightUnit(t*(texel.x*2.0-1.0)+b*(texel.y*2.0-1.0)+
        n*((texel.z*2.0-1.0)*(frontFace?1.0:-1.0)));
}
vec3 worldFoliageSunDirection(vec3 normal,vec3 view,vec3 center,float angularRadius) {
    vec3 reflected=reflect(-view,normal);
    float projection=dot(center,reflected),edge=cos(angularRadius);
    vec3 offset=reflected-center*projection;
    if(projection>=edge)return reflected;
    // Antiparallel reflection has no unique rim point; use the disk center.
    if(dot(offset,offset)<1e-12)return center;
    return finiteLightUnit(center*edge+finiteLightUnit(offset)*sin(angularRadius));
}
float worldFoliageTransmission(float viewDotLight,float normalDotSun,float roughness,float transmission) {
    float width=.2+.5*roughness,width2=width*width;
    float back=clamp(-viewDotLight,0.0,1.0);
    float denominator=1.0+(width2-1.0)*back*back;
    return clamp(transmission,0.0,1.0)*(.5+.5*abs(normalDotSun))*
        width2/(3.141592653589793*denominator*denominator);
}
FiniteLightResponse worldFoliageDirectional(vec3 normal,vec3 view,vec3 center,
        float angularRadius,vec3 albedo,float roughness,float transmission) {
    vec3 light=worldFoliageSunDirection(normal,view,center,angularRadius);
    vec3 halfVector=finiteLightUnit(light+view);
    float nl=clamp(dot(normal,light),0.0,1.0),nv=clamp(dot(normal,view),.00001,1.0);
    float nh=clamp(dot(normal,halfVector),0.0,1.0),vh=clamp(dot(view,halfVector),0.0,1.0);
    float lh=clamp(dot(light,halfVector),0.0,1.0),sun=clamp(dot(normal,center),0.0,1.0);
    float edge=(.5+2.0*lh*lh)*roughness-1.0;
    float diffuse=(1.0+edge*finiteLightPow5(1.0-nl))*(1.0+edge*finiteLightPow5(1.0-nv))*
        (1.0-.337748344*roughness)/3.141592653589793;
    float a2=roughness*roughness;a2*=a2;
    float denominator=1.0+(a2-1.0)*nh*nh;
    float distribution=a2/(3.141592653589793*denominator*denominator);
    float visibility=.5/max(nl*sqrt(nv*nv*(1.0-a2)+a2)+nv*sqrt(nl*nl*(1.0-a2)+a2),1e-12);
    float specular=(.04+.96*finiteLightPow5(1.0-vh))*min(distribution*nl*visibility,100.0);
    FiniteLightResponse result;
    result.diffuse=albedo*(diffuse*sun+worldFoliageTransmission(dot(view,light),sun,roughness,transmission));
    result.specular=vec3(specular,specular,specular);
    return result;
}

float worldFoliageNoise(vec2 pixel,float frame) {
    return fract(52.9829189*fract(dot(pixel+vec2(frame*.7,frame*.7),vec2(.06711056,.00583715))));
}
float worldFoliageCutoff(vec2 dx,vec2 dy,float mipCount,float minimum,float maximum,vec2 pixel,float frame) {
    vec2 footprint=abs(dx)+abs(dy);
    float mip=clamp(max(0.0,log2(max(dot(footprint,footprint),1e-12)))/max(mipCount,1.0),0.0,1.0);
    return min(mix(minimum,maximum,mip)*(1.0+worldFoliageNoise(pixel,frame)),.9);
}
float worldFoliageBayer(vec2 pixel) {
    vec2 p=fract(floor(pixel)*.5)*2.0;
    return (p.y<.5?(p.x<.5?0.0f:2.0f):(p.x<.5?3.0f:1.0f))*.25f;
}
float worldFoliageBillboardAlpha(float alpha,float coverage,vec2 pixel) {
    return mix(coverage*worldFoliageBayer(pixel)*alpha,alpha,coverage);
}
vec3 worldFoliageTint(vec3 color,vec3 tint,float transmission,vec2 weights) {
    return tint.x<0.0?color:mix(mix(color,tint,weights.x*transmission),color*tint,weights.y*transmission);
}
float worldFoliageTerrainTransmission(float transmission,float terrainSpec,float height,float blendHeight) {
    return mix(transmission,terrainSpec,clamp(1.0-height/max(blendHeight,.001),0.0,1.0));
}
void worldFoliageTerrainBlend(inout vec3 color,inout vec3 normal,inout float transmission,
        vec3 terrainColor,vec3 terrainNormal,float terrainSpec,float height,
        float blendHeight,float distanceFromCamera,float maximumDistance) {
    float h=clamp(1.0-height/max(blendHeight,.001),0.0,1.0);
    float d=1.0-clamp(distanceFromCamera/max(maximumDistance,.001),0.0,1.0);
    d=1.0-.5*d*(1.0+d);
    transmission=worldFoliageTerrainTransmission(transmission,terrainSpec,height,blendHeight);
    float blend=mix(h,1.0,d);
    color=.2*color+.8*mix(color,terrainColor,blend);
    normal=finiteLightUnit(.5*normal+.5*mix(normal,terrainNormal,blend*blend));
}
// Keep the displaced material point on the pixel's camera ray.
vec3 worldFoliageDisplacedPosition(vec3 position,vec3 camera,vec3 forward,float perspective,float offset) {
    vec3 delta=position-camera;
    vec3 ray=perspective>.5?delta/max(dot(delta,forward),.0001):forward;
    return position+ray*offset;
}
// Perspective z=A+B/w; orthographic z varies linearly with view distance.
float worldFoliageProjectedDepth(float depth,float offset,vec3 projection) {
    float z=2.0*depth-1.0;
    if(projection.z>.5) {
        float w=projection.y/(z-projection.x);
        z=projection.x+projection.y/max(w+offset,.0001);
    } else z+=projection.x*offset;
    return clamp(.5*z+.5,0.0,1.0);
}

// Fragment-private depth state. Zero displacement preserves all other materials.
float worldFoliageDepthOffset=0.0;
vec3 worldFoliageDepthProjection=vec3(0.0,0.0,0.0);
float worldFoliageResolveDepth(float depth) {
    return worldFoliageDepthOffset==0.0?depth:
        worldFoliageProjectedDepth(depth,worldFoliageDepthOffset,worldFoliageDepthProjection);
}

// Absolute depth is used with a near-plane raster proxy for depth billboards.
// Keeping clip X/Y/W unchanged preserves coverage and perspective interpolation.
float worldFoliagePositionDepth(vec3 position,vec3 camera,vec3 forward,vec3 projection) {
    float z=projection.z>.5?
        projection.x+projection.y/max(dot(position-camera,forward),.0001):
        projection.x*dot(position,forward)+projection.y;
    return .5*z+.5;
}
float worldFoliageAbsoluteDepth=-1.0;
float worldFoliageRasterDepth(float rasterDepth) {
    return worldFoliageAbsoluteDepth<0.0?rasterDepth:worldFoliageAbsoluteDepth;
}
