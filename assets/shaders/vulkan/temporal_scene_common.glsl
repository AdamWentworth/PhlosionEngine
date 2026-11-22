// Independent temporal reconstruction; API adapters define vector and image IO.
// p0: output extent / input extent. p1: history-valid, current weight,
// exposure ratio, stage. p2: full extent/current linear-depth coefficients.
// p3: current/previous camera projection kind and previous depth coefficients.
// p4-p7: current-to-previous sky clip transform, with camera translation removed.
ivec2 boundCoord(ivec2 p,vec2 size) {return clamp(p,ivec2(0,0),ivec2(size)-ivec2(1,1));}
vec3 toTemporalColor(vec3 c) {return vec3(dot(c,vec3(.25,.5,.25)),.5*(c.r-c.b),.5*c.g-.25*(c.r+c.b));}
vec3 fromTemporalColor(vec3 c) {return vec3(c.x+c.y-c.z,c.x+c.z,c.x-c.y-c.z);}
vec4 historyCubic(vec2 uv) {
    // Five bilinear taps of a Catmull-Rom reconstruction, omitting corner taps.
    vec2 position=uv*p2.xy-.5,base=floor(position),f=position-base;
    vec2 w0=f*(-.5+f*(1.-.5*f)),w1=1.+f*f*(-2.5+1.5*f);
    vec2 w2=f*(.5+f*(2.-1.5*f)),w3=f*f*(-.5+.5*f),w12=w1+w2;
    vec2 c=(base+.5+w2/w12)/p2.xy,a=(base-.5)/p2.xy,b=(base+2.5)/p2.xy;
    float weight=w12.x*(w0.y+w12.y+w3.y)+w12.y*(w0.x+w3.x);
    return (SAMPLE3(vec2(c.x,a.y))*w12.x*w0.y+SAMPLE3(vec2(a.x,c.y))*w0.x*w12.y+
        SAMPLE3(c)*w12.x*w12.y+SAMPLE3(vec2(b.x,c.y))*w3.x*w12.y+
        SAMPLE3(vec2(c.x,b.y))*w12.x*w3.y)/weight;
}
float temporalLinearDepth(float d,vec2 coefficients,float kind) {
    return kind>1.5?max(dot(vec2(d,1),coefficients),.0001):
        1./max(dot(vec2(d,1),coefficients),.000001);
}
bool temporalSky(ivec2 pixel) {return p3.x>.5 && ID8(pixel)==0u && LOAD2(pixel).x>=1.;}
vec4 temporalSkyMotion(ivec2 pixel) {
    vec2 uv=(vec2(pixel)+.5)/p2.xy;
    vec4 clip=p4*(2.*uv.x-1.)+p5*(1.-2.*uv.y)+p6+p7;
    if(p3.y<.5||clip.w<=.000001)return vec4(0,0,1,0);
    vec2 previous=vec2(clip.x,-clip.y)/clip.w*.5+.5;
    return vec4(previous-uv,1,1);
}
void temporalScene(ivec2 pixel) {
    if(pixel.x>=int(p0.x)||pixel.y>=int(p0.y))return;
    int stage=int(p1.w);
    if(stage==0) {
        // Nearest of each 2x2 footprint, including odd-sized edges. Preserve the
        // exact uint identity rather than storing it in a half-float channel.
        ivec2 lo=ivec2(floor(vec2(pixel)*p0.zw/p0.xy));
        ivec2 hi=ivec2(ceil(vec2(pixel+ivec2(1,1))*p0.zw/p0.xy));
        float nearest=2.;ivec2 best=lo;
        for(int y=0;y<3;++y)for(int x=0;x<3;++x) {
            ivec2 q=lo+ivec2(x,y);
            if(q.x>=hi.x||q.y>=hi.y)continue;
            q=boundCoord(q,p0.zw);float d=LOAD2(q).x;
            if(d<nearest){nearest=d;best=q;}
        }
        vec4 motion=LOAD1(best);uint identity=ID8(best),category=CLASS8(best);
        if(temporalSky(best)){motion=temporalSkyMotion(best);identity=0xffffffffu;category=4u;}
        STORE(pixel,vec4(motion.xy,nearest,motion.w>.5?motion.z:-1.));
        STORE_ID(pixel,identity,category);return;
    }
    if(stage==1) {
        // Nearest-depth velocity dilation on the half-size field.
        ivec2 best=pixel;float nearest=2.;
        for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x) {
            ivec2 q=boundCoord(pixel+ivec2(x,y),p0.xy);
            float d=LOAD4(q).z;if(d<nearest){nearest=d;best=q;}
        }
        vec4 motion=LOAD4(best);
        vec2 uv=(vec2(pixel)+.5)/p0.xy+motion.xy;
        ivec2 old=boundCoord(ivec2(uv*p0.xy),p0.xy);
        uint currentID=ID8(pixel),previousID=ID9(old);
        float previousDepth=LOAD5(old).z;
        uint category=CLASS8(pixel),oldCategory=CLASS9(old);
        float invalid=(p1.x<.5||motion.w<0.||currentID==0u||previousID==0u||
            uv.x<0.||uv.y<0.||uv.x>=1.||uv.y>=1.)?1.:0.;
        if(p3.x>.5 && p3.y>.5) {
            // Ordinary class/depth heuristic: the current local depth range
            // tolerates a sloping surface, while nearby vacated actors reject more.
            if(category==4u || oldCategory==4u)invalid=max(invalid,category==oldCategory?0.:1.);
            else {
                float nearestLinear=1e20,farthestLinear=0.;
                for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x) {
                    float d=temporalLinearDepth(LOAD4(boundCoord(pixel+ivec2(x,y),p0.xy)).z,p2.zw,p3.x);
                    nearestLinear=min(nearestLinear,d);farthestLinear=max(farthestLinear,d);
                }
                float oldLinear=temporalLinearDepth(previousDepth,p3.zw,p3.y);
                float lo=min(farthestLinear,oldLinear),hi=max(nearestLinear,oldLinear);
                float strength=clamp(1.5-min(farthestLinear,oldLinear)/30.,0.,1.);
                if(category!=oldCategory && (oldCategory==1u||oldCategory==2u))strength*=4.;
                invalid+=strength*clamp(abs(hi-lo)/max(max(hi,lo),.0001),0.,1.);
            }
        } else {
            // Camera-less callers still use exact previous projected depth/ID.
            float tolerance=max(.000002,abs(motion.w)*.0005);
            if(currentID!=previousID || abs(previousDepth-motion.w)>tolerance)invalid=1.;
        }
        invalid=clamp(invalid,0.,1.);
        STORE(pixel,vec4(motion.xy,invalid,0));return;
    }
    if(stage==2) {
        float invalid=0.;
        for(int y=-1;y<=2;++y)for(int x=-1;x<=2;++x)
            invalid=max(invalid,LOAD6(boundCoord(pixel+ivec2(x,y),p0.xy)).z);
        STORE(pixel,vec4(LOAD6(pixel).xy,invalid,0));return;
    }
    vec2 uv=(vec2(pixel)+.5)/p0.xy;
    if(stage==3) {
        vec4 now=LOAD0(pixel),motion=SAMPLE6(uv);
        vec2 previousUV=uv+motion.xy;
        float invalid=motion.z;
        // Reject uncovered/missing producers and both current and previous
        // transparency. alpha records reactive coverage, not output opacity.
        vec4 rawMotion=LOAD1(pixel);
        bool sky=temporalSky(pixel);
        if(p1.x<.5||(!sky&&(rawMotion.w<.5||ID8(pixel)==0u))||
           previousUV.x<0.||previousUV.y<0.||previousUV.x>=1.||previousUV.y>=1.)invalid=1.;
        vec3 crossMin=toTemporalColor(now.rgb),crossMax=crossMin;
        vec3 boxMin=crossMin,boxMax=crossMax;
        for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x) {
            vec3 c=toTemporalColor(LOAD0(boundCoord(pixel+ivec2(x,y),p0.xy)).rgb);
            boxMin=min(boxMin,c);boxMax=max(boxMax,c);
            if(x==0||y==0){crossMin=min(crossMin,c);crossMax=max(crossMax,c);}
        }
        vec3 low=.5*(crossMin+boxMin),high=.5*(crossMax+boxMax);
        vec4 old=historyCubic(previousUV);
        vec3 history=clamp(toTemporalColor(max(old.rgb*p1.z,vec3(0,0,0))),low,high);
        vec3 current=toTemporalColor(max(now.rgb,vec3(0,0,0)));
        float edges=max(high.x-low.x,0.);
        float distanceToEdge=max(min(abs(history.x-high.x),abs(history.x-low.x)),0.);
        float motionPixels=min(.5,length(motion.xy*p0.xy));
        float rejection=distanceToEdge*(1.+motionPixels)*.25;
        float weight=max(p1.y,clamp(invalid+rejection/max(rejection+edges,1e-6),0.,1.));
        float reactive=max(clamp(now.a*2.,0.,1.),REACTIVE8(pixel));
        weight=max(weight,max(reactive,clamp(SAMPLE3(previousUV).a,0.,1.)));
        if(CLASS8(pixel)==3u)weight=min(weight+.5,1.);
        current/=1.+current.x;history/=1.+history.x;
        vec3 result=mix(history,current,weight);
        result/=max(1.-result.x,1e-5);
        STORE(pixel,vec4(min(max(fromTemporalColor(result),vec3(0,0,0)),vec3(65000,65000,65000)),reactive));return;
    }
    // Reference feedback filtering: a five-tap diagonal/center filter starts
    // each four-level group; intermediate levels reduce 2x2 in the compressed
    // color domain. Separate dispatches preserve the math without LDS coupling.
    vec3 sum=vec3(0,0,0);float count=0.;
    if(stage==4) {
        for(int tap=0;tap<5;++tap) {
            vec2 offset=tap==0?vec2(0,0):vec2((tap==1||tap==2)?-1.:1.,(tap==1||tap==3)?-1.:1.);
            vec3 sampleColor=SAMPLE0(uv+offset/p0.zw).rgb;
            sum+=sampleColor/max(1.+sampleColor.r,1.);count+=1.;
        }
    } else {
        for(int y=0;y<2;++y)for(int x=0;x<2;++x) {
            vec3 sampleColor=LOAD0(boundCoord(pixel*2+ivec2(x,y),p0.zw)).rgb;
            sum+=sampleColor/max(1.+sampleColor.r,1.);count+=1.;
        }
    }
    vec3 reduced=clamp(sum/count,vec3(0,0,0),vec3(1,1,1));
    STORE(pixel,vec4(min(reduced/max(1.-reduced.r,1e-5),vec3(65000,65000,65000)),1));
}
