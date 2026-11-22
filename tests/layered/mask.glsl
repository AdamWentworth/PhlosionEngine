void computeFixture(uint i,out vec4 first,out vec4 second) {
    uint packed=(i%4u)|(((i/4u)%4u)<<10u)|((i%3u)<<20u)|(((i/3u)%3u)<<24u);
    vec2 uv=vec2((float(i)+.5)/32.0,1.0-(float(i%8u)+.5)/8.0);
    if(i==30u)uv=vec2(0.0,0.0);
    if(i==31u)uv=vec2(1.0,1.0);
    vec2 p=worldLayerMaskAtlasUv(packed,uv,vec2(512.0,256.0),32.0,vec2(256.0,512.0),vec2(34.0/256.0,34.0/512.0));
    first=vec4(p,0.0,0.0);second=vec4(0.0,0.0,0.0,0.0);
}
