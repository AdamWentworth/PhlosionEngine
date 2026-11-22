#include "WorldLayeredMaterial.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <cstring>

namespace engine::render {
namespace {
bool reject(std::string* error,const char* message){if(error)*error=message;return false;}
std::uint64_t tileCount(std::uint32_t pixels,std::uint32_t size){return (std::uint64_t(pixels)+size-1)/size;}
std::uint32_t layerBits(std::uint32_t count){return count==32?~0u:(1u<<count)-1u;}
}
bool validateWorldLayerSetup(std::span<const WorldLayerRecord> layers,std::uint32_t textures,
                            std::uint32_t& active,std::string* error) {
    if(layers.empty()||layers.size()>kWorldLayerLimit||!textures||textures>65536)
        return reject(error,"Invalid layered material layer/texture count");
    std::uint32_t bits=0;
    for(std::size_t i=0;i<layers.size();++i) {
        const auto& l=layers[i];
        for(const auto* row:{&l.tintOpacity,&l.roughnessRemap,&l.metalnessRemap,&l.colorMaskRemap,
                            &l.detail,&l.microOffset,&l.layerOffsetNormal})
            for(float x:*row)if(!std::isfinite(x))return reject(error,"Nonfinite layered material parameter");
        if(l.tintOpacity[3]<0||l.tintOpacity[3]>1||l.detail[0]<=0||l.detail[1]<=0||l.detail[2]<=0)
            return reject(error,"Invalid layer opacity, tiling or contrast");
        if(l.tintOpacity[0]<0||l.tintOpacity[1]<0||l.tintOpacity[2]<0)
            return reject(error,"Negative layer tint");
        if(l.microOffset[2]!=0||l.microOffset[3]!=0||l.layerOffsetNormal[3]!=0||l.textures[3]!=0)
            return reject(error,"Unsupported layered material record version");
        for(const auto index:{l.textures[0],l.textures[1]&65535u,l.textures[1]>>16u,
                              l.textures[2]&65535u,l.textures[2]>>16u})
            if(index>=textures)return reject(error,"Layer texture outside prepared table");
        if(l.tintOpacity[3]>0)bits|=1u<<i;
    }
    active=bits;return true;
}
bool validateWorldLayerMasks(const WorldLayerMaskData& m,std::uint32_t count,std::string* error) {
    if(!count||count>32||!m.tileSize||m.tileSize>4096)
        return reject(error,"Invalid layer mask count or tile size");
    for(unsigned c=0;c<2;++c)
        if(!m.highResolution[c]||!m.lowResolution[c]||m.lowResolution[c]>m.highResolution[c]||
           !m.atlasExtent[c]||m.atlasExtent[c]>16384||m.highResolution[c]>65536)
            return reject(error,"Invalid layer mask dimensions");
    if(std::uint64_t(m.atlasExtent[0])*m.atlasExtent[1]!=m.atlas.size())
        return reject(error,"Layer mask atlas payload size mismatch");
    const auto allowed=layerBits(count);
    for(unsigned level=0;level<2;++level) {
        const auto& resolution=level?m.lowResolution:m.highResolution;
        const auto& tiles=level?m.low:m.high;
        if(tileCount(resolution[0],m.tileSize)*tileCount(resolution[1],m.tileSize)!=tiles.size())
            return reject(error,"Layer mask tile grid size mismatch");
        for(const auto& tile:tiles) {
            const auto records=static_cast<std::uint32_t>(std::popcount(tile.layers));
            if(tile.layers&~allowed||tile.firstRecord>m.records.size()||records>m.records.size()-tile.firstRecord)
                return reject(error,"Layer mask table range is invalid");
            for(std::uint32_t i=0;i<records;++i) {
                const auto p=m.records[tile.firstRecord+i];
                if(p&0xf0000000u)return reject(error,"Unsupported layer mask record version");
                const std::array<std::uint32_t,2> position{p&1023u,(p>>10u)&1023u};
                const std::array<std::uint32_t,2> shifts{(p>>20u)&15u,(p>>24u)&15u};
                for(unsigned c=0;c<2;++c)
                    if(std::uint64_t(position[c]+1)*(m.tileSize+2)>m.atlasExtent[c]||
                       (m.highResolution[c]>>shifts[c])==0)
                        return reject(error,"Layer mask atlas address or resolution is invalid");
            }
        }
    }
    return true;
}
WorldLayerMaskSelection selectWorldLayerMasks(const WorldLayerMaskData& m,
                                             std::uint32_t setup,std::array<float,2> uv) {
    if(!std::isfinite(uv[0])||!std::isfinite(uv[1]))throw std::invalid_argument("Nonfinite mask coordinates");
    WorldLayerMaskSelection s;
    uv[1]=1.f-uv[1];
    for(unsigned c=0;c<2;++c)s.flippedUv[c]=uv[c]-std::floor(uv[c]);
    const auto choose=[&](const auto& resolution,const auto& tiles) {
        const auto width=tileCount(resolution[0],m.tileSize);
        const auto x=static_cast<std::uint32_t>(s.flippedUv[0]*float(resolution[0])/float(m.tileSize));
        const auto y=static_cast<std::uint32_t>(s.flippedUv[1]*float(resolution[1])/float(m.tileSize));
        return tiles.at(static_cast<std::size_t>(y*width+x));
    };
    const auto high=choose(m.highResolution,m.high),low=choose(m.lowResolution,m.low);
    s.highFirst=high.firstRecord;s.lowFirst=low.firstRecord;
    s.highLayers=high.layers;s.lowLayers=low.layers;s.active=setup&(high.layers|low.layers);
    return s;
}
std::uint32_t worldLayerMaskRecord(const WorldLayerMaskData& m,const WorldLayerMaskSelection& s,std::uint32_t layer) {
    if(layer>=32||!(s.active&(1u<<layer)))throw std::invalid_argument("Inactive layer requested");
    const auto bit=1u<<layer;
    const bool high=(s.highLayers&bit)!=0;
    const auto index=(high?s.highFirst:s.lowFirst)+static_cast<std::uint32_t>(
        std::popcount((high?s.highLayers:s.lowLayers)&(bit-1u)));
    return m.records.at(index);
}
std::array<float,2> worldLayerMaskUv(const WorldLayerMaskData& m,std::uint32_t packed,std::array<float,2> uv) {
    std::array<float,2> result{};
    const std::array<std::uint32_t,2> tile{packed&1023u,(packed>>10u)&1023u};
    const std::array<std::uint32_t,2> shift{(packed>>20u)&15u,(packed>>24u)&15u};
    for(unsigned c=0;c<2;++c) {
        if(!std::isfinite(uv[c]))throw std::invalid_argument("Nonfinite mask coordinates");
        const float p=uv[c]*float(m.highResolution[c]>>shift[c])/float(m.tileSize);
        result[c]=(float(tile[c]*(m.tileSize+2))+(p-std::floor(p))*float(m.tileSize)+1.f)/float(m.atlasExtent[c]);
    }
    return result;
}
}

namespace engine::render {
WorldLayerPackedSet packWorldLayerMaterialSet(const WorldLayerMaterialSet& set) {
    constexpr std::uint64_t textureBudget=256ull<<20;
    constexpr std::size_t wordBudget=(16u<<20)/sizeof(std::uint32_t);
    if(set.identity.empty()||set.materials.empty()||set.materials.size()>256||
       set.textures.empty()||set.textures.size()>kWorldLayerTextureLimit)
        throw std::invalid_argument("Layered set identity or table budget");
    WorldLayerPackedSet out;
    std::unordered_set<std::string> identities;
    for(const auto& t:set.textures) {
        if(t.identity.empty()||!identities.insert(t.identity).second||t.mips.empty()||t.mips.size()>15)
            throw std::invalid_argument("Layered texture identity or mip count");
        auto width=t.mips.front().width,height=t.mips.front().height;
        if(!width||!height||width>16384||height>16384)
            throw std::invalid_argument("Layered texture dimensions");
        for(std::size_t i=0;i<t.mips.size();++i) {
            const auto& mip=t.mips[i];
            if(mip.width!=width||mip.height!=height||mip.rgba.size()!=std::uint64_t(width)*height*4)
                throw std::invalid_argument("Layered texture mip payload");
            out.textureBytes+=mip.rgba.size();
            if(out.textureBytes>textureBudget)throw std::invalid_argument("Layered texture memory budget");
            if(width==1&&height==1&&i+1!=t.mips.size())throw std::invalid_argument("Redundant layered texture mip");
            width=std::max(1u,width/2);height=std::max(1u,height/2);
        }
        if(t.mips.back().width!=1||t.mips.back().height!=1)
            throw std::invalid_argument("Incomplete layered texture mip chain");
    }
    out.words.resize(4+set.materials.size()*32);
    out.words[0]=1;out.words[1]=static_cast<std::uint32_t>(set.materials.size());
    out.words[2]=static_cast<std::uint32_t>(set.textures.size());
    const auto append=[&](const auto* source,std::size_t bytes) {
        if(bytes%4||bytes/4>wordBudget-out.words.size())throw std::invalid_argument("Layered parameter memory budget");
        const auto offset=static_cast<std::uint32_t>(out.words.size());
        out.words.resize(out.words.size()+bytes/4);
        if(bytes)std::memcpy(out.words.data()+offset,source,bytes);
        return offset;
    };
    const auto texture=[&](std::uint32_t index,bool srgb,bool optional=false) {
        if(optional&&index==kWorldLayerNoTexture)return;
        if(index>=set.textures.size()||set.textures[index].srgb!=srgb)
            throw std::invalid_argument("Layered texture colour space or index");
    };
    for(std::size_t index=0;index<set.materials.size();++index) {
        const auto& m=set.materials[index];std::uint32_t active=0;std::string why;
        if(!validateWorldLayerSetup(m.layers,static_cast<std::uint32_t>(set.textures.size()),active,&why)||
           !validateWorldLayerMasks(m.masks,static_cast<std::uint32_t>(m.layers.size()),&why))
            throw std::invalid_argument(why);
        if(!active||m.selectedLayers[0]>m.selectedLayers[1]||m.selectedLayers[1]>=32||
           !std::isfinite(m.normalStrength)||m.normalStrength<0||m.normalStrength>16)
            throw std::invalid_argument("Layered material selection or normal strength");
        for(float x:m.uvScale)if(!std::isfinite(x)||x<=0)throw std::invalid_argument("Layered material UV scale");
        for(float x:m.tiling)if(!std::isfinite(x)||x<=0)throw std::invalid_argument("Layered material tiling table");
        if(!std::isfinite(m.surfaceAspect)||m.surfaceAspect<=0||m.surfaceAspect>1024)
            throw std::invalid_argument("Layered surface aspect");
        for(const auto* scale:{&m.normalGradient,&m.microGradient})
            for(float x:*scale)if(!std::isfinite(x)||x<=0||x>16)throw std::invalid_argument("Layered sample gradient");
        if((m.normalYSign!=1&&m.normalYSign!=-1)||!std::isfinite(m.globalNormalIntensity)||m.globalNormalIntensity<0||m.globalNormalIntensity>16)
            throw std::invalid_argument("Layered global normal parameters");
        for(const auto* row:{&m.globalNormalUvScale,&m.globalNormalUvBias})
            for(float x:*row)if(!std::isfinite(x))throw std::invalid_argument("Layered global normal coordinates");
        texture(m.maskTexture,false);
        texture(m.macroColor,true,true);texture(m.globalNormal,false,true);texture(m.tilingMap,false,true);
        const auto& atlas=set.textures[m.maskTexture].mips.front();
        if(atlas.width!=m.masks.atlasExtent[0]||atlas.height!=m.masks.atlasExtent[1])
            throw std::invalid_argument("Layered mask texture extent");
        for(std::size_t i=0;i<m.masks.atlas.size();++i)
            if(atlas.rgba[i*4]!=m.masks.atlas[i])throw std::invalid_argument("Layered mask texture data mismatch");
        for(const auto& layer:m.layers) {
            texture(layer.textures[0],false);
            texture(layer.textures[1]&65535u,true);texture(layer.textures[1]>>16u,false);
            texture(layer.textures[2]&65535u,false);texture(layer.textures[2]>>16u,false);
        }
        std::array<std::uint32_t,32> header{};
        header[0]=static_cast<std::uint32_t>(m.layers.size());header[1]=active;
        header[2]=append(m.layers.data(),m.layers.size()*sizeof(WorldLayerRecord));
        header[3]=append(m.masks.high.data(),m.masks.high.size()*sizeof(WorldLayerMaskTile));
        header[4]=append(m.masks.low.data(),m.masks.low.size()*sizeof(WorldLayerMaskTile));
        header[5]=append(m.masks.records.data(),m.masks.records.size()*4);
        header[6]=m.maskTexture;header[7]=m.masks.tileSize;
        header[8]=m.masks.highResolution[0];header[9]=m.masks.highResolution[1];
        header[10]=m.masks.lowResolution[0];header[11]=m.masks.lowResolution[1];
        header[12]=m.masks.atlasExtent[0];header[13]=m.masks.atlasExtent[1];
        header[14]=m.terrain?1u:0u;header[15]=m.macroColor;header[16]=m.globalNormal;header[17]=m.tilingMap;
        header[18]=m.selectedLayers[0];header[19]=m.selectedLayers[1];
        header[20]=std::bit_cast<std::uint32_t>(m.uvScale[0]);header[21]=std::bit_cast<std::uint32_t>(m.uvScale[1]);
        header[22]=std::bit_cast<std::uint32_t>(m.normalStrength);
        header[23]=append(m.tiling.data(),sizeof(m.tiling));
        header[24]=std::bit_cast<std::uint32_t>(m.surfaceAspect);
        for(unsigned c=0;c<2;++c){header[25+c]=std::bit_cast<std::uint32_t>(m.normalGradient[c]);header[27+c]=std::bit_cast<std::uint32_t>(m.microGradient[c]);}
        header[29]=std::bit_cast<std::uint32_t>(m.normalYSign);
        const std::array<float,8> global{m.globalNormalUvScale[0],m.globalNormalUvScale[1],m.globalNormalUvBias[0],m.globalNormalUvBias[1],m.globalNormalIntensity,0,0,0};
        header[30]=append(global.data(),sizeof(global));header[31]=m.terrainDistanceFade?1:0;
        std::copy(header.begin(),header.end(),out.words.begin()+4+index*32);
    }
    return out;
}
}
