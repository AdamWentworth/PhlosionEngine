vec3 encodeScene(vec3 c) {
    c=clamp(c,vec3(0.0),vec3(1.0));
    return mix(1.055*pow(c,vec3(1.0/2.4))-0.055,12.92*c,lessThanEqual(c,vec3(0.0031308)));
}
vec3 historyByte(ivec2 p) {
    return floor(fetchHistory(clamp(p,ivec2(0),ivec2(control.xy)-1)).rgb*255.0+0.5);
}
vec3 sampleHistory(vec2 uv) {
    vec2 p=floor(max(uv,vec2(0))*control.xy*128.0)/128.0-0.5;
    ivec2 b=ivec2(floor(p)); vec2 f=fract(p);
    return floor(mix(mix(historyByte(b),historyByte(b+ivec2(1,0)),f.x),
                     mix(historyByte(b+ivec2(0,1)),historyByte(b+ivec2(1,1)),f.x),f.y)+0.5);
}
vec3 sceneByte(vec2 uv) {
    vec3 color=sampleScene(uv).rgb;
    if (scenePost.y>0.0) color=sceneDisplayMap(sceneBloom(uv,color,scenePost.x));
    return floor(sceneDitherDisplay(encodeScene(color),uv)*255.0+0.5);
}
vec3 composed(vec2 p) {
    vec3 dest=sceneByte(p/control.xy);
    for(int i=0;i<int(control.w);++i) {
        vec4 r=passes[i*3],uv=passes[i*3+1],option=passes[i*3+2];
        if(p.x<r.x || p.y<r.y || p.x>=r.z || p.y>=r.w) continue;
        vec2 t=(p-r.xy)/(r.zw-r.xy);
        vec3 c=option.x==2.0 ? vec3(0) : sampleHistory(mix(uv.xy,uv.zw,t));
        float a=option.y/255.0;
        if(option.x==1.0) { float m=sampleMask(t); c*=m>128.0/255.0?1.0:0.0; a*=m; }
        dest=floor(dest*(1.0-a)+c*a+0.5);
    }
    return dest;
}
vec4 feedbackOutput(vec2 uv) {
    if(control.z==0.0) return vec4(sceneByte(uv)/255.0,1);
    if(control.z==1.0) {
        vec2 b=floor(uv*control.xy);
        vec3 sum=composed(b+vec2(.25,.25))+composed(b+vec2(.75,.25))+
                 composed(b+vec2(.25,.75))+composed(b+vec2(.75,.75));
        return vec4(floor(sum*.25)/255.0,1);
    }
    return vec4(composed((floor(uv*control.xy*2.0)+.5)*.5)/255.0,1);
}
