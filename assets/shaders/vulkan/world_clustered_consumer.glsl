
bool worldClusteredDirectWeighted(vec3 position,vec3 diffuseFactor,float specularFactor,out vec3 result) {
    result=vec3(0.0,0.0,0.0);
#ifdef CLUSTER_VIEW
    if(CLUSTER_VIEW(4).w<.5)return false;
    vec4 p=vec4(position,1.0);
    vec4 clip=vec4(dot(CLUSTER_VIEW(0),p),dot(CLUSTER_VIEW(1),p),dot(CLUSTER_VIEW(2),p),dot(CLUSTER_VIEW(3),p));
    if(clip.w<=0.0)return false;
    vec3 ndc=clip.xyz/clip.w;
    vec2 uv=ndc.xy*vec2(.5,-.5)+vec2(.5,.5);
    if(min(uv.x,uv.y)<0.0||max(uv.x,uv.y)>=1.0)return false;
    ivec2 pixel=ivec2(uv*CLUSTER_VIEW(4).xy);
    vec4 diffuse=CLUSTER_DIFFUSE(pixel);
    if(diffuse.a<.5)return false;
    // Reject overdraw, transparent additions and a different scene/view.
    float depth=ndc.z*.5+.5;
    if(abs(CLUSTER_DEPTH(pixel)-depth)>0.000002)return false;
    result=diffuse.rgb*diffuseFactor+CLUSTER_SPECULAR(pixel).rgb*specularFactor;
    return true;
#else
    return false;
#endif
}


bool worldClusteredDirect(vec3 position,out vec3 result) {
    return worldClusteredDirectWeighted(position,vec3(1.0,1.0,1.0),1.0,result);
}

#ifndef CLUSTER_FORWARD_FALLBACK
#define CLUSTER_FORWARD_FALLBACK true
#endif
bool worldStoredSurface(vec3 position, out WorldSurfaceMaterial surface, out vec3 emission) {
    emission=vec3(0.0,0.0,0.0);
#if defined(STORED_ALBEDO) && !defined(PHLOSION_WORLD_SURFACE_CAPTURE)
    if(!CLUSTER_FORWARD_FALLBACK) {
        vec4 p=vec4(position,1.0);
        vec4 clip=vec4(dot(CLUSTER_VIEW(0),p),dot(CLUSTER_VIEW(1),p),dot(CLUSTER_VIEW(2),p),dot(CLUSTER_VIEW(3),p));
        vec3 ndc=clip.xyz/clip.w;
        vec2 uv=ndc.xy*vec2(.5,-.5)+vec2(.5,.5);
        if(clip.w<=0.0 || min(uv.x,uv.y)<0.0 || max(uv.x,uv.y)>=1.0)discard;
        ivec2 pixel=ivec2(uv*CLUSTER_VIEW(4).xy);
        if(abs(CLUSTER_DEPTH(pixel)-(ndc.z*.5+.5))>.000002)discard;
        WorldSurfaceBuffer data;
        data.albedo=STORED_ALBEDO(pixel);data.normalClass=STORED_NORMAL(pixel);
        data.material=STORED_MATERIAL(pixel);data.emission=STORED_EMISSION(pixel);
        // Mask holes and specialized/motion-only pixels remain outside this path.
        if((data.albedo.a>.16 && data.albedo.a<.5) || data.normalClass.a<.16 || data.normalClass.a>.84)discard;
        surface=unpackWorldSurface(data);emission=data.emission.rgb;return true;
    }
#endif
    // A fully initialized value keeps general/compute shader compilers happy.
    WorldSurfaceBuffer emptyData;
    emptyData.albedo=vec4(0.0,0.0,0.0,0.0);emptyData.normalClass=vec4(.5,1.0,.5,0.0);
    emptyData.material=vec4(0.0,.04,1.0,1.0);emptyData.emission=vec4(0.0,0.0,0.0,0.0);
    surface=unpackWorldSurface(emptyData);
    return false;
}

#if defined(PHLOSION_WORLD_SURFACE_COMPOSITION) && defined(STORED_ALBEDO)
WorldSurfaceMaterial loadWorldCompositionSurface(vec2 pixel, out vec3 emission, out vec3 position,out vec2 foliage) {
    ivec2 p=ivec2(pixel);
    WorldSurfaceBuffer data;
    data.albedo=STORED_ALBEDO(p);data.normalClass=STORED_NORMAL(p);
    if(data.albedo.a<.5 || data.normalClass.a<.16 || data.normalClass.a>.84)discard;
    float depth=CLUSTER_DEPTH(p);
    if(depth>=.999999)discard;
    data.material=STORED_MATERIAL(p);data.emission=STORED_EMISSION(p);
    vec2 uv=pixel/CLUSTER_VIEW(4).xy;
    vec4 clip=vec4(uv.x*2.0-1.0,1.0-uv.y*2.0,depth*2.0-1.0,1.0);
    vec4 h=vec4(dot(CONTACT(4),clip),dot(CONTACT(5),clip),dot(CONTACT(6),clip),dot(CONTACT(7),clip));
    if(abs(h.w)<1e-8)discard;
    position=h.xyz/h.w;emission=data.emission.rgb;
    foliage=vec2(data.albedo.a>.84?data.material.x:-1.0,data.material.z);
    return unpackWorldSurface(data);
}
#endif
