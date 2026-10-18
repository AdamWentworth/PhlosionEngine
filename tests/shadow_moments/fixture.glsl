// Public synthetic fields exercise flat, stepped, sloped and cached occlusion.
#ifndef FIXTURE_GLOBAL
#define FIXTURE_GLOBAL
#endif
FIXTURE_GLOBAL uint shadowCase;
float shadowMomentDepth(vec2 pixel) {
    uint kind=shadowCase%8u;
    if(kind==0u) return .6;
    if(kind==1u) return pixel.x<8.0?.2:.8;
    if(kind==2u) return pixel.y<8.0?.2:.8;
    if(kind==3u) return .1+.025*pixel.x+.015*pixel.y;
    if(kind==4u) return 0.0;
    if(kind==5u) return 1.0;
    if(kind==6u) return pixel.x==8.0?.95:.05;
    return (uint(pixel.x+pixel.y)%2u)==0u?.1:.9;
}
float shadowMomentCachedDepth(vec2 pixel) {
    return shadowCase%8u==4u && pixel.x<8.0?.18:1.0;
}
vec2 shadowMomentIntermediate(vec2 pixel) {
    int taps=shadowCase<8u?1:shadowCase<16u?3:shadowCase<24u?7:15;
    vec2 moments=shadowMomentHorizontal(pixel,15.0,taps,vec2(1.1,7.0),vec4(.12,-.10,-.84,.70));
    // The public fixture models the actual intermediate RG16_UNORM storage.
    return floor(clamp(moments,vec2(0.0,0.0),vec2(1.0,1.0))*65535.0+vec2(.5,.5))/65535.0;
}
void computeFixture(uint index,out vec4 a,out vec4 b) {
    shadowCase=index;
    int taps=index<8u?1:index<16u?3:index<24u?7:15;
    vec2 pixel=index%4u==0u?vec2(0,0):index%4u==1u?vec2(15,15):vec2(8,8);
    vec2 horizontal=shadowMomentHorizontal(pixel,15.0,taps,vec2(1.1,7.0),vec4(.12,-.10,-.84,.70));
    vec2 vertical=shadowMomentVertical(pixel,15.0,taps);
    a=vec4(horizontal,vertical);
    b=vec4(shadowMomentDistance(.6,pixel,vec2(1.1,7.0),vec4(.12,-.10,-.84,.70)),
        finiteLightMomentVisibility(vertical,.1,1.0),finiteLightMomentVisibility(vertical,.5,1.0),
        finiteLightMomentVisibility(vertical,.8,.25));
}
