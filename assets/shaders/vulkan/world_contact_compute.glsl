// Five compact, per-view ConeAO passes. Native depth is conventional [0,1].
// Contact flags: foliage=1, suppress detailed AO=2, invalid motion=4.
vec2 cHalf(){return floor((CP(8).xy+1.0)*.5);}
bool cInside(ivec2 q,vec2 size){return q.x>=0&&q.y>=0&&q.x<int(size.x)&&q.y<int(size.y);}
ivec2 cClamp(ivec2 q,vec2 size){return clamp(q,ivec2(0,0),ivec2(size)-ivec2(1,1));}
vec3 cUnit(vec3 v,vec3 fallback){return dot(v,v)>1e-12?normalize(v):fallback;}
vec4 cTransform(vec4 v,int row){
    return vec4(dot(CP(row),v),dot(CP(row+1),v),dot(CP(row+2),v),dot(CP(row+3),v));
}
vec3 cPosition(vec2 uv,float depth,int row){
    vec4 h=cTransform(vec4(uv.x*2.0-1.0,1.0-uv.y*2.0,depth*2.0-1.0,1.0),row);
    return h.xyz/h.w;
}
float cLinear(vec2 uv,float depth){
    return 1.0/((depth*2.0-1.0)*CP(7).z+CP(7).w);
}
vec2 cProjectionOffset(){return vec2(dot(CP(0).xyz,CP(13).xyz),dot(CP(1).xyz,CP(13).xyz));}
vec3 cViewPosition(vec2 uv,float depth){
    vec2 xy=uv*vec2(2.0,-2.0)+vec2(-1.0,1.0)-cProjectionOffset();
    return vec3(xy*vec2(CP(11).w,CP(12).w)*depth,-depth);
}
vec3 cAtDepth(vec2 uv,float depth){
    vec3 ray=cPosition(uv,.5,4)-CP(10).xyz;
    return CP(10).xyz+ray*(depth/max(dot(ray,CP(13).xyz),1e-8));
}
float cDepth(ivec2 q){return cInside(q,cHalf())?HALF_DEPTH(q):0.0;}
vec4 cNormal(ivec2 q){return cInside(q,cHalf())?HALF_NORMAL(q):vec4(0.0,0.0,0.0,0.0);}
vec4 cCone(ivec2 q){return cInside(q,cHalf())?CONE_LOAD(q):vec4(0.0,0.0,0.0,0.0);}
vec3 cHalfPosition(ivec2 q){return cAtDepth((vec2(q)+.5)/cHalf(),cDepth(q));}
vec3 cHalfView(ivec2 q){return cViewPosition((vec2(q)+.5)/cHalf(),cDepth(q));}
vec3 cDepthNormal(ivec2 q){
    vec3 p=cHalfView(q),l=cHalfView(q-ivec2(1,0)),r=cHalfView(q+ivec2(1,0));
    vec3 u=cHalfView(q-ivec2(0,1)),d=cHalfView(q+ivec2(0,1));
    vec3 dx=abs(cDepth(q-ivec2(1,0))-cDepth(q))<abs(cDepth(q+ivec2(1,0))-cDepth(q))?p-l:r-p;
    vec3 dy=abs(cDepth(q-ivec2(0,1))-cDepth(q))<abs(cDepth(q+ivec2(0,1))-cDepth(q))?p-u:d-p;
    return cUnit(cross(dy,dx),vec3(0.0,0.0,1.0));
}
float cBayer(ivec2 q){
    uint x=uint(q.x),y=uint(q.y);
    uint lo=(((x^y)&1u)<<1u)|(y&1u);
    uint hi=((((x>>1u)^(y>>1u))&1u)<<1u)|((y>>1u)&1u);
    return float((lo<<2u)|hi)*.0625+.03125;
}
vec4 cSampleCone(vec2 uv){
    vec2 p=uv*cHalf()-.5;ivec2 b=ivec2(floor(p));vec2 f=fract(p);
    return mix(mix(cCone(b),cCone(b+ivec2(1,0)),f.x),
               mix(cCone(b+ivec2(0,1)),cCone(b+ivec2(1,1)),f.x),f.y);
}
float cGatherDepth(vec2 uv){
    ivec2 b=ivec2(floor(uv*cHalf()-.5));
    return max(max(HALF_DEPTH(cClamp(b,cHalf())),HALF_DEPTH(cClamp(b+ivec2(1,0),cHalf()))),
               max(HALF_DEPTH(cClamp(b+ivec2(0,1),cHalf())),HALF_DEPTH(cClamp(b+ivec2(1,1),cHalf()))));
}
uint cFlags(ivec2 q){return uint(HALF_NORMAL(cClamp(q,cHalf())).w*255.0+.5);}
vec4 cRaw(ivec2 pixel){
    vec2 uv=(vec2(pixel)+.5)/cHalf();
    vec3 p=cHalfView(pixel),normal=cDepthNormal(pixel);
    vec3 view=cUnit(p,vec3(0.0,0.0,-1.0));
    vec3 tangent=cUnit(cross(vec3(0.0,1.0,0.0),view),vec3(-1.0,0.0,0.0));
    vec3 bitangent=cross(view,tangent);
    float depth=cDepth(pixel),range=clamp((depth-5.0)*.1,0.0,1.0),radius=.4;
    if(depth>15.0){range=1.5;radius=.4+clamp((depth-15.0)*.1,0.0,1.0);}
    if(depth>50.0){range=1.5+40.0*clamp((depth-50.0)/150.0,0.0,1.0);radius=1.4+8.6*clamp((depth-50.0)*.025,0.0,1.0);}
    radius*=CP(8).z/.8;range*=CP(8).z/.8;
    bool foliage=(cFlags(pixel)&1u)!=0u;
    float rotation=foliage?0.0:fract(cBayer(pixel)+CP(9).y);
    float offset=foliage?1.0:2.0*fract(cBayer(pixel.yx)+CP(9).y);
    float area=0.0;vec3 moment=vec3(0.0,0.0,0.0);
    CONTACT_LOOP for(int i=0;i<C_DIRS;++i){
        float angle=(float(i)+.5+rotation)*(6.28318530718/float(C_DIRS));
        vec3 direction=cos(angle)*tangent+sin(angle)*bitangent;
        vec3 probe=p+direction*(radius/float(C_STEPS));
        vec2 projected=probe.xy/(-probe.z*vec2(CP(11).w,CP(12).w))+cProjectionOffset();
        vec2 step=projected*vec2(.5,-.5)+.5-uv;
        vec3 rel=p,pr=probe;
        float denominator=dot(normal,pr);
        float hit=abs(denominator)>1e-8?dot(normal,rel)/denominator:-1.0;
        vec3 delta=hit*pr-rel;
        float plane=hit<=0.0?1.0:dot(cUnit(delta,view),view),horizon=1.0;
        CONTACT_LOOP for(int j=0;j<C_STEPS;++j){
            vec2 q=uv+step*(offset+float(j));
            float h=plane;
            if(q.x>=0.0&&q.y>=0.0&&q.x<=1.0&&q.y<=1.0){
                vec3 diff=cViewPosition(q,cGatherDepth(q))-p;
                float distance=length(diff);
                h=mix(dot(diff,view)/max(distance,1e-8),plane,clamp(distance-range,0.0,1.0));
            }
            horizon=min(horizon,h);
        }
        horizon=clamp(horizon,-1.0,1.0);area+=(horizon+1.0)/float(C_DIRS);
        float h2=horizon*horizon;
        float axial=-.5376213133912192+.49405493145877283*horizon+.03581331526499383*h2;
        float magnitude=.9305141157425706+.8494448854988428*horizon-.1042526565205432*h2
            -.059156878555752766*h2*horizon-.038348820643562175*h2*h2;
        moment+=(sqrt(max(0.0,1.0-axial*axial))*direction+axial*view)*magnitude;
    }
    // Quantize in camera coordinates before spatial filtering, as the producer does.
    vec3 bent=cUnit(moment,normal);
    vec3 encoded=bent*vec3(1.0,-1.0,-1.0);
    bool suppressed=false;
    CONTACT_LOOP for(int y=0;y<2;++y) CONTACT_LOOP for(int x=0;x<2;++x){
        suppressed=suppressed||((cFlags(pixel+ivec2(x,y))&2u)!=0u);
        suppressed=suppressed||((cFlags(pixel+ivec2(x+1,y+1))&2u)!=0u);
    }
    return vec4(encoded*.5+.5,suppressed?1.0:clamp(area,0.0,1.0));
}
vec4 cSpatial(ivec2 pixel){
    float depth=cDepth(pixel),total=0.0;
    vec3 normal=cUnit(cNormal(pixel).xyz-.5,-CP(13).xyz);
    vec4 sum=vec4(0.0,0.0,0.0,0.0);
    CONTACT_LOOP for(int y=-1;y<=2;++y) CONTACT_LOOP for(int x=-1;x<=2;++x){
        ivec2 q=pixel+ivec2(x,y);
        vec3 n=cUnit(cNormal(q).xyz-.5,normal);
        float w=clamp(1.0-30.0*(1.0-dot(n,normal)),0.0,1.0)/(abs(cDepth(q)-depth)+.00000457088);
        sum+=cCone(q)*w;total+=w;
    }
    vec4 cone=sum/max(total,1e-12);vec3 axis=cUnit(cone.xyz-.5,vec3(0.0,0.0,-1.0));
    axis=axis.x*CP(11).xyz-axis.y*CP(12).xyz+axis.z*CP(13).xyz;
    return vec4(axis*.5+.5,cone.w);
}
vec4 cTemporal(ivec2 pixel){
    vec4 current=cCone(pixel);
    if(CP(9).z<.5||(cFlags(pixel)&4u)!=0u)return current;
    vec2 velocity=HALF_VELOCITY(pixel);
    vec2 uv=(vec2(pixel)+.5)/cHalf()+velocity;
    vec2 halfPixel=.5/cHalf();
    if(uv.x<=halfPixel.x||uv.y<=halfPixel.y||uv.x>=1.0-halfPixel.x||uv.y>=1.0-halfPixel.y)return current;
    vec2 diffVelocity=velocity-PREV_VELOCITY(uv);
    float velocityMatch=clamp(1.0-256.0*length(diffVelocity),0.0,1.0);
    // Rigid/object motion belongs in UV. Depth confidence compares camera-space
    // positions, matching the reference producer rather than a tuned depth bias.
    vec3 currentPosition=cHalfPosition(pixel);
    float oldLinear=PREV_DEPTH(uv);
    vec3 oldRay=cPosition(uv,.5,18)-CP(22).xyz;
    vec3 oldPoint=CP(22).xyz+oldRay*(oldLinear/max(dot(oldRay,CP(24).xyz),1e-8));
    float agreement=clamp(1.0-dot(currentPosition-oldPoint,currentPosition-oldPoint),0.0,1.0);
    vec4 history=PREV_CONE(uv);
    vec3 axis=cUnit(current.xyz-.5,-CP(13).xyz),oldAxis=cUnit(history.xyz-.5,axis);
    vec2 corner=vec2(pixel)/cHalf();
    float low=1.0,high=0.0,limit=1.0;
    CONTACT_LOOP for(int y=0;y<2;++y) CONTACT_LOOP for(int x=0;x<2;++x){
        vec4 n=cSampleCone(corner+vec2(float(x),float(y))/cHalf());
        low=min(low,n.w);high=max(high,n.w);
        limit=min(limit,dot(axis,cUnit(n.xyz-.5,axis)));
    }
    float margin=.5*velocityMatch*(high-low+.05);
    low-=margin;high+=margin;limit-=.5*velocityMatch*((1.0-limit)+.01);
    float alignment=dot(oldAxis,axis),clipped=max(limit,alignment);
    vec3 side=cUnit(oldAxis-alignment*axis+vec3(0.0,0.0,.0001),CP(11).xyz);
    vec3 clampedAxis=cUnit(side*sqrt(max(0.0,1.0-clipped*clipped))+axis*clipped,axis);
    float weight=.9*agreement;
    return vec4(cUnit(mix(axis,clampedAxis,weight),axis)*.5+.5,mix(current.w,clamp(history.w,low,high),weight));
}
vec4 cUpsample(ivec2 pixel){
    vec2 uv=(vec2(pixel)+.5)/CP(8).xy;
    float depth=cLinear(uv,FULL_DEPTH(pixel));
    // Integer parity avoids a drifting footprint for odd-sized viewports.
    vec2 coord=(vec2(pixel)+.5)*.5-.5;ivec2 base=ivec2(floor(coord));vec2 f=fract(coord);
    vec4 sum=vec4(0.0,0.0,0.0,0.0);float total=0.0;
    CONTACT_LOOP for(int y=0;y<2;++y) CONTACT_LOOP for(int x=0;x<2;++x){
        ivec2 q=cClamp(base+ivec2(x,y),cHalf());
        float w=(x==0?1.0-f.x:f.x)*(y==0?1.0-f.y:f.y)/(abs(cDepth(q)-depth)+.00000457088);
        sum+=CONE_LOAD(q)*w;total+=w;
    }
    return sum/max(total,1e-12);
}
void contactCompute(ivec2 pixel){
#if C_STAGE == 4
    if(!cInside(pixel,CP(8).xy))return;
    STORE_CONE(pixel,cUpsample(pixel));
#elif C_STAGE == 0
    if(!cInside(pixel,cHalf()))return;
    ivec2 b=pixel*2;
    ivec2 selected=cClamp(b+ivec2(0,1),CP(8).xy);
    float depth=FULL_DEPTH(selected);uint flags=0u;
    // Gather order defines ties: bottom-left, bottom-right, top-right, top-left.
    CONTACT_LOOP for(int k=0;k<4;++k){
        ivec2 q=cClamp(b+ivec2(k==1||k==2?1:0,k<2?1:0),CP(8).xy);
        float d=FULL_DEPTH(q);flags|=FULL_FLAGS(q);
        if(d>depth){depth=d;selected=q;}
    }
    vec4 motion=FULL_VELOCITY(selected);
    if(motion.w<.5)flags|=4u;
    STORE_DEPTH(pixel,cLinear((vec2(selected)+.5)/CP(8).xy,depth));
    STORE_VELOCITY(pixel,motion.xy);
    STORE_NORMAL(pixel,vec4(FULL_NORMAL(selected).xyz,float(flags&255u)/255.0));
#else
    if(!cInside(pixel,cHalf()))return;
#if C_STAGE == 1
    STORE_CONE(pixel,cRaw(pixel));
#elif C_STAGE == 2
    STORE_CONE(pixel,cSpatial(pixel));
#else
    STORE_CONE(pixel,cTemporal(pixel));
#endif
#endif
}
