// Display-linear bloom for the existing tone-mapped scene-color pipeline.
// This is an optical halo, not an HDR lighting buffer or GI.
float sceneBloomWeight(int offset) {
    int i=abs(offset);
    return i==0 ? 1.0 : i==1 ? .882497 : i==2 ? .606531 : i==3 ? .324652 : .135335;
}
vec3 sceneBloomBright(vec3 c) {
    return c*smoothstep(.60,.95,max(c.r,max(c.g,c.b)));
}
vec3 sceneBloom(vec2 uv, vec3 center, float strength) {
    if (strength<=0.0) return center;
    vec2 size=SCENE_SIZE;
    vec2 stepUv=vec2(1.0,1.0)*max(size.y/720.0,.75)/size;
    vec3 blurred=vec3(0.0,0.0,0.0);
    float total=0.0;
    for(int y=-4;y<=4;++y) for(int x=-4;x<=4;++x) {
        float weight=sceneBloomWeight(x)*sceneBloomWeight(y);
        vec2 sampleUv=clamp(uv+vec2(float(x),float(y))*stepUv,vec2(.5,.5)/size,vec2(1.0,1.0)-vec2(.5,.5)/size);
        blurred+=sceneBloomBright(SCENE_SAMPLE(sampleUv).rgb)*weight;
        total+=weight;
    }
    // Add local spread without dimming clipped light cores or veiling uniform sky.
    return center+strength*max(blurred/total-sceneBloomBright(center),vec3(0.0,0.0,0.0));
}
