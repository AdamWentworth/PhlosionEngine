// Directional visibility with edge-aware filtering and camera reprojection.
// The production adapter uses geometric depth/normal inputs and rejects moving
// depth rather than assuming unavailable per-object motion vectors.
bool worldContactFiltering() { return CONTACT(8).w>1.5; }
float contactFraction(float value) { return value-floor(value); }
vec2 contactHalfSize() { return floor((CONTACT(8).xy+1.0)*.5); }
vec3 contactSelectFullDepthPixel(vec2 pixel) {
    vec2 base=clamp(pixel,vec2(0.0,0.0),contactHalfSize()-1.0)*2.0;
    vec2 upper=min(base+1.0,CONTACT(8).xy-1.0);
    vec3 selected=vec3(base,-1.0);
    // Keep row-major tie selection, but avoid nested dynamic loops at every ray
    // sample. Carry the chosen depth so callers need not select/load it again.
    float candidate=CONTACT_DEPTH(base);
    if(candidate>selected.z)selected=vec3(base,candidate);
    vec2 p=vec2(upper.x,base.y);candidate=CONTACT_DEPTH(p);
    if(candidate>selected.z)selected=vec3(p,candidate);
    p=vec2(base.x,upper.y);candidate=CONTACT_DEPTH(p);
    if(candidate>selected.z)selected=vec3(p,candidate);
    candidate=CONTACT_DEPTH(upper);
    if(candidate>selected.z)selected=vec3(upper,candidate);
    return selected;
}
// Store all 32 depth bits losslessly in the existing RGBA16F scratch texture.
// Each 11/11/10-bit integer is exactly representable in half precision; alpha
// identifies the selected full-resolution pixel. Never filter this payload.
vec4 contactPrepareDepth(vec2 pixel) {
    vec3 selected=contactSelectFullDepthPixel(pixel);
    uint bits=floatBitsToUint(selected.z);
    vec2 offset=selected.xy-pixel*2.0;
    return vec4(float(bits&2047u),float((bits>>11u)&2047u),float(bits>>22u),offset.x+2.0*offset.y);
}
vec3 contactSelectedDepthPixel(vec2 pixel) {
#ifdef CONTACT_PREPARED_FETCH
    if(CONTACT(8).w>1.5 && CONTACT(8).w<3.5) {
        vec2 q=clamp(pixel,vec2(0.0,0.0),contactHalfSize()-1.0);
        vec4 packed=CONTACT_PREPARED_FETCH(q);
        uint bits=uint(packed.x)|(uint(packed.y)<<11u)|(uint(packed.z)<<22u);
        uint offset=uint(packed.w);
        return vec3(q*2.0+vec2(float(offset&1u),float(offset>>1u)),uintBitsToFloat(bits));
    }
#endif
    return contactSelectFullDepthPixel(pixel);
}
vec2 contactSelectedPixel(vec2 pixel) {return contactSelectedDepthPixel(pixel).xy;}
vec3 contactSamplePosition(vec3 samplePixel) {
    vec2 uv=(samplePixel.xy+.5)/CONTACT(8).xy;uv.y=1.0-uv.y;
    return contactPosition(uv,samplePixel.z);
}
vec3 contactHalfPosition(vec2 pixel) {return contactSamplePosition(contactSelectedDepthPixel(pixel));}
float contactViewDepth(vec3 position) {return dot(position-CONTACT(10).xyz,CONTACT(13).xyz);}
vec3 contactDepthNormal(vec2 pixel) {
    vec3 p=contactHalfPosition(pixel),l=contactHalfPosition(pixel-vec2(1.0,0.0)),r=contactHalfPosition(pixel+vec2(1.0,0.0));
    vec3 t=contactHalfPosition(pixel-vec2(0.0,1.0)),b=contactHalfPosition(pixel+vec2(0.0,1.0));
    vec3 dx=abs(contactViewDepth(l)-contactViewDepth(p))<abs(contactViewDepth(r)-contactViewDepth(p))?p-l:r-p;
    vec3 dy=abs(contactViewDepth(t)-contactViewDepth(p))<abs(contactViewDepth(b)-contactViewDepth(p))?p-t:b-p;
    vec3 n=cross(dx,dy);
    if(dot(n,n)<.0000000001)n=CONTACT_AUX_FETCH(contactSelectedPixel(pixel)).xyz*2.0-1.0;
    if(dot(n,n)<.0000000001)n=-CONTACT(13).xyz;
    n=normalize(n);return dot(n,CONTACT(10).xyz-p)<0.0?-n:n;
}
vec4 contactRaw(vec2 pixel) {
    vec3 selectedDepth=contactSelectedDepthPixel(pixel);
    vec2 selected=selectedDepth.xy;
    if(selectedDepth.z>=.999999)return vec4(.5,.5,.5,1.0);
    vec3 p=contactSamplePosition(selectedDepth);
    // The prepass provides geometric normals without normal-map detail. Unlike
    // depth differences, these remain defined at silhouettes and thin geometry.
    vec3 normal=CONTACT_AUX_FETCH(selected).xyz*2.0-1.0;
    normal=dot(normal,normal)>.000001?normalize(normal):contactDepthNormal(pixel);
    if(dot(normal,CONTACT(10).xyz-p)<0.0)normal=-normal;
    vec3 view=normalize(p-CONTACT(10).xyz);
    vec3 tangent=normalize(cross(CONTACT(12).xyz,view));
    vec3 bitangent=normalize(cross(view,tangent));
    float depth=contactViewDepth(p),range=clamp((depth-5.0)*.1,0.0,1.0),radius=.4;
    if(depth>15.0){range=1.5;radius=.4+clamp((depth-15.0)*.1,0.0,1.0);}
    if(depth>50.0){range=1.5+40.0*clamp((depth-50.0)/150.0,0.0,1.0);radius=1.4+8.6*clamp((depth-50.0)*.025,0.0,1.0);}
    radius*=CONTACT(8).z/.8;range*=CONTACT(8).z/.8;
    float phase=contactFraction((pixel.x+pixel.y*4.0)*.0625+.03125+CONTACT(9).y);
    float jitter=contactFraction((pixel.y+pixel.x*4.0)*.0625+.03125+CONTACT(9).y)*2.0;
    vec2 uv=(selected+.5)/CONTACT(8).xy;uv.y=1.0-uv.y;
    float area=0.0;vec3 moment=vec3(0.0,0.0,0.0);
    CONTACT_LOOP for(int i=0;i<15;++i) {
        float angle=(float(i)+.5+phase)*.41887906194;
        vec3 direction=cos(angle)*tangent+sin(angle)*bitangent;
        vec3 probe=p+direction*radius*.1;
        vec4 clip=contactTransform(vec4(probe,1.0),0);
        vec2 stepUv=clip.xy/clip.w*.5+.5-uv;
        vec3 relative=p-CONTACT(10).xyz,probeRelative=probe-CONTACT(10).xyz;
        float denominator=dot(normal,probeRelative);
        float intersection=abs(denominator)>.000001?dot(normal,relative)/denominator:-1.0;
        vec3 planeDelta=intersection*probeRelative-relative;
        float planeHorizon=intersection<=0.0?1.0:dot(planeDelta,view)/max(length(planeDelta),.000001);
        float horizon=1.0;
        CONTACT_LOOP for(int j=0;j<10;++j) {
            vec2 sampleUv=uv+stepUv*(jitter+float(j));
            float candidate=planeHorizon;
            if(sampleUv.x>=0.0 && sampleUv.y>=0.0 && sampleUv.x<1.0 && sampleUv.y<1.0) {
                vec2 q=floor(vec2(sampleUv.x,1.0-sampleUv.y)*contactHalfSize());
                vec3 samplePixel=contactSelectedDepthPixel(q);
                vec3 delta=contactSamplePosition(samplePixel)-p;
                float distanceToSample=length(delta);
                // Snapped samples on the receiver's own plane are not blockers.
                if(samplePixel.z<.999999 && distanceToSample>.00001 && dot(delta,normal)>CONTACT(9).x)
                    candidate=mix(dot(delta,view)/distanceToSample,planeHorizon,clamp(distanceToSample-range,0.0,1.0));
            }
            horizon=min(horizon,candidate);
        }
        horizon=clamp(horizon,-1.0,1.0);area+=(horizon+1.0)*.41887906194;
        float axial=(horizon*.0358133167+.4940549433)*horizon-.5376213193;
        float h2=horizon*horizon;
        float magnitude=.9305140972+.8494448662*horizon-.1042526588*h2-.0591568798*h2*horizon-.0383488201*h2*h2;
        moment+=(sqrt(max(0.0,1.0-axial*axial))*direction+axial*view)*magnitude;
    }
    vec3 bent=dot(moment,moment)>.000001?normalize(moment):normal;
    return vec4(bent*.5+.5,clamp(area*.1591549431,0.0,1.0));
}
vec3 contactUnit(vec3 value,vec3 fallback) {vec3 v=value*2.0-1.0;return dot(v,v)>.000001?normalize(v):fallback;}
vec4 contactSpatial(vec2 pixel) {
    float depth=contactViewDepth(contactHalfPosition(pixel));
    vec3 normal=contactUnit(CONTACT_AUX_FETCH(contactSelectedPixel(pixel)).xyz,contactDepthNormal(pixel));
    vec4 sum=vec4(0.0,0.0,0.0,0.0);float total=0.0;
    CONTACT_LOOP for(int y=-1;y<=2;++y) CONTACT_LOOP for(int x=-1;x<=2;++x) {
        vec2 q=clamp(pixel+vec2(float(x),float(y)),vec2(0.0,0.0),contactHalfSize()-1.0);
        vec3 samplePixel=contactSelectedDepthPixel(q);
        vec3 n=contactUnit(CONTACT_AUX_FETCH(samplePixel.xy).xyz,normal);
        float weight=clamp(1.0-30.0*(1.0-dot(n,normal)),0.0,1.0)/(abs(contactViewDepth(contactSamplePosition(samplePixel))-depth)+.00000457088);
        sum+=CONTACT_BUFFER_FETCH(q)*weight;total+=weight;
    }
    vec4 result=sum/max(total,.000001);return vec4(contactUnit(result.xyz,normal)*.5+.5,result.w);
}
vec4 contactTemporal(vec2 pixel) {
    vec4 current=CONTACT_BUFFER_FETCH(pixel);
    if(CONTACT(9).z<.5)return current;
    vec3 p=contactHalfPosition(pixel);
    vec4 oldClip=contactTransform(vec4(p,1.0),14);
    if(oldClip.w<=.001)return current;
    vec2 uv=oldClip.xy/oldClip.w*.5+.5;uv.y=1.0-uv.y;
    if(uv.x<=.5/CONTACT(8).x || uv.y<=.5/CONTACT(8).y || uv.x>=1.0-.5/CONTACT(8).x || uv.y>=1.0-.5/CONTACT(8).y)return current;
    vec2 oldPixel=clamp(floor(uv*CONTACT(8).xy),vec2(0.0,0.0),CONTACT(8).xy-1.0);
    float oldDepth=CONTACT_AUX_FETCH(oldPixel).r;
    vec2 oldUv=(oldPixel+.5)/CONTACT(8).xy;oldUv.y=1.0-oldUv.y;
    vec4 oldPosition=contactTransform(vec4(oldUv*2.0-1.0,oldDepth*2.0-1.0,1.0),18);
    vec3 delta=oldPosition.xyz/oldPosition.w-p;
    float tolerance=.025+contactViewDepth(p)*.002;
    float agreement=clamp(1.0-dot(delta,delta)/(tolerance*tolerance),0.0,1.0);
    if(agreement<=0.0 || oldDepth>=.999999)return current;
    vec4 history=CONTACT_HISTORY_SAMPLE(uv*CONTACT(23).xy);
    vec3 direction=contactUnit(current.xyz,-CONTACT(13).xyz),oldDirection=contactUnit(history.xyz,direction);
    float low=current.w,high=current.w,cosineLimit=1.0;
    CONTACT_LOOP for(int y=-1;y<=1;++y) CONTACT_LOOP for(int x=-1;x<=1;++x) {
        vec4 v=CONTACT_BUFFER_FETCH(clamp(pixel+vec2(float(x),float(y)),vec2(0.0,0.0),contactHalfSize()-1.0));
        low=min(low,v.w);high=max(high,v.w);cosineLimit=min(cosineLimit,dot(direction,contactUnit(v.xyz,direction)));
    }
    float cosine=clamp(dot(direction,oldDirection),-1.0,1.0),limit=max(cosine,cosineLimit);
    vec3 tangent=oldDirection-cosine*direction;
    vec3 clipped=direction;
    if(dot(tangent,tangent)>.000001)clipped=normalize(normalize(tangent)*sqrt(max(0.0,1.0-limit*limit))+limit*direction);
    float margin=.025,weight=.9*agreement;
    return vec4(normalize(mix(direction,clipped,weight))*.5+.5,mix(current.w,clamp(history.w,low-margin,high+margin),weight));
}
vec4 worldContactFilter(vec2 fragCoord) {
    vec2 pixel=floor(fragCoord);
    if(CONTACT(8).w>4.5)return contactPrepareDepth(pixel);
    if(contactSelectedDepthPixel(pixel).z>=.999999)return vec4(.5,.5,.5,1.0);
    if(CONTACT(8).w<2.5)return contactRaw(pixel);
    if(CONTACT(8).w<3.5)return contactSpatial(pixel);
    return contactTemporal(pixel);
}
vec4 contactReconstruct(vec3 position,vec3 normal) {
    vec4 clip=contactTransform(vec4(position,1.0),0);
    vec2 uv=clip.xy/clip.w*.5+.5;uv.y=1.0-uv.y;
    if(CONTACT(10).w<-.5){
        vec4 value=CONTACT_BUFFER_FETCH(clamp(floor(uv*CONTACT(8).xy),vec2(0.0,0.0),CONTACT(8).xy-1.0));
        return vec4(contactUnit(value.xyz,normal),value.w);
    }
    vec2 q=uv*contactHalfSize()-.5,base=floor(q),fraction=q-base;
    float depth=contactViewDepth(position),total=0.0;vec4 sum=vec4(0.0,0.0,0.0,0.0);
    CONTACT_LOOP for(int y=0;y<2;++y) CONTACT_LOOP for(int x=0;x<2;++x) {
        vec2 p=clamp(base+vec2(float(x),float(y)),vec2(0.0,0.0),contactHalfSize()-1.0);
        vec3 samplePixel=contactSelectedDepthPixel(p);
        if(samplePixel.z>=.999999)continue;
        float weight=(x==0?1.0-fraction.x:fraction.x)*(y==0?1.0-fraction.y:fraction.y);
        weight/=abs(contactViewDepth(contactSamplePosition(samplePixel))-depth)+.00000457088;
        sum+=CONTACT_BUFFER_FETCH(p)*weight;total+=weight;
    }
    if(total<=.000001)return vec4(normal,1.0);
    vec4 value=sum/total;return vec4(contactUnit(value.xyz,normal),clamp(value.w,0.0,1.0));
}
