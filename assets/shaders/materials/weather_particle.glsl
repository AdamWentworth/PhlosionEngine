// Non-emissive animated smoke. Texture/depth/light bindings are supplied by
// the material family; the same body is compiled for both enrolled APIs.
vec2 weatherParticleUv(vec2 uv,float frame,vec2 grid) {
    return (vec2(floor(frame),-floor(frame/grid.x))+uv)/grid;
}
vec4 weatherParticleColor(vec2 uv,float frame,float alpha,float lookDot,
        float rawDepth,vec2 pixel,vec2 lightUv,vec3 world,
        vec4 shape,vec4 dark,vec4 light,vec4 maskMotion,vec4 response,vec4 projection,float seconds) {
    float sceneDepth=WP_DEPTH(pixel);
    float separation;
    if(projection.z>0.5)separation=(sceneDepth-rawDepth)*projection.w;
    else separation=projection.y/(sceneDepth-projection.x)-projection.y/(rawDepth-projection.x);
    float softness=dark.w==0.0?1.0:clamp(separation*abs(dark.w),0.0,1.0);
    if(dark.w<0.0)softness*=1.0-softness;
    if(softness<.0009)discard;
    float first=frame+shape.x;
    vec4 a=WP_TEXTURE(weatherParticleUv(uv,first,shape.xy));
    vec4 b=WP_TEXTURE(weatherParticleUv(uv,first+1.0,shape.xy));
    vec4 technical=mix(a,b,fract(first+1.0));
    float grain=WP_MASK(uv*maskMotion.xy+seconds*maskMotion.zw);
    grain=mix(grain,1.0-grain,response.x)*light.w;
    float coverage=mix(clamp(shape.z*(technical.a-1.0+alpha),0.0,1.0),
                       technical.a*alpha*shape.z,floor(response.w));
    coverage=clamp(pow(abs(coverage),shape.w),0.0,1.0);
    float facing=lookDot*response.y;
    coverage=clamp(coverage*grain*(1.0-facing*facing)*softness,0.0,1.0);
    if(coverage<.001)discard;
    coverage-=.001;
    vec3 radiance=mix(dark.rgb,light.rgb,technical.b);
    radiance*=mix(WP_LIGHT(lightUv),vec3(1.0,1.0,1.0),response.z);
    vec4 fog=worldFogAt(world);
    float opacity=clamp(1.0-fog.a,0.0,1.0);
    float blend=mix(1.0,clamp(dot(radiance,vec3(1.0/3.0,1.0/3.0,1.0/3.0))/max(fog.a,1e-8),0.0,1.0),opacity);
    return vec4(mix(radiance,fog.rgb+fog.a*radiance,blend)*coverage,blend*coverage);
}
