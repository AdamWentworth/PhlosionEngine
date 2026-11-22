// Scene-owned source masks and geometry-derived exposure, before shared lighting.
float worldWetReactive=0.0;
float worldWetDepth=1.0;
uint worldWetCategory=0u;
vec2 worldWetScreen=vec2(0.0,0.0);
vec3 worldWetDryColor=vec3(0.0,0.0,0.0);
bool worldWetEvaluated=false;
#ifdef WORLD_WET_VIEW
#ifndef WORLD_WET_LOOP
#define WORLD_WET_LOOP
#endif
float wetInteriorTap(ivec2 p,int layer) {
    int size=int(WORLD_WET_VIEW(4).w);
    if(p.x<0||p.y<0||p.x>=size||p.y>=size||layer<0||layer>=int(WORLD_WET_VIEW(5).z))return 0.0;
    uvec4 b=uvec4(round(WORLD_WET_FIELD(p)*255.0));
    uint bits=b.x|(b.y<<8u)|(b.z<<16u)|(b.w<<24u);
    return float((bits>>uint(layer))&1u);
}
float wetInterior(vec3 p) {
    vec2 q=(p.xz-WORLD_WET_VIEW(4).xy)/WORLD_WET_VIEW(4).z;
    ivec2 cell=ivec2(floor(q))-ivec2(1,1);
    int layer=int(floor((p.y-WORLD_WET_VIEW(5).x)/WORLD_WET_VIEW(5).y));
    vec2 f=fract(q);
    return mix(mix(wetInteriorTap(cell,layer),wetInteriorTap(cell+ivec2(1,0),layer),f.x),
               mix(wetInteriorTap(cell+ivec2(0,1),layer),wetInteriorTap(cell+ivec2(1,1),layer),f.x),f.y);
}
float wetU16(vec2 b){return dot(round(b*255.0),vec2(1.0,256.0))/65535.0;}
float wetFieldTap(ivec2 p,int tile,float y) {
    int size=int(WORLD_WET_VIEW(4).w);
    if(p.x<0||p.y<0||p.x>=size||p.y>=size)return tile==1?0.0:-1000000.0;
    vec4 data=WORLD_WET_FIELD(p+ivec2(tile*size,0));
    if(tile==1) {
        float top=wetU16(data.rg)*128.0+WORLD_WET_VIEW(6).w-64.0;
        float bottom=wetU16(data.ba)*128.0+WORLD_WET_VIEW(6).w-64.0;
        return top>bottom&&y>bottom&&y<top?1.0:0.0;
    }
    return data.b>.5?wetU16(data.rg)*128.0+WORLD_WET_VIEW(7).w:-1000000.0;
}
float wetSpatialField(vec3 p,int tile) {
    vec4 transform=WORLD_WET_VIEW(tile==1?6:7);
    vec2 uv=(p.xz-transform.xy)/transform.z;
    vec2 q=uv*WORLD_WET_VIEW(4).w-.5;
    ivec2 cell=ivec2(floor(q));vec2 f=fract(q);
    return mix(mix(wetFieldTap(cell,tile,p.y),wetFieldTap(cell+ivec2(1,0),tile,p.y),f.x),
               mix(wetFieldTap(cell+ivec2(0,1),tile,p.y),wetFieldTap(cell+ivec2(1,1),tile,p.y),f.x),f.y);
}
int wetRainCascade(vec3 p,out vec2 uv) {
    WORLD_WET_LOOP for(int i=0;i<2;++i) {
        vec4 c=WORLD_WET_VIEW(2+i);
        uv=(p.xz-c.xy)/(c.z*WORLD_WET_VIEW(0).z);
        if(uv.x>0.0&&uv.y>0.0&&uv.x<1.0&&uv.y<1.0)return i;
    }
    return 2;
}
float wetRainTap(ivec2 p,int level) {
    int size=int(WORLD_WET_VIEW(0).z);
    p=clamp(p,ivec2(0,0),ivec2(size-1,size-1));p.y+=level*size;
    return WORLD_WET_RAIN(p);
}
float wetRainSample(vec3 p,bool comparison) {
    vec2 uv;int level=wetRainCascade(p,uv);
    if(level==2)return 1.0;
    vec2 q=uv*WORLD_WET_VIEW(0).z-.5;ivec2 cell=ivec2(floor(q));vec2 f=fract(q);
    vec4 d=vec4(wetRainTap(cell,level),wetRainTap(cell+ivec2(1,0),level),
                wetRainTap(cell+ivec2(0,1),level),wetRainTap(cell+ivec2(1,1),level));
    if(comparison) {
        float depth=(8000.0-p.y)/16000.0;
        d=vec4(depth<=d.x?1.0:0.0,depth<=d.y?1.0:0.0,depth<=d.z?1.0:0.0,depth<=d.w?1.0:0.0);
    }
    return mix(mix(d.x,d.y,f.x),mix(d.z,d.w,f.x),f.y);
}
float wetRainExposure(vec3 p) {
    vec2 uv;if(wetRainCascade(p,uv)==2)return 1.0;
    // Preserve the active source occluder-distance calculation with the Y-up
    // depth-range adapter, including its early exposed return.
    float distance=p.y+8000.0-wetRainSample(p,false)*16000.0;
    if(distance<0.0)return 1.0;
    
    // Stable spatial rotation; resource noise layout is an explicit adapter.
    uint hash=celestialHash(uint(int(floor(p.x*100.0)))*1597334677u^uint(int(floor(-p.z*100.0)))*3812015801u);
    vec3 rotation=vec3(float(hash&1023u),float((hash>>10u)&1023u),float((hash>>20u)&1023u))/511.5-vec3(1.0,1.0,1.0);
    float result=0.0;
    WORLD_WET_LOOP for(int i=0;i<4;++i) {
        vec2 poisson=i==0?vec2(.5857539,-.01466949):i==1?vec2(.2023499,-.1068985):i==2?vec2(.991432,.09448081):vec2(.34052,-.45828);
        vec2 offset=(distance*poisson*rotation.xy+poisson*rotation.zx)*.05;
        result+=wetRainSample(p+vec3(offset.x,0.0,-offset.y),true);
    }
    return result*.25;
}
// Open-roof ground gets drift/splash wetting around the edge. Keep the rain
// occlusion map for other surfaces; closed-room rejection still happens below.
float wetOpenShelterExposure(vec3 position,vec3 geometric,float rainExposure) {
    float exposure=rainExposure;
#ifdef ENV_SHELTER
    if(geometric.y>.8) for(int i=0;i<4;++i) {
        vec4 bounds=ENV_SHELTER(i);
        float clearance=ENV_SHELTER_HEIGHT[i]-position.y;
        if(bounds.x>=bounds.z||bounds.y>=bounds.w||clearance<=.5)continue;
        vec2 q=abs(position.xz-(bounds.xy+bounds.zw)*.5)-(bounds.zw-bounds.xy)*.5;
        float edge=length(max(q,vec2(0.0,0.0)))+min(max(q.x,q.y),0.0);
        float weight=1.0-smoothstep(1.5,2.5,abs(edge));
        // Static world-space irregularity prevents an artificial rectangular seam.
        float irregularity=.18*sin(position.x*1.7+position.z*.9)+.12*sin(position.z*2.3-position.x*.6);
        float damp=smoothstep(-1.5,1.0,edge+irregularity);
        exposure=mix(exposure,damp,weight);
    }
#endif
    return exposure;
}
float worldWetExposure(vec3 position,vec3 geometric) {
    vec3 offset=position+geometric*.5;
    float mask=clamp(1.0-wetInterior(offset),0.0,1.0);
    if(mask>0.0)mask*=wetOpenShelterExposure(position,geometric,wetRainExposure(offset));
    if(mask>0.0) {
        float water=wetSpatialField(position,2);
        mask*=1.0-clamp((water-position.y)/.4+.4,0.0,1.0);
        mask*=1.0-wetSpatialField(position+geometric*.1,1);
    }
    return mask;
}
vec2 worldWetPuddle(vec3 position,vec3 geometric,float amount) {
    float top=clamp((geometric.y+mix(-1.0,-.96,amount))*25.0,0.0,1.0);
    vec2 xy=vec2(position.x,-position.z);
    float mask=WORLD_WET_DROPS(fract(xy/5.0)).a*WORLD_WET_DROPS(fract(xy/25.0)).a*top;
    return clamp(vec2((mask-.3)*2.5,(mask-.45)*15.0),0.0,1.0);
}
vec3 wetCombine(vec3 normal,vec2 xy,float weight) {
    normal.x+=xy.x*weight;normal.z-=xy.y*weight;return normal;
}
float wetDitherOpacity(float opacity) {
    vec2 uv=worldWetScreen;
    float dither=fract((uv.x+uv.y*2.0-1.5+WORLD_WET_VIEW(0).y)/5.0);
    float noise=fract(dot(vec2(171.0,231.0)/71.0,uv));
    float threshold=(dither*5.0+noise)/6.0*(1.0-2.0/255.0)+2.0/255.0;
    return clamp((max(opacity-.5,0.0)*2.0-threshold)/.001,0.0,1.0);
}
void applyWorldWetSurface(vec3 position,vec3 geometric,inout vec3 normal,inout vec3 albedo,
                          float metal,inout float roughness) {
    worldWetDryColor=albedo;worldWetEvaluated=true;
    if(worldWetCategory==1u||worldWetCategory==4u||worldWetDepth<=WORLD_WET_VIEW(10).w)return;
    if(ENV_WEATHER.x+ENV_WEATHER.y+ENV_WEATHER.z<=0.0)return;
    float exposure=worldWetExposure(position,geometric);
    float moisture=ENV_WEATHER.x*exposure,puddles=ENV_WEATHER.y*exposure,rain=ENV_WEATHER.z*exposure;
    if(moisture+puddles+rain<=0.0)return;
    float distance=max(0.0,dot(position-WORLD_WET_VIEW(1).xyz,WORLD_WET_VIEW(10).xyz)),originalRoughness=roughness;
    float porosity=pow(roughness,WORLD_WET_VIEW(8).w)*(1.0-metal),accumulated=0.0,water=0.0;
    float time=ENV_WEATHER.w;
    if(distance<=120.0) {
        float fade=clamp(1.0-distance/120.0,0.0,1.0);
        float wet=clamp(geometric.y+mix(-1.0,.5,moisture),0.0,1.0);
        vec4 color=WORLD_WET_VIEW(9),rough=WORLD_WET_VIEW(8);
        float colorMask=pow(1.0-clamp((porosity-color.y)/max(1.0-color.x-color.y,.000001),0.0,1.0),color.z);
        float roughMask=pow(1.0-clamp((porosity-rough.x)/max(1.0-rough.x-rough.y,.000001),0.0,1.0),rough.z);
        albedo*=exp(-albedo*(1.0-colorMask)*wet*fade*5.0);
        roughness*=exp(-roughMask*wet*fade*5.0);accumulated=wet;
        if(worldWetCategory!=2u) {
            vec2 cover=worldWetPuddle(position,geometric,puddles);
            albedo*=1.0-cover.x*porosity*(1.0-accumulated)*fade;
            roughness*=(1.0-cover.x*fade)*(1.0-cover.y*fade);
            normal=mix(normal,geometric,cover.y*cover.y*fade);
            accumulated=clamp(accumulated+max(cover.x,cover.y),0.0,1.0);water=cover.y;
        }
    }
    vec2 xy=vec2(position.x,-position.z);
    if(distance<=15.0) {
        float frame=floor(fract(time)*16.0);
        vec2 uv=fract(fract(xy*1.5)*.25+vec2((frame-floor(frame/4.0)*4.0),-floor(frame/4.0))*.25);
        vec4 tex=WORLD_WET_RIPPLES(uv);
        float fade=clamp(1.0-distance/15.0,0.0,1.0);
        normal=wetCombine(normal,(tex.rg*2.0-vec2(1.0,1.0))*rain*.3,water*fade);
        worldWetReactive+=tex.b*water*fade;
    }
    if(distance<=5.0) {
        float fade=clamp(1.0-distance/5.0,0.0,1.0);
        vec4 tex=WORLD_WET_DROPS(fract(xy));
        float mask=tex.b*clamp((geometric.y-.75)*10.0,0.0,1.0)*rain;
        float phase=1.0-fract(time*.4+tex.r);
        float dropWater=clamp((phase-.85)/.15,0.0,1.0)*mask;
        float dropWet=clamp(tex.g+mix(-1.0,1.0,moisture),0.0,1.0)*mask;
        albedo*=1.0-max(dropWet,dropWater)*porosity*(1.0-accumulated)*fade;
        roughness*=1.0-dropWater*fade;normal=mix(normal,geometric,dropWater*fade);
        worldWetReactive+=dropWater*fade;
    }
    if(distance<=30.0) {
        float fade=clamp(1.0-distance/30.0,0.0,1.0);
        vec3 p=vec3(position.x,-position.z,position.y),g=vec3(geometric.x,-geometric.z,geometric.y);
        vec2 uv=mix(p.yz*vec2(.75,.25),p.xz*vec2(.75,.25),wetDitherOpacity(abs(g.y)));
        uv=mix(uv,vec2((p.x-p.y)*.75/1.4142,p.z*.25),wetDitherOpacity(abs((g.x+g.y)*.70710678)));
        uv=mix(uv,vec2((p.x+p.y)*.75/1.4142,p.z*.25),wetDitherOpacity(abs((-g.x+g.y)*.70710678)));
        vec4 a=WORLD_WET_LEAKS(fract(uv+vec2(0.0,time*.1))),b=WORLD_WET_LEAKS(fract(uv*.33+vec2(0.0,time*.015)));
        float wave=.5+.5*sin(6.28318530718*fract(a.b-b.b+time*.25));
        float weight=wave*wave*clamp((1.0-abs(g.z)-.2)*2.0,0.0,1.0)*fade*moisture*rain;
        roughness*=1.0-weight;
        normal=wetCombine(wetCombine(normal,a.rg*2.0-vec2(1.0,1.0),weight),b.rg*2.0-vec2(1.0,1.0),weight);
    }
    roughness=max(roughness,min(originalRoughness,metal*.1));
    normal*=inversesqrt(max(dot(normal,normal),1e-12));
}
#endif
vec3 worldWetEmissiveColor(vec3 albedo,vec3 emission) {
#ifdef WORLD_WET_VIEW
    if(worldWetEvaluated)return mix(albedo,worldWetDryColor,clamp(max(emission.x,max(emission.y,emission.z))/WORLD_WET_VIEW(9).w,0.0,1.0));
#endif
    return albedo;
}
