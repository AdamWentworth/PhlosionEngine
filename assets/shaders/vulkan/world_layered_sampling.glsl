#include "world_layered_material.glsl"
#include "world_layered_evaluation.glsl"
struct WorldLayerContext {
    uint header;
    uvec2 high;
    uvec2 low;
    vec2 maskUv;
    vec2 uv;
    vec2 dx;
    vec2 dy;
};
LAYER_STORAGE WorldLayerContext worldLayerContext;
uint worldLayerWord(uint offset){return LAYER_WORD(worldLayerContext.header+offset);}
vec4 worldLayerFloat4(uint word){
    return vec4(uintBitsToFloat(LAYER_WORD(word)),uintBitsToFloat(LAYER_WORD(word+1u)),
                uintBitsToFloat(LAYER_WORD(word+2u)),uintBitsToFloat(LAYER_WORD(word+3u)));
}
WorldLayerParameters worldReadLayerParameters(uint layer){
    uint offset=worldLayerWord(2u)+layer*32u;
    WorldLayerParameters p;
    p.tintOpacity=worldLayerFloat4(offset);
    p.roughnessRemap=worldLayerFloat4(offset+4u);p.metalnessRemap=worldLayerFloat4(offset+8u);
    p.colorMaskRemap=worldLayerFloat4(offset+12u);
    vec4 detail=worldLayerFloat4(offset+16u),normal=worldLayerFloat4(offset+24u);
    p.contrastInverse=detail.z;p.borderStrength=detail.w;p.normalStrength=normal.z;
    return p;
}
WorldLayerSamples worldReadLayerSamples(uint layer){
    uint offset=worldLayerWord(2u)+layer*32u;
    vec4 detail=worldLayerFloat4(offset+16u),micro=worldLayerFloat4(offset+20u),normal=worldLayerFloat4(offset+24u);
    uint microIndex=LAYER_WORD(offset+28u),colorNormal=LAYER_WORD(offset+29u),roughMetal=LAYER_WORD(offset+30u);
    vec2 uv=worldLayerContext.uv*detail.x+normal.xy,dx=worldLayerContext.dx*detail.x,dy=worldLayerContext.dy*detail.x;
    vec2 normalGradient=vec2(uintBitsToFloat(worldLayerWord(25u)),uintBitsToFloat(worldLayerWord(26u)));
    vec2 microGradient=vec2(uintBitsToFloat(worldLayerWord(27u)),uintBitsToFloat(worldLayerWord(28u)));
    vec2 aspect=vec2(uintBitsToFloat(worldLayerWord(24u)),1.0);
    WorldLayerSamples s;
    s.color=LAYER_SAMPLE(colorNormal&65535u,uv,dx,dy).xyz;
    s.normalXY=LAYER_SAMPLE(colorNormal>>16u,uv,dx*normalGradient.x,dy*normalGradient.y).xy*2.0-vec2(1.0,1.0);
    s.roughness=LAYER_SAMPLE(roughMetal&65535u,uv,dx,dy).x;
    s.metalness=LAYER_SAMPLE(roughMetal>>16u,uv,dx,dy).x;
    vec4 mb=LAYER_SAMPLE(microIndex,worldLayerContext.uv*detail.y+micro.xy,worldLayerContext.dx*detail.y*aspect*microGradient.x,worldLayerContext.dy*detail.y*aspect*microGradient.y);
    s.microblend=vec4(mb.xyz*2.0-vec3(1.0,1.0,1.0),mb.w);
    return s;
}
float worldReadLayerCoverage(uint layer){
    uint bit=1u<<layer;
    uvec2 tile=(worldLayerContext.high.y&bit)!=0u?worldLayerContext.high:worldLayerContext.low;
    uint record=LAYER_WORD(worldLayerWord(5u)+tile.x+uint(LAYER_POPCOUNT(tile.y&(bit-1u))));
    vec2 extent=vec2(worldLayerWord(12u),worldLayerWord(13u));float size=float(worldLayerWord(7u));
    vec2 uv=worldLayerMaskAtlasUv(record,worldLayerContext.maskUv,vec2(worldLayerWord(8u),worldLayerWord(9u)),
        size,extent,vec2(size+2.0,size+2.0)/extent);
    return LAYER_LEVEL(worldLayerWord(6u),uv).x;
}
uvec2 worldReadMaskTile(uint first,uvec2 resolution,vec2 uv,uint size){
    uvec2 tile=uvec2(uv*vec2(resolution)/float(size));
    uint width=(resolution.x+size-1u)/size;
    uint address=first+2u*(tile.y*width+tile.x);
    return uvec2(LAYER_WORD(address),LAYER_WORD(address+1u));
}
WorldSurfaceMaterial worldLayeredSurface(uint material,vec2 meshUv,vec3 position,vec3 sourceNormal,vec4 tangent,float lodBias){
    WorldSurfaceMaterial surface;
    surface.albedo=vec3(1.0,0.0,1.0);surface.normal=normalize(sourceNormal);
    surface.orm=vec4(1.0,1.0,0.0,1.0);surface.metallic=0.0;surface.roughness=1.0;surface.occlusion=1.0;surface.dielectricSpecular=.04;
    if(material==0u||material>LAYER_WORD(1u))return surface;
    worldLayerContext.header=4u+(material-1u)*32u;
    vec2 uvScale=vec2(uintBitsToFloat(worldLayerWord(20u)),uintBitsToFloat(worldLayerWord(21u)));
    vec2 maskUv=meshUv*uvScale,maskDx=dFdx(maskUv),maskDy=dFdy(maskUv);
    vec2 uv=maskUv,dx=maskDx,dy=maskDy;
    bool terrain=worldLayerWord(14u)==1u;
    vec3 globalNormal=normalize(sourceNormal);
    uint globalMap=worldLayerWord(16u);
    vec3 globalDetail=vec3(0.0,0.0,1.0);
    if(globalMap!=0xffffffffu){
        vec4 globalUv=worldLayerFloat4(worldLayerWord(30u));
        vec2 xy=LAYER_SAMPLE(globalMap,maskUv*globalUv.xy+globalUv.zw,maskDx*globalUv.xy,maskDy*globalUv.xy).xy*2.0-vec2(1.0,1.0);
        vec3 decoded=worldLayerReconstruct(xy);
        if(terrain)globalNormal=normalize(vec3(decoded.x,decoded.z,-decoded.y));
        else globalDetail=normalize(mix(vec3(0.0,0.0,1.0),decoded,uintBitsToFloat(LAYER_WORD(worldLayerWord(30u)+4u))));
    }
    if(terrain){
        vec3 sourcePosition=vec3(position.x,-position.z,position.y),sourceGlobal=vec3(globalNormal.x,-globalNormal.z,globalNormal.y);
        vec3 positionDx=dFdx(sourcePosition),positionDy=dFdy(sourcePosition);
        uint axis=worldTerrainProjectionAxis(worldTerrainProjectionWeights(sourceGlobal));
        uv=worldTerrainPlanar(sourcePosition,axis);
        dx=worldTerrainPlanar(positionDx,axis);dy=worldTerrainPlanar(positionDy,axis);
        uint map=worldLayerWord(17u),tile=0u;
        if(map!=0xffffffffu)tile=min(7u,uint(LAYER_LEVEL(map,maskUv).x*255.0+.5));
        uv*=uintBitsToFloat(LAYER_WORD(worldLayerWord(23u)+tile));
    }
    worldLayerContext.uv=fract(uv)*vec2(uintBitsToFloat(worldLayerWord(24u)),1.0);
    worldLayerContext.dx=dx*exp2(lodBias);worldLayerContext.dy=dy*exp2(lodBias);
    worldLayerContext.maskUv=fract(vec2(maskUv.x,1.0-maskUv.y));
    worldLayerContext.high=worldReadMaskTile(worldLayerWord(3u),uvec2(worldLayerWord(8u),worldLayerWord(9u)),
        worldLayerContext.maskUv,worldLayerWord(7u));
    worldLayerContext.low=worldReadMaskTile(worldLayerWord(4u),uvec2(worldLayerWord(10u),worldLayerWord(11u)),
        worldLayerContext.maskUv,worldLayerWord(7u));
    uint activeLayers=worldLayerWord(1u)&(worldLayerContext.high.y|worldLayerContext.low.y);
    WorldLayeredResult result=worldEvaluateLayers(activeLayers,globalDetail,uvec2(worldLayerWord(18u),worldLayerWord(19u)));
    surface.albedo=result.color;surface.roughness=environmentMaterialRoughness(result.roughness);
    surface.metallic=terrain?0.0:result.metalness;
    surface.dielectricSpecular=terrain?.04*result.metalness:.04;
    if(terrain){
        uint macro=worldLayerWord(15u);
        if(macro!=0xffffffffu)surface.albedo=clamp(surface.albedo*LAYER_SAMPLE(macro,maskUv,maskDx,maskDy).xyz*2.0,0.0,1.0);
        float strength=uintBitsToFloat(worldLayerWord(22u));
        if(worldLayerWord(31u)!=0u)strength*=1.0-clamp((length(position-LAYER_CAMERA)-20.0)/80.0,0.0,1.0);
        vec3 n=worldTerrainLayerNormal(vec3(globalNormal.x,-globalNormal.z,globalNormal.y),result.normal,strength);
        surface.normal=vec3(n.x,n.z,-n.y);
    }else{
        vec3 tx=tangent.xyz-dot(tangent.xyz,globalNormal)*globalNormal;
        float handedness=tangent.w<0.0?-1.0:1.0;
        if(dot(tx,tx)<1e-10){
            vec3 dpdx=dFdx(position),dpdy=dFdy(position);
            float det=maskDx.x*maskDy.y-maskDx.y*maskDy.x;
            if(abs(det)>1e-12){
                tx=(dpdx*maskDy.y-dpdy*maskDx.y)/det;
                vec3 by=(dpdy*maskDx.x-dpdx*maskDy.x)/det;
                tx-=globalNormal*dot(globalNormal,tx);
                handedness=dot(cross(globalNormal,tx),by)<0.0?-1.0:1.0;
            }
        }
        if(dot(tx,tx)<1e-10)tx=cross(abs(globalNormal.y)<.99?vec3(0.0,1.0,0.0):vec3(0.0,0.0,1.0),globalNormal);
        tx=normalize(tx);
        vec3 ty=cross(globalNormal,tx)*handedness;
        surface.normal=normalize(tx*result.normal.x+ty*(result.normal.y*uintBitsToFloat(worldLayerWord(29u)))+globalNormal*result.normal.z);
    }
    environmentWetMaterial(position,surface.normal,surface.albedo,surface.roughness,surface.dielectricSpecular,surface.metallic,sourceNormal);
    surface.orm=vec4(1.0,surface.roughness,surface.metallic,1.0);
    return surface;
}
