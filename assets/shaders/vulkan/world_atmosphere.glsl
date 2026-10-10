// Ground-level single-scattering atlas: 32 sun elevations, 64 azimuths,
// 32 view elevations. RGB stores sqrt(linear radiance / 16).
vec3 environmentSky(vec3 ray) {
    ray = normalize(ray);
    vec3 sun = ENV_SUN.xyz;
    float elevation = asin(clamp(ray.y, 0.0, 1.0));
    float sunElevation = asin(clamp(sun.y, -1.0, 1.0));
    vec2 rayHorizontal = ray.xz / max(length(ray.xz), 0.0001);
    vec2 sunHorizontal = sun.xz / max(length(sun.xz), 0.0001);
    float azimuth = acos(clamp(dot(rayHorizontal, sunHorizontal), -1.0, 1.0));
    float layer = clamp((sunElevation + 0.14) / (1.5707963 + 0.14), 0.0, 1.0) * 31.0;
    float lower = floor(layer);
    float column = azimuth / 3.14159265 * 63.0 + 0.5;
    float row = sqrt(elevation / 1.5707963) * 31.0 + 0.5;
    vec3 a = ENV_LUT(vec2((lower * 64.0 + column) / 2048.0, row / 32.0));
    vec3 b = ENV_LUT(vec2((min(lower + 1.0, 31.0) * 64.0 + column) / 2048.0, row / 32.0));
    vec3 scattering = mix(a*a, b*b, layer-lower) * 16.0 * ENV_HEIGHT.w;
    float daylight = smoothstep(-0.14, 0.02, sun.y);
    vec3 night = ENV_NIGHT.rgb * mix(1.8, 0.65, pow(max(ray.y, 0.0), 0.35));
    float alignment = dot(ray, sun);
    float edge = max(fwidth(alignment), 0.0000005);
    float disk = smoothstep(cos(0.00465) - edge, cos(0.00465) + edge, alignment);
    disk *= smoothstep(-0.01, 0.005, sun.y);
    return scattering * daylight + night * (1.0-daylight) + ENV_RADIANCE.rgb * disk * 12.0;
}

vec3 environmentHaze(vec3 color, vec3 position, vec3 camera) {
    if (ENV_SUN.w < 0.5 || ENV_HAZE.w <= 0.0) return color;
    float distanceMetres = length(position-camera);
    float startHeight = max(camera.y-ENV_HEIGHT.x, 0.0);
    float endHeight = max(position.y-ENV_HEIGHT.x, 0.0);
    // Integrate exponential height density along the view segment, with a
    // stable horizontal limit; retain near-surface density below base height.
    float a = clamp(startHeight*ENV_HEIGHT.y, 0.0, 40.0);
    float b = clamp(endHeight*ENV_HEIGHT.y, 0.0, 40.0);
    float difference = b-a;
    float densityIntegral = abs(difference) < 0.001
        ? exp(-0.5*(a+b)) : (exp(-a)-exp(-b))/difference;
    float opacity = min(1.0-exp(-ENV_HAZE.w*distanceMetres*densityIntegral), ENV_HEIGHT.z);
    return mix(color, ENV_HAZE.rgb, opacity);
}
