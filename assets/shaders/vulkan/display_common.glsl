#if DISPLAY_STAGE == 9
vec3 heatPosition(vec2 pixel) {
    float depth=LOAD1(ivec2(pixel)).r;
    vec4 clip=vec4(pixel.x*2.0/p7.x-1.0,1.0-pixel.y*2.0/p7.y,depth*2.0-1.0,1.0);
    vec4 v=vec4(dot(p0,clip),dot(p1,clip),dot(p2,clip),dot(p3,clip));
    return v.xyz/v.w;
}
float heatNoise(vec3 seed) {
    vec2 xy=(seed.xy-floor(seed.xy))*p6.yz-.5;
    ivec2 cell=ivec2(floor(xy)),size=ivec2(p6.yz);
    vec2 f=xy-floor(xy);
    int layer=int(clamp(round(seed.z),0.0,p6.w-1.0));
    ivec2 lo=(cell+size)%size,hi=(cell+ivec2(1,1)+size)%size;
    lo.y+=layer*size.y;hi.y+=layer*size.y;
    return mix(mix(LOAD2(lo).r,LOAD2(ivec2(hi.x,lo.y)).r,f.x),
               mix(LOAD2(ivec2(lo.x,hi.y)).r,LOAD2(hi).r,f.x),f.y)-.5;
}
vec4 heatColor(ivec2 q) {
    vec2 pixel=vec2(q)+.5;
    vec3 position=heatPosition(pixel);
    float distance=length(position-p4.xyz);
    float amplitude=clamp(p5.x*(distance-p5.y)/(p5.z-p5.y),0.0,1.0);
    if(amplitude<=0.0)return LOAD0(q);
    vec3 seed=vec3(5.0,5.0,1.2)*position*pow(clamp(distance,100.0,6000.0),-.6);
    seed.z-=p6.x*p4.w;seed.xy+=.1*p6.x*p4.w;seed*=p5.w;
    vec2 target=clamp(pixel+amplitude*vec2(heatNoise(seed),heatNoise(-seed)),vec2(0,0),p7.xy-1.0);
    float other=length(heatPosition(target)-p4.xyz);
    if(other<=distance&&other<=mix(p5.y,p5.z,.3))return LOAD0(q);
    return SAMPLE0(target/p7.xy);
}
#endif
vec3 displayRoundCompact(vec3 v) {
    return vec3(BITS_FLOAT(FLOAT_BITS(v.r)+65536u),BITS_FLOAT(FLOAT_BITS(v.g)+65536u),BITS_FLOAT(FLOAT_BITS(v.b)+131072u));
}
// Shared display stages. Input history remains scene-linear and unmodified.
float displayLuma(vec3 v){return dot(v,vec3(.2126,.7152,.0722));}
float displayPreviousExposure() {return p1.y>.5?max(LOAD3(ivec2(0,0)).y,1e-8):1.;}
vec3 displayLut(vec3 color) {
    vec3 v=clamp((log2(max(color,vec3(.001,.001,.001)))-log2(.001))/(log2(100.)-log2(.001)),0.,1.)*47.;
    ivec2 rg=ivec2(floor(v.xy));int b=int(floor(v.z));vec3 f=v-vec3(float(rg.x),float(rg.y),float(b));
    vec3 result=vec3(0.,0.,0.);
    for(int z=0;z<2;++z)for(int y=0;y<2;++y)for(int x=0;x<2;++x){
        ivec2 q=ivec2(min(rg.x+x,47)+min(b+z,47)*48,min(rg.y+y,47));
        result+=LOAD4(q).rgb*(x==0?1.-f.x:f.x)*(y==0?1.-f.y:f.y)*(z==0?1.-f.z:f.z);
    }
    return result;
}
#if DISPLAY_STAGE == 1
SHARED uint displayBins[512];
void displayHistogram(uint lane,uint group) {
    displayBins[lane]=0u;displayBins[lane+256u]=0u;SYNC;
    for(uint k=0u;k<4u;++k){
        uint n=group*1024u+lane+k*256u;if(n<4u)continue;
        float luma=LOAD0(ivec2(int(n%256u),int(n/256u))).r;
        float bin=clamp((log2(max(luma,1e-30))+8.)*511./28.,0.,511.);
        uint low=min(uint(bin),510u),high=uint(65536.*(bin-float(low)));
        ATOMIC(displayBins[low],65536u-high);ATOMIC(displayBins[low+1u],high);
    }
    SYNC;
    uint lo=displayBins[lane],hi=displayBins[lane+256u];
    STORE(ivec2(int(lane),int(group)),vec4(float(lo&65535u),float(lo>>16u),0.,0.));
    STORE(ivec2(int(lane+256u),int(group)),vec4(float(hi&65535u),float(hi>>16u),0.,0.));
}
#else
void displayStage(ivec2 q) {
#if DISPLAY_STAGE == 9
    if(q.x>=int(p7.x)||q.y>=int(p7.y))return;
    STORE(q,heatColor(q));
#elif DISPLAY_STAGE == 0
    if(q.x>=256||q.y>=256)return;
    vec2 uv=vec2(q)/256.+vec2(.5,.5)/p0.zw,center=uv*2.-1.;
    // Signed authored weighting is valid; the exact centre has no direction.
    float radius=length(center);
    if(radius>0.)center+=center*pow(radius,p4.x);
    uv=clamp(center*.25/sqrt(pow(sqrt(2.),p4.x))+.5,0.,1.);
    vec3 sum=vec3(0.,0.,0.);
    for(int k=0;k<3;++k){
        vec2 coord=uv+vec2(float(k-1)*2.,float(k-1))/p0.zw;
        float depth=LOAD2(clamp(ivec2(coord*p0.zw),ivec2(0,0),ivec2(p0.zw)-ivec2(1,1))).r;
        float emissive=displayLuma(SAMPLE1(coord).rgb);
        sum+=max(SAMPLE0(coord).rgb,vec3(0.,0.,0.))*
            mix(1.,p4.y,clamp(emissive*50.,0.,1.))*(depth>=.999999?p3.w:1.);
    }
    float luma=displayLuma(sum)/(3.*p1.z);STORE(q,vec4(luma,0.,0.,1.));
#elif DISPLAY_STAGE == 2
    if(q.x!=0||q.y!=0)return;
    float lower=p2.x*65532.,upper=p2.y*65532.,weighted=0.,count=0.;
    for(int bin=0;bin<512;++bin){
        vec2 halves=LOAD0(ivec2(bin,0)).rg;uint total=uint(halves.x)+(uint(halves.y)<<16u);
        float v=float(total)/65536.,removed=min(v,lower);v-=removed;lower-=removed;upper-=removed;
        v=min(v,upper);upper-=v;weighted+=(float(bin)+.5)*v;count+=v;
    }
    float measured=clamp(exp2((weighted/max(count,1e-6))*28./511.-8.),p2.z,p2.w);
    float old=p1.y>.5?LOAD3(ivec2(0,0)).x:measured;
    float speed=measured>old?p3.y:p3.x;
    float adapted=mix(old,measured,(1.-exp(-p1.x*speed))*p3.z);
    float exposure=mix(1./max(9.6*adapted,1e-30),p4.w,p4.z)*p1.w;
    STORE(q,vec4(adapted,exposure,measured,count));
#elif DISPLAY_STAGE == 8
    if(q.x>=512||q.y!=0)return;
    uint total=0u;
    for(int row=0;row<64;++row){vec2 halves=LOAD0(ivec2(q.x,row)).rg;total+=uint(halves.x)+(uint(halves.y)<<16u);}
    STORE(q,vec4(float(total&65535u),float(total>>16u),0.,0.));
#elif DISPLAY_STAGE == 3 || DISPLAY_STAGE == 4
    if(q.x>=int(p0.x)||q.y>=int(p0.y))return;
    vec2 uv=(vec2(q)+.5)/p0.xy;vec3 value=vec3(0.,0.,0.);
    const vec2 offsets[9]=vec2[9](vec2(-1.7,1.7),vec2(0.,1.75),vec2(1.7,1.7),vec2(-1.75,0.),vec2(0.,0.),vec2(1.75,0.),vec2(-1.7,-1.7),vec2(0.,-1.75),vec2(1.7,-1.7));
    for(int k=0;k<9;++k){
        vec3 c=max(SAMPLE0(uv+offsets[k]/p0.zw).rgb,vec3(0.,0.,0.));
#if DISPLAY_STAGE == 3
        c*=displayPreviousExposure()/p1.z;
        c*=p5.x>=p5.y?1.:clamp((displayLuma(c)-p5.x)/(p5.y-p5.x),0.,1.);
#endif
        value+=c;
    }
    STORE(q,vec4(displayRoundCompact(clamp(value/9.,0.,100.)),1.));
#elif DISPLAY_STAGE == 5
    if(q.x>=int(p0.x)||q.y>=int(p0.y))return;
    vec2 uv=(vec2(q)+.5)/p0.xy;vec3 value=vec3(0.,0.,0.);
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x)
        value+=SAMPLE0(uv+vec2(float(x),float(y))/p0.zw).rgb*float((x==0?2:1)*(y==0?2:1))/16.;
    STORE(q,vec4(displayRoundCompact(value)+SAMPLE1(uv).rgb,1.));
#elif DISPLAY_STAGE == 6
    if(q.x>=2304||q.y>=48)return;
    vec3 domain=vec3(float(q.x%48),float(q.y),float(q.x/48))/47.;
    vec3 color=exp2(domain*(log2(100.)-log2(.001))+log2(.001));
    STORE(q,vec4(displayAces(displayGrade(color)),1.));
#elif DISPLAY_STAGE == 7
    if(q.x>=int(p0.x)||q.y>=int(p0.y))return;
    // History remains in the actual jittered camera's coordinates. Present it
    // on the stable output grid, including bloom, without shifting UI overlays.
    vec2 uv=(vec2(q)+.5+p6.xy)/p0.xy;
    vec3 color=SAMPLE0(uv).rgb*(p5.z/p1.z)+SAMPLE1(uv).rgb*(p5.w/displayPreviousExposure());
    color*=LOAD2(ivec2(0,0)).y;
    STORE(q,vec4(displayLut(max(color,vec3(0.,0.,0.))),1.));
#endif
}
#endif
