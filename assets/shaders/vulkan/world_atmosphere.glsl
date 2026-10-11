// Ground-level single-scattering atlas: 32 sun elevations, 64 azimuths,
// 32 view elevations. RGB stores sqrt(linear radiance / 16).
// One dominant directional source keeps the established material cost bounded.
// Solar and lunar radiance fade at their horizons before the source switches.
float environmentDirectionalVisibility = 1.0;
bool environmentMoonIsPrimary() {
    return max(ENV_MOON_LIGHT.r, max(ENV_MOON_LIGHT.g, ENV_MOON_LIGHT.b)) >
           max(ENV_RADIANCE.r, max(ENV_RADIANCE.g, ENV_RADIANCE.b));
}
vec3 environmentLightDirection() {
    return environmentMoonIsPrimary() ? ENV_MOON.xyz : ENV_SUN.xyz;
}
vec3 environmentLightRadiance() {
    return (environmentMoonIsPrimary() ? ENV_MOON_LIGHT.rgb : ENV_RADIANCE.rgb) * environmentDirectionalVisibility;
}

uint celestialHash(uint seed) {
    seed ^= seed >> 16u;
    seed *= 2146121005u;
    seed ^= seed >> 15u;
    seed *= 2221713035u;
    return seed ^ (seed >> 16u);
}
float celestialRandom(uint seed) { return float(celestialHash(seed) & 65535u) / 65535.0; }

vec3 environmentDitherSky(vec3 targetColor, vec3 ray) {
    // The current scene target is 8-bit even before its display transfer.
    // Sub-LSB, world-ray noise makes the dark gradient quantize without bands;
    // use the same seed across APIs and camera-centred sky geometry.
    ray = normalize(ray);
    uint seed = uint((ray.x+1.0)*8192.0) + uint((ray.y+1.0)*8192.0)*16411u +
                uint((ray.z+1.0)*8192.0)*269336921u;
    float noise = (celestialRandom(seed)-0.5)/255.0;
    return clamp(targetColor+vec3(noise,noise,noise),0.0,1.0);
}

vec3 environmentStars(vec3 ray) {
    // Cube-face cells avoid polar singularities and backend-dependent sine hashes.
    vec3 magnitude = abs(ray);
    vec2 plane;
    uint face;
    if (magnitude.x >= magnitude.y && magnitude.x >= magnitude.z) {
        plane = ray.zy / magnitude.x;
        face = ray.x > 0.0 ? 0u : 1u;
    } else if (magnitude.y >= magnitude.z) {
        plane = ray.xz / magnitude.y;
        face = ray.y > 0.0 ? 2u : 3u;
    } else {
        plane = ray.xy / magnitude.z;
        face = ray.z > 0.0 ? 4u : 5u;
    }
    vec2 grid = (plane + 1.0) * 64.0;
    vec2 cell = floor(grid);
    uint seed = celestialHash(uint(cell.x) + uint(cell.y)*131u + face*17161u);
    float selected = celestialRandom(seed);
    vec2 center = vec2(celestialRandom(seed+1u), celestialRandom(seed+2u)) * 0.6 + 0.2;
    float radius = mix(0.045, 0.12, celestialRandom(seed+3u));
    float footprint = max(length(fwidth(grid)), 0.01);
    float distanceToStar = length(grid-cell-center);
    float star = 1.0-smoothstep(max(radius-footprint*0.5, 0.0), radius+footprint*0.5, distanceToStar);
    star *= min(1.0, radius*radius / max(footprint*footprint*0.25, 0.0001));
    star *= selected > 0.991 ? mix(0.25, 1.0, celestialRandom(seed+4u)) : 0.0;
    return mix(vec3(0.62,0.76,1.0), vec3(1.0,0.84,0.65), celestialRandom(seed+5u)) * star;
}

float lunarCrater(vec2 p, vec2 center, float radius) {
    float distanceToCenter = length(p-center)/radius;
    float basin = 1.0-smoothstep(0.4,0.85,distanceToCenter);
    float rim = exp(-pow((distanceToCenter-0.88)*14.0,2.0));
    return -0.15*basin + 0.10*rim;
}

vec3 environmentMoon(vec3 ray) {
    float radius = ENV_MOON.w;
    vec3 direction = ENV_MOON.xyz;
    vec3 axis = abs(direction.y) < 0.98 ? vec3(0,1,0) : vec3(1,0,0);
    vec3 right = normalize(cross(axis,direction));
    vec3 up = cross(direction,right);
    vec2 p = vec2(dot(ray,right),dot(ray,up)) / max(sin(radius),0.00001);
    float radial = length(p);
    float edge = max(fwidth(radial),0.001);
    float disk = (1.0-smoothstep(1.0-edge,1.0+edge,radial)) *
        smoothstep(0.0,0.04,direction.y) * step(0.00001,radius) * step(0.0,dot(ray,direction));
    if (disk <= 0.0) return vec3(0.0,0.0,0.0);
    float z = sqrt(max(1.0-dot(p,p),0.0));
    float phaseZ = ENV_MOON_LIGHT.w*2.0-1.0;
    float illumination = max(p.x*sqrt(max(1.0-phaseZ*phaseZ,0.0)) + z*phaseZ,0.0);
    float maria = exp(-dot(p-vec2(-.25,.20),p-vec2(-.25,.20))*5.0) +
                  0.5*exp(-dot(p-vec2(.38,-.12),p-vec2(.38,-.12))*12.0);
    float albedo = .82-.25*maria;
    albedo += lunarCrater(p,vec2(.30,-.43),.22) + lunarCrater(p,vec2(-.40,-.23),.14) +
              lunarCrater(p,vec2(.10,.48),.12) + lunarCrater(p,vec2(-.48,.46),.09);
    return ENV_MOON_LIGHT.rgb * mix(vec3(0.48,0.52,0.64),vec3(1.0,.95,.86),illumination) *
        albedo * (0.025+illumination*12.0) * disk;
}

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
    float disk = smoothstep(cos(ENV_CELESTIAL.x) - edge, cos(ENV_CELESTIAL.x) + edge, alignment);
    disk *= smoothstep(-0.01, 0.005, sun.y) * step(0.00001,ENV_CELESTIAL.x);
    float halo = exp(-max(1.0-alignment,0.0) / max(ENV_CELESTIAL.z*ENV_CELESTIAL.z*0.5,0.000001));
    halo *= ENV_CELESTIAL.y * smoothstep(0.0,0.02,sun.y);
    vec3 stars = vec3(0.0,0.0,0.0);
    if (daylight < 1.0 && ENV_CELESTIAL.w > 0.0)
        stars = environmentStars(ray) * ENV_CELESTIAL.w * (1.0-daylight) * smoothstep(0.02,0.25,ray.y);
    vec3 moon = environmentMoon(ray);
    return scattering * daylight + night * (1.0-daylight) + ENV_RADIANCE.rgb * (disk*12.0+halo) +
           moon + stars * (1.0-clamp(max(moon.r,max(moon.g,moon.b)),0.0,1.0));
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
