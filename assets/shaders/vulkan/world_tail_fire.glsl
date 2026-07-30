struct TailFireMaterialState {
    vec4 timingFlagsAtlas;
    vec4 rect0;
    vec4 rect1;
    vec4 flipbook0;
    vec4 flipbook1;
};

float tailFireHash11(float value) {
    return fract(sin(value * 12.9898) * 43758.5453);
}

float tailFireHash21(vec2 value) {
    return fract(sin(dot(value, vec2(127.1, 311.7))) * 43758.5453);
}

float tailFireValueNoise(vec2 point) {
    vec2 cell = floor(point);
    vec2 local = fract(point);
    vec2 blend = local * local * (3.0 - 2.0 * local);
    float a = tailFireHash21(cell);
    float b = tailFireHash21(cell + vec2(1.0, 0.0));
    float c = tailFireHash21(cell + vec2(0.0, 1.0));
    float d = tailFireHash21(cell + vec2(1.0, 1.0));
    return mix(mix(a, b, blend.x), mix(c, d, blend.x), blend.y);
}

float tailFireSmoothFlicker(float time, float seed) {
    float point = time * 9.0 + seed * 97.0;
    float cell = floor(point);
    float local = fract(point);
    local = local * local * (3.0 - 2.0 * local);
    return mix(tailFireHash11(cell), tailFireHash11(cell + 1.0), local);
}

float tailFireFbm(vec2 point) {
    float value = 0.0;
    float amplitude = 0.5;
    for (int octave = 0; octave < 5; ++octave) {
        value += amplitude * tailFireValueNoise(point);
        point *= 2.02;
        amplitude *= 0.5;
    }
    return value;
}

vec2 tailFireFbmGradient(vec2 point) {
    const float epsilon = 0.03;
    float x = tailFireFbm(point + vec2(epsilon, 0.0)) -
              tailFireFbm(point - vec2(epsilon, 0.0));
    float y = tailFireFbm(point + vec2(0.0, epsilon)) -
              tailFireFbm(point - vec2(0.0, epsilon));
    return vec2(x, y) / (2.0 * epsilon);
}

vec2 tailFireCurl(vec2 point) {
    vec2 gradient = tailFireFbmGradient(point);
    return vec2(gradient.y, -gradient.x);
}

vec2 tailFireAdvect(vec2 point, float flowY, float amount) {
    vec2 curl1 = tailFireCurl(point * 1.30 + vec2(0.0, -flowY * 0.10));
    vec2 curl2 = tailFireCurl(point * 2.70 + vec2(3.1, -flowY * 0.18));
    return point + (curl1 * 0.65 + curl2 * 0.35) * amount;
}

vec3 tailFireTonemapSoft(vec3 color) {
    return color / (vec3(1.0) + color);
}

vec2 tailFireClampUvToRegion(
    vec2 localUv,
    vec4 rectUv,
    TailFireMaterialState materialState) {
    vec2 atlasSize = max(materialState.timingFlagsAtlas.zw, vec2(1.0));
    vec2 rectPixels = max(rectUv.zw * atlasSize, vec2(1.0));
    vec2 minPixel = vec2(0.5) / atlasSize;
    vec2 maxPixel = (rectPixels - vec2(0.5)) / atlasSize;
    vec2 clampedLocalUv = clamp(localUv, vec2(0.0), vec2(1.0));
    vec2 regionUv = rectUv.xy + clampedLocalUv * rectUv.zw;
    return rectUv.xy + clamp(regionUv - rectUv.xy, minPixel, maxPixel);
}

vec4 sampleTailFireAtlas(sampler2D baseColorMap,
                         TailFireMaterialState materialState,
                         vec4 rectUv,
                         vec2 grid,
                         float frames,
                         float fps,
                         vec2 localUv,
                         float seed,
                         float time,
                         bool coherentPlayback) {
    float speed = coherentPlayback
        ? 1.0
        : mix(0.85, 1.10, tailFireHash11(seed * 31.7 + 2.3));
    float phase = coherentPlayback ? 0.0 : seed * frames;
    float frame = mod(floor(time * fps * speed + phase), max(1.0, frames));
    float columns = max(1.0, grid.x);
    float rows = max(1.0, grid.y);
    float column = mod(frame, columns);
    float rowFromTop = floor(frame / columns);
    float row = (rows - 1.0) - rowFromTop;
    vec2 cellLocalUv = (vec2(column, row) + localUv) / vec2(columns, rows);
    return texture(
        baseColorMap,
        tailFireClampUvToRegion(cellLocalUv, rectUv, materialState));
}

vec4 sampleTailFireAtlasTopLeft(sampler2D baseColorMap,
                                TailFireMaterialState materialState,
                                vec4 rectUv,
                                vec2 grid,
                                float frames,
                                float fps,
                                vec2 localUv,
                                float time) {
    float frame = mod(floor(time * fps), max(1.0, frames));
    float columns = max(1.0, grid.x);
    float rows = max(1.0, grid.y);
    float column = mod(frame, columns);
    float row = floor(frame / columns);
    vec2 cellLocalUv = (vec2(column, row) + localUv) / vec2(columns, rows);
    return texture(
        baseColorMap,
        tailFireClampUvToRegion(cellLocalUv, rectUv, materialState));
}

vec4 evaluateAuthoredTailFire(
    sampler2D baseColorMap,
    TailFireMaterialState materialState) {
    vec4 flipbook0 = materialState.flipbook0;
    vec2 uv = clamp(
        vertexUv + materialState.flipbook1.xy,
        vec2(0.0),
        vec2(1.0));
    vec4 baked = sampleTailFireAtlasTopLeft(
        baseColorMap,
        materialState,
        materialState.rect0,
        flipbook0.xy,
        flipbook0.z,
        flipbook0.w,
        uv,
        materialState.timingFlagsAtlas.x);
    float rgbCoverage = smoothstep(0.03, 0.20, max(baked.r, max(baked.g, baked.b)));
    baked.a = max(baked.a, rgbCoverage);
    float baseEngulf = 1.0 - smoothstep(0.0, 0.28, clamp(vertexGenerated.y, 0.0, 1.0));
    vec2 centerXZ = vertexGenerated.xz - vec2(0.5);
    float centerDistance = length(centerXZ * vec2(1.2, 1.0));
    float coreMask = 1.0 - smoothstep(0.0, 0.23, centerDistance);
    float tipHideMask = baseEngulf * coreMask;
    float warmMask =
        smoothstep(0.68, 0.98, baked.r) *
        smoothstep(0.56, 0.90, baked.g) *
        (1.0 - smoothstep(0.22, 0.58, baked.b));
    baked.rgb = mix(baked.rgb, vec3(1.0, 0.68, 0.16), warmMask * 0.44);
    baked.rgb = mix(baked.rgb, vec3(1.0, 0.82, 0.30), tipHideMask * 0.55);
    baked.a = max(baked.a, baseEngulf * 0.95);
    baked.a = max(baked.a, tipHideMask);
    if (baked.a <= 0.08) discard;
    baked.a = 1.0;
    return baked;
}

float tailFireLickBlobs(float x, float y, vec2 advectedPoint, float flowY, float seed) {
    float point = y * 6.6 + flowY * 0.55;
    float segment = floor(point);
    float local = fract(point);
    float center1 = (tailFireHash11(segment + seed * 31.0) - 0.5) * 0.95 * (1.0 - y);
    float center2 = (tailFireHash11(segment + seed * 73.0) - 0.5) * 0.95 * (1.0 - y);
    float width = mix(0.34, 0.085, y);
    vec2 q1 = vec2((x - center1) / width, (local - 0.30) / 0.70);
    vec2 q2 = vec2((x - center2) / (width * 0.85), (local - 0.45) / 0.65);
    float mask1 = 1.0 - smoothstep(0.60, 1.00, length(q1 * vec2(1.0, 1.45)));
    float mask2 = 1.0 - smoothstep(0.60, 1.00, length(q2 * vec2(1.0, 1.60)));
    float breakup = tailFireFbm(advectedPoint * vec2(7.0, 12.0) + seed * 17.0);
    float broken = smoothstep(0.25, 0.88, breakup);
    float gate = smoothstep(0.05, 0.22, y) * (1.0 - smoothstep(0.86, 1.0, y));
    return clamp((mask1 + 0.85 * mask2) * broken * gate, 0.0, 1.0);
}

vec4 evaluateTailFire(
    sampler2D baseColorMap,
    TailFireMaterialState materialState) {
    float age = clamp(vertexColor.r, 0.0, 1.0);
    float seed = clamp(vertexColor.g, 0.0, 1.0);
    float time = materialState.timingFlagsAtlas.x;
    vec2 uv = vertexUv;
    vec2 centered = (uv - 0.5) * 2.0;
    float x = centered.x;
    float y = clamp(uv.y, 0.0, 1.0);
    float bottomFade = smoothstep(0.00, 0.11, y);

    float baseT = smoothstep(0.00, 0.22, y);
    float xScaleBase = mix(2.55, 1.90, baseT);
    float yScaleBase = mix(1.05, 0.75, baseT);
    float baseRadius = length(vec2(centered.x * xScaleBase, centered.y * yScaleBase));
    float radialMaskBase = 1.0 - smoothstep(0.98, 1.10, baseRadius);
    float tightMask = 1.0 - smoothstep(0.62, 0.88, baseRadius);
    float looseRadius = length(centered * vec2(0.55, 0.85));
    float radialMaskLoose = 1.0 - smoothstep(0.98, 1.20, looseRadius);

    float fade = pow(mix(1.0 - age, 1.0, 0.25), 0.75);
    vec2 wobble = vec2(
        tailFireSmoothFlicker(time * 0.9, seed + 0.17),
        tailFireSmoothFlicker(time * 1.1, seed + 0.73)) - 0.5;
    vec4 flipbook1Sample = vec4(1.0);
    vec4 flipbook2Sample = vec4(1.0);
    int fireFlags = int(materialState.timingFlagsAtlas.y + 0.5);
    bool hasFirstTexture = (fireFlags & 1) != 0;
    bool hasSecondTexture = (fireFlags & 2) != 0;
    if ((fireFlags & 8) != 0) {
        return evaluateAuthoredTailFire(baseColorMap, materialState);
    }

    float wobbleScale1 = hasSecondTexture ? 0.010 : 0.0009;
    float wobbleScale2 = hasSecondTexture ? 0.002 : 0.0002;
    if (hasFirstTexture) {
        vec4 flipbook0 = materialState.flipbook0;
        flipbook1Sample = sampleTailFireAtlas(
            baseColorMap,
            materialState,
            materialState.rect0,
            flipbook0.xy,
            flipbook0.z,
            flipbook0.w,
            uv + wobble * wobbleScale1,
            seed,
            time,
            !hasSecondTexture);
        if (hasSecondTexture) {
            vec4 flipbook1 = materialState.flipbook1;
            flipbook2Sample = sampleTailFireAtlas(
                baseColorMap,
                materialState,
                materialState.rect1,
                flipbook1.xy,
                flipbook1.z,
                flipbook1.w,
                uv + wobble * wobbleScale2,
                seed,
                time,
                false);
        } else {
            flipbook2Sample = flipbook1Sample;
        }
    }

    if (hasFirstTexture && !hasSecondTexture) {
        vec4 directSample = sampleTailFireAtlas(
            baseColorMap,
            materialState,
            materialState.rect0,
            materialState.flipbook0.xy,
            materialState.flipbook0.z,
            materialState.flipbook0.w,
            vec2(uv.x, 1.0 - uv.y),
            seed,
            time,
            true);
        float alpha = clamp(directSample.a, 0.0, 1.0) * bottomFade * fade;
        vec3 rgb = clamp(directSample.rgb * 1.15, 0.0, 1.0);
        alpha = clamp(alpha, 0.0, 0.985);
        if (alpha < 0.003) discard;
        return vec4(rgb * alpha, alpha);
    }

    float firstAlpha = clamp(flipbook1Sample.a, 0.0, 1.0);
    float firstLuminance = clamp(dot(flipbook1Sample.rgb, vec3(0.3333)), 0.0, 1.0);
    float speed = hasSecondTexture ? mix(0.95, 1.10, tailFireHash11(seed * 19.31)) : 1.0;
    float flow = time * 1.55 * speed;
    float flowY = flow * mix(0.75, 1.55, y * y);
    float width = mix(0.30, 0.055, pow(y, 2.35)) * 2.80;
    float remappedY = ((y * 2.0 - 1.0) * 1.45 + 0.38) / 1.12;
    vec2 point = vec2(x / width, remappedY) * 1.22;
    float sway = tailFireFbm(
        vec2(x * 1.7, y * 3.8) + vec2(0.0, -flowY * 0.65) + seed * 7.0);
    point.x += (sway - 0.5) * (hasSecondTexture ? 0.015 : 0.004) * (1.0 - y);
    float distance0 = length(point);
    vec2 advectedPoint = tailFireAdvect(point * vec2(1.20, 1.0) + seed * 6.0, flowY, 0.25);
    float noise = tailFireFbm(advectedPoint * vec2(2.7, 4.5) + seed * 11.0);
    float distance = distance0 + (noise - 0.5) * 0.18 * (1.0 - y);
    float core = clamp(1.0 - smoothstep(0.00, 0.88, distance), 0.0, 1.0);
    float outer = clamp(1.0 - smoothstep(0.30, 1.05, distance), 0.0, 1.0);
    float blobs = tailFireLickBlobs(x, y, advectedPoint, flowY, seed);
    float body = clamp(smoothstep(0.92, 0.12, distance), 0.0, 1.0);

    float proceduralAlpha = body * (0.60 + 0.55 * blobs);
    float calmFlicker = tailFireSmoothFlicker(time * 1.2, seed);
    proceduralAlpha *= hasSecondTexture
        ? 0.92 + 0.15 * calmFlicker
        : 0.985 + 0.03 * calmFlicker;
    proceduralAlpha *= bottomFade * fade;
    proceduralAlpha = clamp(1.0 - exp(-proceduralAlpha * 1.85), 0.0, 0.96);

    vec3 yellow = vec3(1.70, 1.20, 0.28);
    vec3 red = vec3(1.45, 0.18, 0.06);
    vec3 orange = vec3(1.60, 0.55, 0.12);
    float wave = 0.5 + 0.5 * sin((x * 1.8 + y * 8.5 - flowY * 4.9) + seed * 7.0);
    float segmentPoint = y * 6.0 - flowY * 0.55;
    float segment = floor(segmentPoint);
    float segmentRandom = tailFireHash11(segment + seed * 71.3);
    float segmentRandom2 = tailFireHash11(segment + seed * 19.7 + 5.0);
    float triangle1 = abs(fract((x * 0.85 + y * 1.05 - flowY * 0.18) * 2.8 + seed * 7.0) - 0.5) * 2.0;
    float triangle2 = abs(fract((x * 1.10 - y * 0.60 - flowY * 0.14) * 3.8 + seed * 3.0) - 0.5) * 2.0;
    float zig = smoothstep(0.15, 0.85, mix(
        triangle1,
        triangle2,
        0.50 + 0.50 * (segmentRandom - 0.5)));
    float warp = tailFireFbm(
        tailFireAdvect(vec2(x * 0.85, y * 1.2) + seed * 6.0, flowY, 0.22) *
        vec2(4.5, 7.5)) - 0.5;
    float jag =
        (segmentRandom - 0.5) * 0.10 +
        (segmentRandom2 - 0.5) * 0.05 +
        (zig - 0.5) * 0.14 +
        warp * 0.06;
    jag *= 1.0 - 0.55 * smoothstep(0.65, 1.0, y);
    float boundary = clamp(0.34 + jag, 0.14, 0.62);
    float redMask = smoothstep(boundary, boundary + 0.11, y);
    vec3 proceduralRgb = mix(yellow, red, redMask);
    float band = smoothstep(boundary - 0.02, boundary + 0.02, y) *
                 (1.0 - smoothstep(boundary + 0.02, boundary + 0.10, y));
    proceduralRgb = mix(proceduralRgb, orange, 0.55 * band);
    float climb = core * (1.0 - smoothstep(0.55, 0.95, y)) * (0.35 + 0.65 * wave);
    proceduralRgb = mix(proceduralRgb, yellow, 0.18 * climb);
    proceduralRgb *= 1.18 + 0.35 * outer;

    vec3 hybridRgb = proceduralRgb;
    float hybridAlpha = proceduralAlpha;
    if (hasFirstTexture) {
        hybridAlpha = clamp(hybridAlpha * mix(0.55, 1.65, firstAlpha), 0.0, 0.96);
        hybridRgb *= mix(0.85, 1.25, firstLuminance);
        hybridRgb *= mix(vec3(1.0), flipbook1Sample.rgb * 1.35, 0.30);
    }

    vec3 secondRgb = flipbook2Sample.rgb;
    float secondAlpha = pow(clamp(flipbook2Sample.a, 0.0, 1.0), 0.66);
    float hot = smoothstep(0.10, 0.55, 1.0 - y);
    secondRgb *= mix(red, yellow, hot) * 1.30;
    secondAlpha *= tightMask * bottomFade;

    vec3 rgb = mix(hybridRgb, secondRgb, 0.50);
    float alpha = mix(
        hybridAlpha * radialMaskLoose * bottomFade,
        secondAlpha * radialMaskBase,
        0.50);
    alpha = clamp(alpha * fade + 0.10 * outer * fade, 0.0, 0.985);
    rgb *= 2.60;
    float emissive = (0.85 * outer + 0.45 * core) * fade;
    rgb *= 1.0 + 2.10 * emissive;
    rgb = tailFireTonemapSoft(rgb);
    if (alpha < 0.003) discard;
    return vec4(rgb * alpha, alpha);
}
