// Binding adapters provide sparse mask coverage and gradient-aware texture
// samples. Walking fixed priority bits makes each texture index wave-uniform;
// inactive lanes do not sample that layer. Never normalize remaining weights.
WorldLayerParameters worldReadLayerParameters(uint layer);
WorldLayerSamples worldReadLayerSamples(uint layer);
float worldReadLayerCoverage(uint layer);
struct WorldLayeredResult {
    vec3 color;
    vec3 normal;
    float roughness;
    float metalness;
    uint dominantLayer;
    float dominantWeight;
    float selectedRangeWeight;
};
WorldLayeredResult worldEvaluateLayers(uint activeLayers,vec3 globalNormal,uvec2 selectedRange) {
    WorldLayerAccumulation a=worldLayerBegin();
    WorldLayeredResult result;
    result.dominantLayer=0u;result.dominantWeight=0.0;result.selectedRangeWeight=0.0;
    bool multiple=(activeLayers&(activeLayers-1u))!=0u;
    for(int priority=31;priority>=0;--priority) {
        uint layer=uint(priority),bit=1u<<layer;
        if((activeLayers&bit)==0u)continue;
        bool last=(activeLayers&(activeLayers-1u))==0u;
        activeLayers^=bit;
        float weight=last?1.0:worldReadLayerCoverage(layer);
        if(weight>0.0) {
            float contribution=worldLayerAccumulate(a,worldReadLayerParameters(layer),
                                                     worldReadLayerSamples(layer),weight);
            if(contribution>result.dominantWeight) {
                result.dominantLayer=layer;result.dominantWeight=contribution;
            }
            if(layer>=selectedRange.x&&layer<=selectedRange.y)
                result.selectedRangeWeight+=contribution;
        }
        if(a.remaining<=0.0)break;
    }
    result.color=a.color;result.roughness=a.roughness;result.metalness=a.metalness;
    result.normal=worldLayerFinishNormal(a,globalNormal,multiple);
    return result;
}
