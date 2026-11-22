vec3 environmentAnalyticSky(vec3 ray,vec3 sun) {
    float theta=max(ray.y,ENV_SKY(9).w),gamma=clamp(dot(ray,sun),-1.0,1.0);
    vec3 result=vec3(0.0,0.0,0.0);
    for(int c=0;c<3;++c) {
        vec4 a=ENV_SKY(c*3),b=ENV_SKY(c*3+1);
        float g=ENV_SKY(c*3+2).x;
        float mie=(1.0+gamma*gamma)/pow(abs(1.0+g*g-2.0*g*gamma),1.5);
        result[c]=(1.0+a.x*exp(a.y/(theta+.01)))*
            (a.z+a.w*exp(b.x*acos(gamma))+b.y*gamma*gamma+b.z*mie+b.w*sqrt(theta))*ENV_SKY(9)[c];
    }
    float span=max(abs(ENV_SKY(13).y-ENV_SKY(13).x),1e-11);
    float mask=clamp((gamma+1.0)*.5-ENV_SKY(13).x,0.0,1.0);
    mask=clamp(mask/span,0.0,1.0);
    return result*pow(abs(mix(vec3(1.0,1.0,1.0),ENV_SKY(12).rgb,1.0-mask)),
        vec3(abs(ENV_SKY(12).w),abs(ENV_SKY(12).w),abs(ENV_SKY(12).w)));
}
