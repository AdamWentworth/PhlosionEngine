#ifdef SCREEN_REFLECTION
// Retained dry materials with current illumination.
vec4 resolveWorldProbeSurface(vec2 pixel,vec3 camera) {
    ivec2 p=ivec2(pixel);
    uvec2 data=PROBE_DATA(p);
    if(data.y==0u || ((data.y>>17u)&3u)==3u)discard;
    vec2 uv=pixel/SCREEN_REFLECTION(8).xy;
    vec4 clip=vec4(uv.x*2.0-1.0,1.0-uv.y*2.0,1.0,1.0);
    vec4 h=vec4(dot(SCREEN_REFLECTION(4),clip),dot(SCREEN_REFLECTION(5),clip),
                dot(SCREEN_REFLECTION(6),clip),dot(SCREEN_REFLECTION(7),clip));
    vec4 center=vec4(SCREEN_REFLECTION(4).z+SCREEN_REFLECTION(4).w,
        SCREEN_REFLECTION(5).z+SCREEN_REFLECTION(5).w,
        SCREEN_REFLECTION(6).z+SCREEN_REFLECTION(6).w,
        SCREEN_REFLECTION(7).z+SCREEN_REFLECTION(7).w);
    vec3 ray=normalize(h.xyz/h.w-camera),forward=normalize(center.xyz/center.w-camera);
    float depth=exp(7.0*float(data.y>>19u)/8191.0)-1.0;
    vec3 position=camera+ray*(depth/max(dot(ray,forward),.00001));
    vec2 xy=vec2(float(data.y&255u),float((data.y>>8u)&255u))/127.0-vec2(1.0,1.0);
    vec3 right=normalize(vec3(SCREEN_REFLECTION(4).x,SCREEN_REFLECTION(5).x,SCREEN_REFLECTION(6).x));
    vec3 up=normalize(vec3(SCREEN_REFLECTION(4).y,SCREEN_REFLECTION(5).y,SCREEN_REFLECTION(6).y));
    float nz=sqrt(max(0.0,1.0-dot(xy,xy)))*((data.y&65536u)!=0u?-1.0:1.0);
    vec3 normal=normalize(right*xy.x+up*xy.y-cross(right,up)*nz);
    vec3 albedo=vec3(float(data.x&255u),float((data.x>>8u)&255u),float((data.x>>16u)&255u))/255.0;
    albedo*=albedo;
    uint mask=data.x>>24u;
    vec4 authored=PROBE_WEATHER(p);
    localLightReceiverOverride=uint(round(authored.w));localLightConsumer=8u;
    float metal=float(mask&3u)*(.8/3.0),dielectric=.04,occlusion=1.0;
    float roughness=max(float(mask>>5u)/7.0,4.0/256.0);
    environmentWetMaterialParameters(position,normal,albedo,roughness,dielectric,authored.xy,metal,normal);
    albedo=worldWetEmissiveColor(albedo,PROBE_EMISSION(p).rgb);
    vec3 view=normalize(camera-position);
    if(dot(normal,view)<0.0)normal=-normal;
    vec3 f0=mix(vec3(dielectric,dielectric,dielectric),albedo,metal);
    updateEnvironmentDirectionalVisibility(position,normal,2.0);
    vec3 light=environmentLightDirection();
    float nv=max(dot(normal,view),0.0),nl=max(dot(normal,light),0.0);
    FiniteLightResponse direct=environmentPointBrdf(normal,view,light,nv,nl,albedo,f0,max(.16,roughness),metal);
    vec3 lit=(direct.diffuse+direct.specular*environmentGlobalSpecularTint())*environmentLightRadiance()*nl;
    lit+=environmentLocalPbr(position,normal,view,albedo,f0,max(.16,roughness),metal,occlusion);
    vec3 singleScattering=vec3(0.0,0.0,0.0),multiScattering=vec3(0.0,0.0,0.0);
    environmentIndirectScattering(normal,view,f0,roughness,singleScattering,multiScattering);
    vec3 fresnel=f0+(max(vec3(1.0-roughness,1.0-roughness,1.0-roughness),f0)-f0)*pow(1.0-nv,5.0);
    vec3 diffuseWeight=(vec3(1.0,1.0,1.0)-fresnel)*(1.0-metal);
    vec3 ambient=worldIndirectAmbient(position,normal);
    vec3 radiance=worldLocalReflectionRadiance(ENV_AMBIENT.rgb*.5,position,normal,view,roughness);
    vec3 total=singleScattering+multiScattering;
    float remaining=1.0-max(total.x,max(total.y,total.z));
    lit+=albedo*(1.0-metal)*max(remaining,0.0)*ambient*(worldIndirectEnabled()?1.0:1.26)*environmentDiffuseOcclusion(occlusion,albedo);
    vec3 response=environmentReflectionResponse(normal,view,f0,roughness,albedo,occlusion,singleScattering);
    float specularOcclusion=clamp(pow(nv+occlusion,exp2(-16.0*roughness-1.0))-1.0+occlusion,0.0,1.0);
    lit+=(radiance*response+multiScattering*ambient)*environmentSpecularScale(.44)*specularOcclusion;
    if(!worldIndirectEnabled())lit+=diffuseWeight*albedo*ENV_AMBIENT.rgb*environmentDiffuseOcclusion(occlusion,albedo);
    vec3 emission=PROBE_EMISSION(p).rgb;
    int channel=int(round(authored.z));
    if(channel>=0 && channel<32)emission*=LOCAL_REFLECTION(20+channel).rgb;
    return vec4(clamp(environmentHaze(lit+emission,position,camera),0.0,64512.0),1.0);
}

#endif
