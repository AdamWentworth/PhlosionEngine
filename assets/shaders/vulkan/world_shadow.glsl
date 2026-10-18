// Manual bounded 3x3 PCF preserves the same comparison on all supported APIs.
float environmentShadowVisibility(vec3 position, vec3 normal) {
    if (ENV_SUN.w < 0.5 || WORLD_SHADOW_PARAMS.x <= 0.0) return 1.0;
    float normalLength = dot(normal, normal);
    if (normalLength > 0.00000001) {
        normal *= inversesqrt(normalLength);
        float orientation = dot(normal, environmentLightDirection()) < 0.0 ? -1.0 : 1.0;
        position += normal * (orientation * WORLD_SHADOW_PARAMS.z);
    }
    vec4 projected = WORLD_SHADOW_CLIP(position);
    if (projected.w <= 0.00000001) return 1.0;
    vec3 clip = projected.xyz / projected.w;
    vec2 uv = clip.xy * vec2(0.5, WORLD_SHADOW_Y * 0.5) + vec2(0.5, 0.5);
    float referenceDepth = clip.z * 0.5 + 0.5 - WORLD_SHADOW_PARAMS.y;
    // Compare each nearest-filtered texel against the receiver plane at that
    // texel's centre. One depth for the whole PCF footprint self-shadows slopes.
    vec3 dx = dFdx(vec3(uv, referenceDepth));
    vec3 dy = dFdy(vec3(uv, referenceDepth));
    float determinant = dx.x * dy.y - dx.y * dy.x;
    vec2 depthGradient = vec2(0.0, 0.0);
    if (abs(determinant) > 0.00000000000000000001)
        depthGradient = vec2(dy.y * dx.z - dx.y * dy.z,
                             dx.x * dy.z - dy.x * dx.z) / determinant;
    if (max(abs(clip.x), max(abs(clip.y), abs(clip.z))) >= 1.0) return 1.0;
    float texelSize = WORLD_SHADOW_PARAMS.w;
    vec2 centre = (floor(uv / texelSize) + vec2(0.5, 0.5)) * texelSize;
    float visibility = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 tap = centre + vec2(float(x), float(y)) * texelSize;
            float depth = min(tap.x, tap.y) < 0.0 || max(tap.x, tap.y) > 1.0
                ? 1.0 : WORLD_SHADOW_SAMPLE(WORLD_SHADOW_ATLAS.xy + tap * WORLD_SHADOW_ATLAS.zw);
            float planeDepth = referenceDepth + clamp(dot(depthGradient, tap - uv), -0.01, 0.01);
            visibility += step(planeDepth, depth);
        }
    }
    return mix(1.0, visibility / 9.0, WORLD_SHADOW_PARAMS.x);
}

void updateEnvironmentDirectionalVisibility(vec3 position, vec3 normal, float mode) {
    environmentDirectionalVisibility = 1.0;
    if (mode >= 1.5 && (mode < 249.5 || mode == 254.0) && !(mode > 2.5 && mode < 3.5))
        environmentDirectionalVisibility = environmentShadowVisibility(position, normal);
}


float environmentLocalMomentVisibility(int index, vec4 projected, vec4 parameters) {
    if (projected.w <= .00000001) return 1.0;
    vec3 clip = projected.xyz / projected.w;
    if (max(abs(clip.x),max(abs(clip.y),abs(clip.z))) >= 1.0) return 1.0;
    vec2 uv = clip.xy * vec2(.5,WORLD_SHADOW_Y*.5) + vec2(.5,.5);
    float texelSize = -parameters.w;
    vec2 texel = uv / texelSize - vec2(.5,.5), base = floor(texel), fraction = texel-base;
    vec4 tile = WORLD_LOCAL_SHADOW(index*6+5);
    tile.x = (tile.x-.5)*2.0; tile.z *= 2.0;
    vec2 moments = vec2(0.0,0.0);
    for (int y=0;y<2;++y) for (int x=0;x<2;++x) {
        vec2 tap = clamp((base+vec2(float(x),float(y))+vec2(.5,.5))*texelSize,
            vec2(.5,.5)*texelSize,vec2(1.0,1.0)-vec2(.5,.5)*texelSize);
        vec2 weight = mix(vec2(1.0,1.0)-fraction,fraction,vec2(float(x),float(y)));
        moments += WORLD_MOMENT_SAMPLE(tile.xy+tap*tile.zw)*weight.x*weight.y;
    }
    vec2 ray = clip.xy*parameters.z;
    float receiver = projected.w*parameters.y*sqrt(1.0+dot(ray,ray));
    return finiteLightMomentVisibility(moments,receiver,parameters.x);
}

// Medium samples have no receiver plane or surface-normal bias. Derivatives
// inside a divergent ray march would corrupt the surface PCF correction.
float environmentLocalVolumeVisibility(int index, vec3 position) {
    vec4 parameters=WORLD_LOCAL_SHADOW(index*6+4);
    if (parameters.x<=0.0) return 1.0;
    vec4 projected=WORLD_LOCAL_SHADOW(index*6)*position.x +
        WORLD_LOCAL_SHADOW(index*6+1)*position.y +
        WORLD_LOCAL_SHADOW(index*6+2)*position.z + WORLD_LOCAL_SHADOW(index*6+3);
    if (parameters.w < 0.0) return environmentLocalMomentVisibility(index,projected,parameters);
    if (projected.w<=.00000001) return 1.0;
    vec3 clip=projected.xyz/projected.w;
    if (max(abs(clip.x),max(abs(clip.y),abs(clip.z)))>=1.0) return 1.0;
    vec2 uv=clip.xy*vec2(.5,WORLD_SHADOW_Y*.5)+vec2(.5,.5);
    vec2 texel=uv/parameters.w-vec2(.5,.5),base=floor(texel),fraction=texel-base;
    vec4 tile=WORLD_LOCAL_SHADOW(index*6+5);
    float visibility=0.0;
    for (int y=0;y<2;++y) for (int x=0;x<2;++x) {
        vec2 tap=(base+vec2(float(x),float(y))+vec2(.5,.5))*parameters.w;
        float depth=min(tap.x,tap.y)<0.0 || max(tap.x,tap.y)>1.0 ? 1.0 :
            WORLD_SHADOW_SAMPLE(tile.xy+tap*tile.zw);
        vec2 weight=mix(vec2(1.0,1.0)-fraction,fraction,vec2(float(x),float(y)));
        visibility+=step(clip.z*.5+.5-parameters.y,depth)*weight.x*weight.y;
    }
    return mix(1.0,visibility,parameters.x);
}

float environmentLocalShadowVisibility(int index, vec3 position, vec3 normal) {
    vec4 parameters = WORLD_LOCAL_SHADOW(index*6+4);
    if (parameters.x <= 0.0 || ENV_SUN.w < 0.5) return 1.0;
    if (parameters.w < 0.0) {
        vec4 radialClip = WORLD_LOCAL_SHADOW(index*6)*position.x +
            WORLD_LOCAL_SHADOW(index*6+1)*position.y +
            WORLD_LOCAL_SHADOW(index*6+2)*position.z + WORLD_LOCAL_SHADOW(index*6+3);
        return environmentLocalMomentVisibility(index,radialClip,parameters);
    }
    float n2 = dot(normal,normal);
    if (n2 > 0.00000001) {
        normal *= inversesqrt(n2);
        float signToLight = dot(normal, ENV_RECT(index*4).xyz-position) < 0.0 ? -1.0 : 1.0;
        position += normal * (signToLight * parameters.z);
    }
    vec4 projected = WORLD_LOCAL_SHADOW(index*6)*position.x +
        WORLD_LOCAL_SHADOW(index*6+1)*position.y +
        WORLD_LOCAL_SHADOW(index*6+2)*position.z + WORLD_LOCAL_SHADOW(index*6+3);
    if (projected.w <= 0.00000001) return 1.0;
    vec3 clip = projected.xyz/projected.w;
    vec2 uv = clip.xy*vec2(0.5,WORLD_SHADOW_Y*0.5)+vec2(0.5,0.5);
    float referenceDepth = clip.z*0.5+0.5-parameters.y;
    vec3 dx = dFdx(vec3(uv,referenceDepth)), dy = dFdy(vec3(uv,referenceDepth));
    float determinant = dx.x*dy.y-dx.y*dy.x;
    vec2 gradient = vec2(0.0,0.0);
    if (abs(determinant)>0.00000000000000000001)
        gradient=vec2(dy.y*dx.z-dx.y*dy.z,dx.x*dy.z-dy.x*dx.z)/determinant;
    if (max(abs(clip.x),max(abs(clip.y),abs(clip.z))) >= 1.0) return 1.0;
    vec4 tile = WORLD_LOCAL_SHADOW(index*6+5);
    vec2 centre = (floor(uv/parameters.w)+vec2(0.5,0.5))*parameters.w;
    float visibility = 0.0;
    for (int y=-1;y<=1;++y) {
        for (int x=-1;x<=1;++x) {
            vec2 tap=centre+vec2(float(x),float(y))*parameters.w;
            float depth=min(tap.x,tap.y)<0.0 || max(tap.x,tap.y)>1.0 ? 1.0 :
                WORLD_SHADOW_SAMPLE(tile.xy+tap*tile.zw);
            float receiver=referenceDepth+clamp(dot(gradient,tap-uv),-0.01,0.01);
            visibility+=step(receiver,depth);
        }
    }
    return mix(1.0,visibility/9.0,parameters.x);
}
