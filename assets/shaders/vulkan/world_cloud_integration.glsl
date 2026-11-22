vec2 cloudFrame(float frame){return vec2(frame-floor(frame/8.0)*8.0,8.0-floor(frame/8.0))/8.0;}
vec4 cloudNoise(vec3 uvw){
    float frame=ceil(uvw.z*64.0),phase=fract(uvw.z*64.0);
    vec2 uv=fract(uvw.xy)/8.0;
    uv=fract((fract(uvw.xy)-.5)*mix(vec2(1,1),fract(uv-.5),1.0/64.0)+.5)/8.0;
    vec4 a=CL_NOISE(fract(uv+cloudFrame(frame)));
    return mix(a,CL_NOISE(fract(uv+cloudFrame(frame+1.0))),phase);
}
float cloudDensity(vec3 samplePosition){
    if(samplePosition.y<CP(6).x||samplePosition.y>CP(6).y)return 0.0;
    float altitude=clamp((samplePosition.y-CP(6).x)/(CP(6).y-CP(6).x),0.0,1.0);
    vec2 uv=fract(samplePosition.xz/(CP(7).x*35000.0)+.5+CP(11).xy/CP(7).x);
    vec4 weather=CL_WEATHER(uv);
    float coverage=max(weather.r,clamp(CP(5).w-.5,0.0,1.0)*weather.g*2.0);
    float horizon=fract(max(0.0,(length(CP(2).xz-samplePosition.xz)-2048.0)/4096.0))*CP(6).z;
    coverage=clamp(coverage+horizon,0.0,1.0);
    float stop=clamp(weather.b+.12,0.0,1.0);
    float height=clamp(altitude/.07,0.0,1.0)*clamp((stop-altitude)/max(stop*.8,1e-6),0.0,1.0);
    float scale=altitude*clamp(altitude/.2,0.0,1.0)*weather.a*2.0*clamp((1.0-altitude)/.1,0.0,1.0);
    if(clamp(coverage*height*CP(5).w-.2,0.0,1.0)*scale<=.0001)return 0.0;
    float size=1200.0*CP(7).y;
    vec3 coords=fract(samplePosition.xzy/size+vec3(.5,.5,0))+vec3(CP(11).zw,0)/size*35000.0;
    vec4 noise=cloudNoise(coords);
    float lower=dot(noise.rgb,vec3(.625,.25,.125))-1.0;
    float shape=(noise.a-lower)/(1.0-lower);
    float mask=max(CP(5).w*coverage,1e-6);
    float value=clamp((clamp(shape+horizon,0.0,1.0)*height-(1.0-mask))/mask,0.0,1.0)*scale;
    if(value>.0001) {
        size=200.0*CP(7).z;
        coords=fract(samplePosition.xzy/size+vec3(.5,.5,0))+vec3(CP(12).xy,0)/size*35000.0;
        vec4 detail=cloudNoise(coords);float d=dot(detail.rgb,vec3(.625,.25,.125));
        d=mix(d,1.0-d,clamp(altitude*5.0,0.0,1.0))*exp(-CP(5).w);
        shape=clamp((shape-d)/max(1.0-d,1e-6),0.0,1.0);
        value=clamp((shape*height-(1.0-mask))/mask,0.0,1.0)*scale;
    }
    return value;
}
float cloudPhase(float cosine,float g){return (1.0-g*g)/pow(abs(1.0+g*g-2.0*g*cosine),1.5)*(.25*3.1415);}
float cloudScatter(float cosine){
    return mix(max(cloudPhase(cosine,CP(9).x),cloudPhase(cosine,CP(9).w)),
        cloudPhase(cosine,-CP(9).y),CP(9).z);
}
float cloudShadowSample(vec3 p){
    float result=0.0,stepLength=100.0*CP(8).y;
    CL_LOOP for(int i=0;i<int(CP(8).x);++i){
        result+=cloudDensity(p)*CP(4).w*CP(6).w*stepLength;
        p+=CP(3).xyz*stepLength;
        if(p.y>CP(6).y||p.y<CP(6).x)break;
    }return result;
}
float cloudInitialDistance(vec3 ray){
    vec3 p=CP(2).xyz+ray*CP(10).y;
    if(p.y>=CP(6).x&&p.y<=CP(6).y)return CP(10).y;
    if(abs(ray.y)<1e-7)return 1e9;
    float plane=p.y<CP(6).x?CP(6).x:CP(6).y;
    float d=(plane-CP(2).y)/ray.y;
    return d>=CP(10).y?d:1e9;
}
float cloudOpticalDepth(vec3 p,float altitude){
    vec4 s=CL_SHADOW(fract((p.xz-CP(12).zw)/4096.0+.5));
    float a=clamp(altitude/.75,0.0,1.0);
    float v=a<.33?mix(s.x,s.y,a*3.0):a<.66?mix(s.y,s.z,(a-.33)*3.0):mix(s.z,s.w,(a-.66)*3.0);
    return clamp(v,0.0,1.0)*5.0;
}
vec4 cloudMarch(vec3 ray,float depth,float jitter){
    float initial=cloudInitialDistance(ray);
    float stepLength=12.0*CP(7).w,d=initial+jitter*stepLength;
    if(d>=depth)return vec4(0,0,0,1);
    vec3 p=CP(2).xyz+ray*d,energy=vec3(0,0,0);float transmission=1.0,total=0.0,distanceScale=1.0;
    float phase=cloudScatter(dot(ray,CP(3).xyz));
    CL_LOOP for(int i=1;i<=int(CP(8).z);++i){
        if(d>=depth||transmission<=.01||p.y<CP(6).x-10.0||p.y>CP(6).y+10.0)break;
        float a=clamp((p.y-CP(6).x)/(CP(6).y-CP(6).x),0.0,1.0);
        float density=cloudDensity(p);
        stepLength=mix(18.0*CP(7).w,6.0*CP(7).w,density)*distanceScale;
        density=clamp(density*CP(4).w*clamp((total-CP(10).y)/CP(10).z,0.0,1.0),0.0,1.0);
        if(density>0.0){
            float shadow=exp(-cloudOpticalDepth(p,a));
            vec3 light=CP(4).rgb*shadow*phase*clamp(1.0-exp(-density*100.0*CP(10).x),0.0,1.0)+CP(5).rgb*mix(.5,2.0,a);
            density=clamp(density*stepLength,0.0,1.0);
            energy+=light*(transmission*density);transmission*=1.0-density;
        }else{float f=float(i)/CP(8).z;distanceScale=mix(1.0,2.0,f*f*f*f);}
        total+=transmission*stepLength;p+=ray*stepLength;d+=stepLength;
    }
    energy*=CP(13).rgb;
    if(CP(10).w>0.0){
        float opacity=1.0-transmission;
        vec4 fog=worldFogAt(CP(2).xyz+ray*(initial+total));
        vec3 unpremultiplied=energy/max(opacity,.001);
        energy=mix(unpremultiplied,fog.rgb+fog.a*unpremultiplied,CP(10).w)*opacity;
    }
    return vec4(energy,transmission);
}
