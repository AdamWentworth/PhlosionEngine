#include "world_fog.glsl"
vec3 environmentGlobalSpecularTint() {
#ifdef ENV_SKY
    if(ENV_SUN.w>.5 && ENV_SKY(21).w>.5)return ENV_SKY(21).rgb;
#endif
    return vec3(1.0,1.0,1.0);
}
vec3 worldFogColor(vec3 color,vec3 position) {
    vec4 fog=worldFogAt(position);
#ifdef WORLD_FOG_VIEW
    #ifndef WORLD_FOG_ADDITIVE
#define WORLD_FOG_ADDITIVE (WORLD_FOG_VIEW(2).w>.5)
#endif
    if(WORLD_FOG_ADDITIVE)return fog.a*color;
#endif
    return fog.rgb+fog.a*color;
}
vec4 worldFogTransparent(vec4 color,vec3 position) {
    return vec4(worldFogColor(color.rgb,position),color.a);
}

// Ground-level single-scattering atlas: 32 sun elevations, 64 azimuths,
// 32 view elevations. RGB stores sqrt(linear radiance / 16).
// One dominant directional source keeps the established material cost bounded.
// Solar and lunar radiance fade at their horizons before the source switches.
float environmentCloudShadow(vec3 position) {
#ifdef WORLD_CLOUD
    if(WORLD_CLOUD(0).w<.5)return 1.0;
    vec2 uv=(position.xz-WORLD_CLOUD(0).xy-(position.y-WORLD_CLOUD(0).z)*WORLD_CLOUD(1).xy)/WORLD_CLOUD(1).w+.5;
    return 1.0-clamp(WORLD_CLOUD_SHADOW(uv),0.0,1.0)*WORLD_CLOUD(1).z;
#else
    return 1.0;
#endif
}
vec4 environmentResolvedClouds(vec3 ray,vec3 camera) {
#ifdef WORLD_CLOUD
    if(WORLD_CLOUD(6).z<.5)return vec4(0,0,0,1);
    vec4 p=vec4(camera+ray*10000.0,1);
    vec4 h=vec4(dot(WORLD_CLOUD(2),p),dot(WORLD_CLOUD(3),p),dot(WORLD_CLOUD(4),p),dot(WORLD_CLOUD(5),p));
    if(h.w<=0.0)return vec4(0,0,0,1);
    vec2 uv=h.xy/h.w*vec2(.5,-.5)+.5;
    if(min(uv.x,uv.y)<0.0||max(uv.x,uv.y)>1.0)return vec4(0,0,0,1);
    return WORLD_CLOUD_COLOR(uv);
#else
    return vec4(0,0,0,1);
#endif
}
#if defined(SKY_CLOUD) && defined(ENV_SKY)
SKY_CLOUD_PRIVATE vec3 skyCloudCamera;
vec4 skyCloudParameter(int i) {
    vec4 p=SKY_CLOUD(i);
    if(i==2)p.xyz=skyCloudCamera;
    if(i==10)p.w=0.0; // Probe radiance excludes the primary camera's fog history.
    return p;
}
vec4 skyCloudAtlas(vec2 uv,vec2 origin,float size) {
    vec2 p=fract(uv)*size-.5,base=floor(p),f=fract(p);
    vec2 a=fract((base+size)/size)*size,b=fract((base+vec2(1,1)+size)/size)*size;
    vec2 dimensions=vec2(2048,2112);
    return mix(mix(ENV_ATLAS_RGBA((origin+a+.5)/dimensions),ENV_ATLAS_RGBA((origin+vec2(b.x,a.y)+.5)/dimensions),f.x),
               mix(ENV_ATLAS_RGBA((origin+vec2(a.x,b.y)+.5)/dimensions),ENV_ATLAS_RGBA((origin+b+.5)/dimensions),f.x),f.y);
}
#define CP(i) skyCloudParameter(i)
#define CL_NOISE(uv) skyCloudAtlas(uv,vec2(512,512),512.0)
#define CL_WEATHER(uv) skyCloudAtlas(uv,vec2(0,640),256.0)
#define CL_SHADOW(uv) SKY_CLOUD_SHADOW(uv)
#define CL_LOOP SKY_CLOUD_LOOP
#include "world_cloud_integration.glsl"
vec4 environmentCaptureClouds(vec3 ray,vec3 camera) {
    if(SKY_CLOUD(3).w<.5)return vec4(0,0,0,1);
    skyCloudCamera=camera;return cloudMarch(ray,1e7,.5);
}
#undef CP
#undef CL_NOISE
#undef CL_WEATHER
#undef CL_SHADOW
#undef CL_LOOP
#endif
float environmentDirectionalVisibility = 1.0;
vec3 environmentLocalHaze(vec3 position, vec3 camera);
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
    // HDR keeps the sky unclipped and dithers at the final display boundary.
    if (WORLD_SCENE_RADIANCE) return max(targetColor,vec3(0.0,0.0,0.0));
    // Preserve the legacy world-ray dither for source-fidelity views.
    ray = normalize(ray);
    uint seed = uint((ray.x+1.0)*8192.0) + uint((ray.y+1.0)*8192.0)*16411u +
                uint((ray.z+1.0)*8192.0)*269336921u;
    float noise = (celestialRandom(seed)-0.5)/255.0;
    return clamp(targetColor+vec3(noise,noise,noise),0.0,1.0);
}


vec3 skyCoordinates(vec3 direction,vec2 lonLat){
    vec2 a=lonLat*.017453292519943295;
    float cosine=-direction.x*cos(a.x)-direction.y*sin(a.x);
    float sine=-direction.y*cos(a.x)+direction.x*sin(a.x);
    return vec3(sine,cos(a.y)*direction.z-sin(a.y)*cosine,sin(a.y)*direction.z+cos(a.y)*cosine);
}
vec3 skyStarTexture(vec3 ray){
    vec3 m=abs(ray);vec2 p;float face;
    if(m.x>=m.y&&m.x>=m.z){p=ray.zy/m.x;face=ray.x>0.0?0.0:1.0;}
    else if(m.y>=m.z){p=ray.xz/m.y;face=ray.y>0.0?2.0:3.0;}
    else{p=ray.xy/m.z;face=ray.z>0.0?4.0:5.0;}
    vec2 tile=vec2(face-floor(face/3.0)*3.0,floor(face/3.0))*514.0+vec2(1,1025);
    return ENV_LUT((tile+(p*.5+.5)*512.0)/vec2(2048,2112));
}
vec3 environmentStars(vec3 ray){
#ifdef ENV_SKY
    vec3 galactic=skyCoordinates(skyCoordinates(ray.xzy,vec2(ENV_SKY(15).x,ENV_SKY(13).w)),vec2(192.75,27.13));
    vec3 axis=normalize(skyCoordinates(vec3(0,0,1),vec2(192.75,27.13)));
    float angle=ENV_SKY(13).z*6.28318530718;
    galactic=galactic*cos(angle)+cross(axis,galactic)*sin(angle)+axis*dot(axis,galactic)*(1.0-cos(angle));
    vec3 sampleColor=skyStarTexture(galactic*vec3(1,1,-1));
    float corner=max(abs(galactic.x),max(abs(galactic.y),abs(galactic.z)));
    float mask=sampleColor.r*mix(1.0+ENV_SKY(17).w,1.0-ENV_SKY(17).w,corner);
    float exponent=mix(mix(ENV_SKY(17).x,ENV_SKY(17).y,sampleColor.g),ENV_SKY(17).z,clamp(sampleColor.b*100.0,0.0,1.0));
    float stars=sampleColor.g*pow(abs(mask),exponent)*ENV_CELESTIAL.w*1000.0*(512.0/ENV_SKY(18).y);
    float latitude=clamp(1.55-acos(clamp(galactic.z,-1.0,1.0))/1.5,0.0,1.0);
    float longitude=atan(galactic.x,galactic.y)/6.28318530718;
    vec3 galaxy=ENV_LUT((vec2(801,33)+fract(vec2(longitude,latitude))*vec2(512,256))/vec2(2048,2112));
    return (vec3(stars,stars,stars)+galaxy*ENV_SKY(18).x)*pow(clamp(ray.y,0.0,1.0),2.0);
#else
    return vec3(0,0,0);
#endif
}
vec3 environmentSkySunColor() {
#ifdef ENV_SKY
    return mix(ENV_RADIANCE.rgb,ENV_SKY(19).rgb,ENV_SKY(19).w);
#else
    return ENV_RADIANCE.rgb;
#endif
}
vec3 environmentSkyMoonColor() {
#ifdef ENV_SKY
    return mix(ENV_MOON_LIGHT.rgb,ENV_SKY(20).rgb,ENV_SKY(20).w);
#else
    return ENV_MOON_LIGHT.rgb;
#endif
}
vec3 environmentMoon(vec3 ray){
#ifdef ENV_SKY
    vec3 direction=ENV_MOON.xyz,axis=abs(direction.y)<.9999?vec3(0,1,0):vec3(0,0,1);
    vec3 right=normalize(cross(direction,axis)),up=normalize(cross(direction,right));
    float chord=2.0*sin(ENV_MOON.w*.5);
    vec2 uv=vec2(dot(up,ray),dot(right,ray))/max(chord*2.0,.000001)+.5;
    float mask=pow(max(chord-length(direction-ray),0.0),ENV_SKY(14).y);
    vec3 albedo=ENV_LUT((vec2(513,33)+clamp(uv,0.0,1.0)*256.0)/vec2(2048,2112));
    return albedo*environmentSkyMoonColor()*mask*clamp(ray.y/ENV_SKY(11).w,0.0,1.0)*10.0+
        pow(abs(dot(direction,ray)),ENV_SKY(14).w)*ENV_SKY(14).z*environmentSkyMoonColor();
#else
    return vec3(0,0,0);
#endif
}
vec3 environmentCloudSample(vec2 uv) {
    uv -= floor(uv);
    return ENV_LUT(vec2(1.0+uv.x*256.0,33.0+uv.y*256.0) / vec2(2048.0,2112.0));
}

vec4 environmentCloudLayer(vec3 ray, vec3 camera, bool upper) {
    float coverage = ENV_CLOUD_PARAMS.x;
    vec2 offset = upper ? ENV_CLOUD_OFFSETS.zw : ENV_CLOUD_OFFSETS.xy;
    float altitude = upper ? 1400.0 : 600.0;
    float scale = upper ? 0.065/1400.0 : 0.11/600.0;
    // A shallow curved layer keeps horizon intersections finite and smooth.
    // The stable quadratic form avoids cancellation at the zenith.
    float height = max(altitude-camera.y,1.0);
    float shell = height*(120000.0+height);
    float projected = 60000.0*max(ray.y,0.0);
    float distanceToLayer = shell/(projected+sqrt(projected*projected+shell));
    vec2 uv = (camera.xz+ray.xz*distanceToLayer)*scale-offset;
    vec3 field = environmentCloudSample(uv);
    float shape = upper ? field.g : field.r;
    float threshold = mix(0.84,0.12,coverage);
    float thickness = max(shape-threshold,0.0) * 8.0 * ENV_CLOUD_PARAMS.y;
    float alpha = (1.0-exp(-thickness)) * smoothstep(threshold-0.045,threshold+0.065,shape);
    alpha *= smoothstep(0.005,0.06,ray.y) * (upper ? 0.48 : 1.0);
    vec3 key = environmentLightDirection();
    vec2 lightOffset = key.xz * 0.028;
    vec3 facing = environmentCloudSample(uv+lightOffset);
    float neighbor = upper ? facing.g : facing.r;
    float edgeLight = clamp((shape-neighbor)*5.0+0.35,0.0,1.0);
    float silver = pow(max(dot(ray,key),0.0),12.0) * (1.0-alpha);
    vec3 keyRadiance = environmentMoonIsPrimary() ? ENV_MOON_LIGHT.rgb : ENV_RADIANCE.rgb;
    vec3 color = ENV_AMBIENT.rgb * mix(0.60,1.65,edgeLight) +
        keyRadiance * (0.07+edgeLight*0.17+silver*0.28) * ENV_CLOUD_TINT.w;
    color *= ENV_CLOUD_TINT.rgb;
    // Distant layers soften into the same horizon haze as the world below.
    color = mix(ENV_HAZE.rgb,color,smoothstep(0.02,0.22,ray.y));
    return vec4(color,alpha);
}


#ifdef ENV_SKY
#include "world_sky_model.glsl"
vec3 environmentBlackbody(float kelvin) {
    float k2=kelvin*kelvin;
    float u=(.860117757+1.54118254e-4*kelvin+1.28641212e-7*k2)/(1.0+8.42420235e-4*kelvin+7.08145163e-7*k2);
    float v=(.317398726+4.22806245e-5*kelvin+4.20481691e-8*k2)/(1.0-2.89741816e-5*kelvin+1.61456053e-7*k2);
    float den=2.0*u-8.0*v+4.0,x=3.0*u/den,y=2.0*v/den;
    vec3 xyz=vec3(x/y,1.0,(1.0-x-y)/y);
    return vec3(dot(vec3(3.2404542,-1.5371385,-.4985314),xyz),
        dot(vec3(-.9692660,1.8760108,.0415560),xyz),
        dot(vec3(.0556434,-.2040259,1.0572252),xyz))*pow(.0004*kelvin,4.0);
}
float environmentCelestialMask(vec3 ray,vec3 direction,float radius,float falloff) {
    return pow(max(2.0*sin(radius*.5)-length(direction-ray),0.0),falloff);
}
#endif

vec4 environmentCirrus(vec3 ray,vec3 sky){
#ifdef ENV_SKY
    if(ENV_SKY(15).y<=0.0)return vec4(0,0,0,1);
    float radius=6371000.0,altitude=ENV_SKY(15).w;
    float projected=radius*ray.y;
    float shell=altitude*(2.0*radius+altitude);
    float distance=shell/(projected+sqrt(projected*projected+shell));
    vec3 pointOnShell=vec3(0,radius,0)+ray*distance;
    vec2 uv=pointOnShell.xz/(10000.0*ENV_SKY(15).z);
    float field=environmentCloudSample(uv).g;
    float opacity=pow(clamp(field,0.0,1.0),1.0/ENV_SKY(15).y)*clamp(1.0-pow(1.0-ray.y,4.0),0.0,1.0);
    vec2 g=ENV_SKY(16).xy,cosine=vec2(clamp(dot(ray,ENV_SUN.xyz),-.999,.999),clamp(dot(ray,ENV_MOON.xyz),-.999,.999));
    // The source expression uses the integer exponent 3/2.
    vec2 phase=clamp((vec2(1,1)-g*g)/(vec2(1,1)+g*g-2.0*g*cosine)/12.56637061436,0.0,1.0)*2.0;
    float shadowBase=1.0-clamp(-dot(normalize(pointOnShell),ENV_SUN.xyz),0.0,1.0);
    float shadow=ENV_SKY(16).w>0.0?pow(shadowBase,100.0/ENV_SKY(16).w):(shadowBase>=1.0?1.0:0.0);
    vec3 ambient=sky;
#ifdef ENV_SKY_MODEL
    if(WORLD_CLOUD(8).w>.5)ambient=ENV_SKY_MODEL((vec2(96,0)+clamp((-.5*ray.xz/max(1.0+ray.y,.001)+.5)*16.0,vec2(.5,.5),vec2(15.5,15.5)))/vec2(128,64));
#endif
    vec3 energy=(environmentSkySunColor()*phase.x*shadow+environmentSkyMoonColor()*phase.y+ambient*ENV_SKY(16).z)*opacity;
    return vec4(energy,1.0-opacity);
#else
    return vec4(0,0,0,1);
#endif
}
vec3 environmentSky(vec3 ray, vec3 camera) {
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
    vec3 a = ENV_LUT(vec2((lower * 64.0 + column) / 2048.0, row / 2112.0));
    vec3 b = ENV_LUT(vec2((min(lower + 1.0, 31.0) * 64.0 + column) / 2048.0, row / 2112.0));
    vec3 scattering = mix(a*a, b*b, layer-lower) * 16.0 * ENV_HEIGHT.w;
#ifdef ENV_SKY
    scattering=environmentAnalyticSky(ray,sun)*ENV_SKY(10).rgb*ENV_HEIGHT.w;
#endif
    float daylight = smoothstep(-0.14, 0.02, sun.y);
    vec3 night = ENV_NIGHT.rgb * mix(1.8, 0.65, pow(max(ray.y, 0.0), 0.35));
#ifdef ENV_SKY
    night=environmentAnalyticSky(ray,sun)*ENV_NIGHT.rgb*100.0;
#endif
    float alignment = dot(ray, sun);
    float edge = max(fwidth(alignment), 0.0000005);
    float disk = smoothstep(cos(ENV_CELESTIAL.x) - edge, cos(ENV_CELESTIAL.x) + edge, alignment);
    disk *= smoothstep(-0.01, 0.005, sun.y) * step(0.00001,ENV_CELESTIAL.x);
    float halo = exp(-max(1.0-alignment,0.0) / max(ENV_CELESTIAL.z*ENV_CELESTIAL.z*0.5,0.000001));
    halo *= ENV_CELESTIAL.y * smoothstep(0.0,0.02,sun.y);
    vec3 stars = vec3(0.0,0.0,0.0);
    if (daylight < 1.0 && ENV_CELESTIAL.w > 0.0)
        stars = environmentStars(ray);
    vec3 moon = environmentMoon(ray);
#ifdef ENV_SKY
    float body=environmentCelestialMask(ray,sun,ENV_CELESTIAL.x,ENV_SKY(14).x);
    vec3 solar=environmentBlackbody(body*4000.0)*mix(vec3(1.0,1.0,1.0),vec3(1.0,.1,0.0),
        pow(max(1.0-ray.y,0.0),8.0))*environmentSkySunColor()*body*clamp(ray.y/ENV_SKY(11).w,0.0,1.0);
    disk=0.0;halo=0.0;
#else
    vec3 solar=vec3(0.0,0.0,0.0);
#endif
    vec3 sky = scattering * daylight + night * (1.0-daylight) + environmentSkySunColor() * (disk*12.0+halo) +
           solar + moon + stars * (1.0-clamp(max(moon.r,max(moon.g,moon.b)),0.0,1.0));
#ifdef ENV_SKY
    sky=ENV_SKY(18).z>.5?scattering:scattering+solar+moon+stars;
#endif
    vec4 cirrus=environmentCirrus(ray,scattering);
    sky=sky*cirrus.a+cirrus.rgb;
#if defined(SKY_CLOUD) && defined(ENV_SKY)
    if(ENV_SKY(18).z>.5) {
        vec4 cloud=environmentCaptureClouds(ray,camera);
        return cloud.rgb+cloud.a*sky;
    }
#endif
#ifdef WORLD_CLOUD
    if(WORLD_CLOUD(6).w>.5) {
        vec4 cloud=environmentResolvedClouds(ray,camera);
        vec3 behind=worldFogColor(sky+environmentLocalHaze(camera+ray*100000.0,camera),camera+ray*100000.0);
        return cloud.rgb+cloud.a*behind;
    } else
#endif
    if (ENV_CLOUD_PARAMS.x > 0.0 && ENV_CLOUD_PARAMS.y > 0.0 && ray.y > 0.005) {
        vec4 upper = environmentCloudLayer(ray,camera,true);
        vec4 lowerCloud = environmentCloudLayer(ray,camera,false);
        sky = mix(sky,upper.rgb,upper.a);
        sky = mix(sky,lowerCloud.rgb,lowerCloud.a);
    }
    return worldFogColor(sky + environmentLocalHaze(camera+ray*100000.0,camera),camera+ray*100000.0);
}

// PBR surfaces apply weather before lighting, retaining their mapped normal and
// roughness. Legacy materials receive only dampening in the haze boundary.
float environmentWetMaterialApplied = 0.0;
#include "world_wet_surface.glsl"
float environmentSurfaceWetness(vec3 position, vec3 normal) {
    if (ENV_SUN.w < 0.5 || ENV_WEATHER.x <= 0.0) return 0.0;
    float exposure = 1.0;
    for (int index=0;index<4;++index) {
        vec4 bounds=ENV_SHELTER(index);
        if (bounds.x<bounds.z && bounds.y<bounds.w) {
            vec2 q=abs(position.xz-(bounds.xy+bounds.zw)*0.5)-(bounds.zw-bounds.xy)*0.5;
            float distanceToEdge=length(max(q,vec2(0.0,0.0)))+min(max(q.x,q.y),0.0);
            // Splash/drift transition around the roof edge, rather than a binary
            // rectangle cut in the ground. The roof's upper surface stays wet.
            float belowRoof=1.0-smoothstep(ENV_SHELTER_HEIGHT[index]-.15,ENV_SHELTER_HEIGHT[index],position.y);
            exposure*=mix(1.0,smoothstep(-.8,.35,distanceToEdge),belowRoof);
        }
    }
    return ENV_WEATHER.x*exposure*(0.25+0.75*smoothstep(0.2,0.8,abs(normal.y)));
}

vec2 environmentWaterNoiseCorner(vec2 cell) {
    uint seed=celestialHash(uint(int(cell.x))*1597334677u ^ uint(int(cell.y))*3812015801u);
    return vec2(float(seed&65535u),float(seed>>16u))/32767.5-vec2(1.0,1.0);
}
vec2 environmentWaterNoise(vec2 position) {
    vec2 cell=floor(position),f=position-cell;
    f=f*f*(3.0-2.0*f);
    return mix(mix(environmentWaterNoiseCorner(cell),environmentWaterNoiseCorner(cell+vec2(1.0,0.0)),f.x),
        mix(environmentWaterNoiseCorner(cell+vec2(0.0,1.0)),environmentWaterNoiseCorner(cell+vec2(1.0,1.0)),f.x),f.y);
}

// Analytic masks are the current authored wet-map producer. Keep the same
// shoreline, height and slope contract as WorldPuddle.h / impact placement.
float environmentPuddleCoverage(vec3 position,vec3 geometricNormal) {
#ifdef ENV_PUDDLE
    if(ENV_WEATHER.y<=.02 || abs(geometricNormal.y)<.985)return 0.0;
    float coverage=0.0;
    for (int index=0;index<16;++index) {
        vec4 center=ENV_PUDDLE(index*2),shape=ENV_PUDDLE(index*2+1);
        if(center.w<.5 || abs(position.y-center.z)>.035)continue;
        vec2 p=(position.xz-center.xy)*shape.xy;
        if(max(abs(p.x),abs(p.y))>1.0)continue;
        p=vec2(p.x*shape.z-p.y*shape.w,p.x*shape.w+p.y*shape.z);
        float angle=dot(p,p)>1e-12?atan(p.y,p.x):0.0;
        float shore=.78+.11*sin(angle*3.0+.8)+.07*cos(angle*5.0-.4)+.035*sin(angle*7.0+1.2);
        float edge=clamp((shore-length(p))/.12,0.0,1.0);
        coverage=max(coverage,edge*edge*(3.0-2.0*edge));
    }
    return coverage*clamp((ENV_WEATHER.y-.02)/.45,0.0,1.0);
#else
    return 0.0;
#endif
}

vec3 environmentWaterNormal(vec3 position,vec3 normal) {
    vec2 p=position.xz*6.0;
    vec2 ripple=environmentWaterNoise(p+vec2(.7,-.4)*ENV_WEATHER.w);
    ripple+=.5*environmentWaterNoise(p*2.17+vec2(-.5,.9)*ENV_WEATHER.w+vec2(17.0,31.0));
    float footprint=dot(dFdx(p),dFdx(p))+dot(dFdy(p),dFdy(p));
    return normalize(normal+vec3(ripple.x,0.0,ripple.y)*ENV_WEATHER.z*.012/(1.0+footprint));
}

void environmentWetMaterialParameters(vec3 position, inout vec3 normal, inout vec3 albedo,
                            inout float roughness, inout float dielectricSpecular, vec2 material, float metal, vec3 sourceNormal) {
    environmentWetMaterialApplied=1.0;
    vec3 geometric=cross(dFdx(position),dFdy(position));
    float area=dot(geometric,geometric);
    geometric=area>1e-12?geometric*inversesqrt(area):normal;
    if(dot(geometric,sourceNormal)<0.0)geometric=-geometric;
#ifdef WORLD_WET_VIEW
    if(WORLD_WET_VIEW(0).x>.5) {
        if(material.y<.5)applyWorldWetSurface(position,geometric,normal,albedo,metal,roughness);
        return;
    }
#endif
    float wet=environmentSurfaceWetness(position,normal);
    if (wet<=0.0) return;
    if (material.y>0.5) {
        dielectricSpecular=0.02;
        normal=environmentWaterNormal(position,normal);
        return;
    }
    albedo*=1.0-0.28*wet*material.x;
    // Retain ground microstructure outside actual puddles.
    // Authored roughness is a conservative proxy, not a measured porosity map.
    // The ground floor must not make an already polished material rougher.
    float wetRoughness=min(roughness,max(.16,roughness*(.55+.35*smoothstep(.65,.95,roughness))));
    roughness=mix(roughness,wetRoughness,wet);
    float water=environmentPuddleCoverage(position,geometric);
    if(water>0.0) {
        // The thin film is evaluated once with the receiving ground. SSR, local
        // lights, contact visibility and probes now consume these same attributes.
        vec3 waterNormal=environmentWaterNormal(position,vec3(0.0,1.0,0.0));
        normal=normalize(mix(normal,waterNormal,water*water));
        roughness=mix(roughness,min(roughness,.16),water);
        dielectricSpecular=mix(dielectricSpecular,.02,water);
    }
}

#ifndef WORLD_PROBE_CAPTURE
#define WORLD_PROBE_CAPTURE false
#endif
void environmentWetMaterial(vec3 position, inout vec3 normal, inout vec3 albedo,
                            inout float roughness, inout float dielectricSpecular, float metal, vec3 sourceNormal) {
    if(WORLD_PROBE_CAPTURE)return;
    environmentWetMaterialParameters(position,normal,albedo,roughness,dielectricSpecular,ENV_WEATHER_MATERIAL.xy,metal,sourceNormal);
}

vec3 environmentHazeComposite(vec3 color,vec3 position,vec3 camera,bool includeLocalHaze) {
    bool legacyWetMaterial=environmentWetMaterialApplied<0.5;
#ifdef WORLD_WET_VIEW
    legacyWetMaterial=legacyWetMaterial&&WORLD_WET_VIEW(0).x<.5;
#endif
    if (legacyWetMaterial && ENV_WEATHER.x>0.0) {
        vec3 normal=cross(dFdx(position),dFdy(position));
        normal/=max(length(normal),0.00001);
        color*=1.0-0.28*environmentSurfaceWetness(position,normal)*ENV_WEATHER_MATERIAL.x;
    }
    if (ENV_SUN.w < 0.5 || (ENV_HAZE.w <= 0.0 && ENV_NIGHT.w <= 0.0)) return worldFogColor(color,position);
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
    float opacity = min(1.0-exp(-max(ENV_HAZE.w,0.0)*distanceMetres*densityIntegral), ENV_HEIGHT.z);
    if (ENV_NIGHT.w > 0.0 && distanceMetres > ENV_CLOUD_PARAMS.z) {
        float farHeight = max(mix(camera.y,position.y,ENV_CLOUD_PARAMS.z/distanceMetres)-ENV_HEIGHT.x,0.0);
        float farA = clamp(farHeight*ENV_HEIGHT.y,0.0,40.0);
        float farDifference = b-farA;
        float farIntegral = abs(farDifference)<0.001 ? exp(-0.5*(farA+b)) : (exp(-farA)-exp(-b))/farDifference;
        float distantOpacity = min(1.0-exp(-ENV_NIGHT.w*(distanceMetres-ENV_CLOUD_PARAMS.z)*farIntegral), ENV_CLOUD_PARAMS.w);
        opacity = 1.0-(1.0-opacity)*(1.0-distantOpacity);
    }
    vec3 local=includeLocalHaze?environmentLocalHaze(position,camera):vec3(0.0,0.0,0.0);
    return worldFogColor(mix(color,ENV_HAZE.rgb,opacity)+local,position);
}

vec3 environmentHaze(vec3 color,vec3 position,vec3 camera) {
    return environmentHazeComposite(color,position,camera,true);
}
// Use only when the view composes the prepared local haze once.
vec3 environmentPreparedHaze(vec3 color,vec3 position,vec3 camera) {
    return environmentHazeComposite(color,position,camera,false);
}
