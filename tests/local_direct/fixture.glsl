FiniteLightResponse fixtureLocalComponents(vec3 position,vec3 normal,vec3 view,
        vec3 albedo,vec3 f0,float roughness,float metallic) {
    FiniteLightResponse result=emptyLocalLightResponse();
    if(ENV_SUN.w<.5) return result;
    if(dot(normal,view)<0.0)normal=-normal;
    if(dot(normal,view)<=0.0)return result;
    WorldRectPbrContext context=prepareWorldRectPbr(normal,view,roughness);
    DIRECT_TEST_LOOP for(int pointIndex=0;pointIndex<4;++pointIndex) {
        FiniteLightResponse light=environmentPointPbr(pointIndex,position,normal,view,albedo,f0,roughness,metallic);
        result.diffuse+=light.diffuse;result.specular+=light.specular;
    }
    DIRECT_TEST_LOOP for(int rectIndex=0;rectIndex<16;++rectIndex) {
        // Keep derivative-based PCF in the forward traversal, outside the pure
        // per-source response. The classified consumer supplies its own visibility.
        if(ENV_RECT(rectIndex*4+1).w<=.5) {
            vec3 delta,u,v,radiance;
            if(!environmentRectSample(rectIndex,position,delta,u,v,radiance))continue;
        }
        float visibility=environmentLocalShadowVisibility(rectIndex,position,normal);
        FiniteLightResponse light=environmentRectPbr(rectIndex,position,normal,view,albedo,f0,roughness,metallic,context,visibility);
        result.diffuse+=light.diffuse;result.specular+=light.specular;
    }
    return result;
}

void computeFixture(uint lane,out vec4 first,out vec4 second) {
    float maxError=0.0,minEnergy=0.0,sumDiffuse=0.0,sumSpecular=0.0,maxListError=0.0,maxZero=0.0;
    DIRECT_TEST_LOOP for(int sampleIndex=0;sampleIndex<128;++sampleIndex) {
        float k=float(lane*128u+uint(sampleIndex));
        vec3 position=vec3(float(sampleIndex%17)-8.0,float(sampleIndex%7)-2.0,float(lane%13u)-6.0);
        vec3 normal=normalize(vec3(sin(k*.37),cos(k*.13),sin(k*.19)));
        vec3 view=normalize(vec3(cos(k*.17),.3+sin(k*.31),.6));
        vec3 albedo=vec3(.1+.8*(k*.13-floor(k*.13)),.3,.7);
        float metallic=float(sampleIndex%5)*.25,roughness=.16+.84*float(sampleIndex%19)/18.0;
        vec3 f0=vec3(.04,.04,.04)*(1.0-metallic)+albedo*metallic;
        FiniteLightResponse direct=fixtureLocalComponents(position,normal,view,albedo,f0,roughness,metallic);
        vec3 oriented=dot(normal,view)<0.0?-normal:normal;
        vec3 bounce=(vec3(1.0,1.0,1.0)-f0)*(1.0-metallic)*albedo*
            environmentDiffuseProbeIrradiance(position,oriented)*.65/3.14159265;
        vec3 expected=referenceLocalPbr(position,normal,view,albedo,f0,roughness,metallic,.65);
        vec3 combined=environmentLocalDirectPbr(position,normal,view,albedo,f0,roughness,metallic);
        vec3 error=max(abs(combined+bounce-expected),abs(direct.diffuse+direct.specular-combined))/
            max(abs(expected),vec3(1.0,1.0,1.0));
        maxError=max(maxError,max(error.r,max(error.g,error.b)));
        minEnergy=min(minEnergy,min(min(direct.diffuse.r,min(direct.diffuse.g,direct.diffuse.b)),
                                   min(direct.specular.r,min(direct.specular.g,direct.specular.b))));
        sumDiffuse+=direct.diffuse.r+direct.diffuse.g+direct.diffuse.b;
        sumSpecular+=direct.specular.r+direct.specular.g+direct.specular.b;
        // Emulate two independently built work lists, in reversed order.
        vec3 selected=vec3(0.0,0.0,0.0);
        WorldRectPbrContext context=prepareWorldRectPbr(oriented,view,roughness);
        DIRECT_TEST_LOOP for(int i=15;i>=0;--i) {
            FiniteLightResponse light=environmentRectPbr(i,position,oriented,view,albedo,f0,roughness,metallic,context,environmentLocalShadowVisibility(i,position,oriented));
            selected+=light.diffuse+light.specular;
            if(i%4==0) maxZero=max(maxZero,length(light.diffuse)+length(light.specular));
        }
        DIRECT_TEST_LOOP for(int i=3;i>=0;--i) {
            FiniteLightResponse light=environmentPointPbr(i,position,oriented,view,albedo,f0,roughness,metallic);
            selected+=light.diffuse+light.specular;
        }
        vec3 listError=abs(selected-direct.diffuse-direct.specular)/max(vec3(1.0,1.0,1.0),selected);
        maxListError=max(maxListError,max(listError.r,max(listError.g,listError.b)));
    }
    first=vec4(maxError,minEnergy,sumDiffuse,sumSpecular);
    FiniteLightResponse aligned=environmentPointPbr(0,vec3(-3.0,0.0,1.0),vec3(0.0,1.0,0.0),
        vec3(0.0,1.0,0.0),vec3(.5,.5,.5),vec3(.04,.04,.04),.16+.84*float(lane)/31.0,0.0);
    second=vec4(maxListError,maxZero,aligned.diffuse.r,aligned.specular.r);
}
