// Distance moments for perspective local shadows. Adapters provide integer
// texel loads; both passes write RG16_UNORM, quantizing between the passes.
// edge is resolution-1; taps is an odd integer in [1,15]. Ray coordinates are
// evaluated before edge clamping, matching the captured border extension.
float shadowMomentDepth(vec2 pixel);
float shadowMomentCachedDepth(vec2 pixel);
vec2 shadowMomentIntermediate(vec2 pixel);

float shadowMomentDistance(float depth,vec2 pixel,vec2 depthCoefficients,vec4 rayCoefficients) {
    vec2 ray=pixel*rayCoefficients.xy+rayCoefficients.zw;
    float rayLength=sqrt((ray.y*ray.y+1.0)+ray.x*ray.x);
    return (1.0/(depth*depthCoefficients.y+depthCoefficients.x))*rayLength;
}

vec2 shadowMomentHorizontal(vec2 pixel,float edge,int taps,vec2 depthCoefficients,vec4 rayCoefficients) {
    vec2 sum=vec2(0.0,0.0);
    for(int i=0;i<taps;++i) {
        vec2 samplePixel=pixel+vec2(float(i-(taps-1)/2),0.0);
        vec2 bounded=clamp(samplePixel,vec2(0.0,0.0),vec2(edge,edge));
        float radial=shadowMomentDistance(shadowMomentDepth(bounded),samplePixel,depthCoefficients,rayCoefficients);
        float nearest=min(radial,shadowMomentCachedDepth(bounded));
        sum+=vec2(nearest,nearest*nearest);
    }
    return sum/float(taps);
}

vec2 shadowMomentVertical(vec2 pixel,float edge,int taps) {
    vec2 sum=vec2(0.0,0.0);
    for(int i=0;i<taps;++i) {
        vec2 samplePixel=clamp(pixel+vec2(0.0,float(i-(taps-1)/2)),vec2(0.0,0.0),vec2(edge,edge));
        sum+=shadowMomentIntermediate(samplePixel);
    }
    return sum/float(taps);
}
