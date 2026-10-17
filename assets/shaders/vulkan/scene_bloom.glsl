// One post-composition boundary for legacy display-linear and scene radiance.
// Optical spread does not add illumination to scene geometry.
#ifndef SCENE_EXPOSURE
#define SCENE_EXPOSURE 0.0
#endif
vec3 sceneDitherDisplay(vec3 encoded,vec2 uv) {
    if (SCENE_EXPOSURE<=0.0) return encoded;
    vec2 size=SCENE_SIZE;
    vec2 pixel=clamp(floor(uv*size),vec2(0.0,0.0),size-vec2(1.0,1.0));
    // Mirror Y so framebuffer origin conventions produce the same display noise.
    pixel.y=min(pixel.y,size.y-1.0-pixel.y);
    uint seed=uint(pixel.x)+uint(pixel.y)*16411u;
    seed^=seed>>16u;seed*=2146121005u;seed^=seed>>15u;seed*=2221713035u;seed^=seed>>16u;
    float noise=(float(seed&65535u)/65535.0-.5)/255.0;
    return clamp(encoded+vec3(noise,noise,noise),0.0,1.0);
}
vec3 sceneDisplayMap(vec3 color) {
    if (SCENE_EXPOSURE <= 0.0) return color;
    // Same ACES fit as the material path, expressed with dots for GLSL/HLSL.
    color = vec3(dot(color,vec3(.59719,.35458,.04823)),
                 dot(color,vec3(.07600,.90834,.01566)),
                 dot(color,vec3(.02840,.13383,.83777)));
    color = (color*(color+.0245786)-.000090537) /
            (color*(.983729*color+.4329510)+.238081);
    return clamp(vec3(dot(color,vec3(1.60475,-.53108,-.07367)),
                      dot(color,vec3(-.10208,1.10813,-.00605)),
                      dot(color,vec3(-.00327,-.07276,1.07602))),0.0,1.0);
}
vec3 sceneExposed(vec3 color) {
    return SCENE_EXPOSURE > 0.0 ? max(color,vec3(0.0,0.0,0.0))*SCENE_EXPOSURE/.6 : color;
}
float sceneBloomWeight(int offset) {
    int i=abs(offset);
    return i==0 ? 1.0 : i==1 ? .882497 : i==2 ? .606531 : i==3 ? .324652 : .135335;
}
vec3 sceneBloomBright(vec3 c) {
    float peak=max(c.r,max(c.g,c.b));
    return c*(SCENE_EXPOSURE > 0.0 ? smoothstep(.8,1.2,peak) : smoothstep(.60,.95,peak));
}
vec3 sceneBloom(vec2 uv, vec3 center, float strength) {
    center=sceneExposed(center);
    if (strength<=0.0) return center;
    vec2 size=SCENE_SIZE;
    vec2 stepUv=vec2(1.0,1.0)*max(size.y/720.0,.75)/size;
    vec3 blurred=vec3(0.0,0.0,0.0);
    float total=0.0;
    for(int y=-4;y<=4;++y) for(int x=-4;x<=4;++x) {
        float weight=sceneBloomWeight(x)*sceneBloomWeight(y);
        vec2 sampleUv=clamp(uv+vec2(float(x),float(y))*stepUv,vec2(.5,.5)/size,vec2(1.0,1.0)-vec2(.5,.5)/size);
        blurred+=sceneBloomBright(sceneExposed(SCENE_SAMPLE(sampleUv).rgb))*weight;
        total+=weight;
    }
    // Add local spread without dimming clipped light cores or veiling uniform sky.
    return center+strength*max(blurred/total-sceneBloomBright(center),vec3(0.0,0.0,0.0));
}
