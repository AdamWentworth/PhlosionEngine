#include "engine/render/WorldLayeredMaterial.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace engine::render;
namespace {void require(bool x,const char* message){if(!x)throw std::runtime_error(message);}}
int main()try {
    std::vector<WorldLayerRecord> layers(32);std::uint32_t bits=123;std::string error;
    require(validateWorldLayerSetup(layers,1,bits,&error)&&bits==~0u,"All 32 layer bits");
    layers[31].tintOpacity[3]=0;
    require(validateWorldLayerSetup(layers,1,bits,&error)&&bits==0x7fffffffu,"Disabled highest layer");
    auto bad=layers;bad[3].textures[1]=1u<<16;
    require(!validateWorldLayerSetup(bad,1,bits,&error)&&bits==0x7fffffffu,"Out of range texture must not publish");
    bad=layers;bad[0].detail[2]=0;require(!validateWorldLayerSetup(bad,1,bits,&error),"Zero contrast");
    bad=layers;bad[1].roughnessRemap[0]=std::numeric_limits<float>::quiet_NaN();
    require(!validateWorldLayerSetup(bad,1,bits,&error),"NaN parameter");
    bad=layers;bad[4].tintOpacity[3]=1.1f;require(!validateWorldLayerSetup(bad,1,bits,&error),"Opacity overflow");
    require(!validateWorldLayerSetup({},1,bits,&error),"Empty setup");
    WorldLayerMaskData m;m.highResolution={64,32};m.lowResolution={32,16};m.atlasExtent={136,68};m.tileSize=32;
    m.high={{0,0x80000005u},{3,0x80000001u}};m.low={{5,7}};
    m.records={0,1,2,3,1024,1025,1026,1027};m.atlas.resize(136*68);
    require(validateWorldLayerMasks(m,32,&error),"Valid sparse mask atlas");
    for(unsigned i=0;i<128;++i) {
        const float u=(float(i)+.5f)/128;auto s=selectWorldLayerMasks(m,~0u,{u,.5f});
        require(s.active==0x80000007u,"High/low union");
        require(worldLayerMaskRecord(m,s,1)==1026,"Absent high layer must use low");
        require(worldLayerMaskRecord(m,s,0)==(i<64?0u:3u),"High wins overlap");
        require(worldLayerMaskRecord(m,s,31)==(i<64?2u:1024u),"Bit 31 and sparse rank");
        const auto address=worldLayerMaskUv(m,worldLayerMaskRecord(m,s,0),s.flippedUv);
        const float px=std::fmod(u*64.f,32.f);
        require(std::abs(address[0]-(float(i<64?0:102)+px+1)/136)<.00001,"Atlas texel center and border");
    }
    for(float u:{-2.f,-1.f,0.f,1.f,2.f}) {
        auto s=selectWorldLayerMasks(m,1,{u,u});require(s.flippedUv==std::array<float,2>{0,0},"Periodic boundary");
    }
    auto corrupted=m;corrupted.records[0]=15u<<20;
    require(!validateWorldLayerMasks(corrupted,32,&error),"Zero resolution from shift");
    corrupted=m;corrupted.records[0]=1023u;require(!validateWorldLayerMasks(corrupted,32,&error),"Atlas overflow");
    corrupted=m;corrupted.low[0].firstRecord=0xffffffffu;require(!validateWorldLayerMasks(corrupted,32,&error),"Record overflow");
    corrupted=m;corrupted.high.pop_back();require(!validateWorldLayerMasks(corrupted,32,&error),"Truncated grid");
    corrupted=m;corrupted.atlas.pop_back();require(!validateWorldLayerMasks(corrupted,32,&error),"Truncated atlas");
    require(!validateWorldLayerMasks(m,3,&error),"Setup count mismatch");
    bool rejected=false;try{selectWorldLayerMasks(m,1,{std::numeric_limits<float>::infinity(),0});}
    catch(const std::invalid_argument&){rejected=true;}require(rejected,"Nonfinite coordinates");
    // Uneven grid extents must use ceiling division, not a power-of-two shift.
    m.highResolution={63,31};m.lowResolution={29,15};
    require(validateWorldLayerMasks(m,32,&error),"Non-power-of-two mask dimensions");
    require(selectWorldLayerMasks(m,1,{.999f,.001f}).highFirst==3,"Non-power-of-two last tile");

    WorldLayerMaterialSet set;set.identity="layer-set-test";
    WorldLayerTexture color;color.identity="linear-decoded-colour";color.srgb=true;
    color.mips.push_back({1,1,{128,128,128,255}});
    WorldLayerTexture linear;linear.identity="linear-data";linear.mips.push_back({1,1,{128,128,255,255}});
    WorldLayerTexture mask;mask.identity="mask";
    mask.mips.push_back({4,4,std::vector<std::uint8_t>(64,255)});
    mask.mips.push_back({2,2,std::vector<std::uint8_t>(16,255)});
    mask.mips.push_back({1,1,std::vector<std::uint8_t>(4,255)});
    set.textures={color,linear,mask};
    WorldLayerMaterial material;material.layers.resize(32);
    for(auto& layer:material.layers)layer.textures={1,1u<<16,1u|(1u<<16),0};
    material.maskTexture=2;
    material.masks.highResolution={2,2};material.masks.lowResolution={1,1};
    material.masks.atlasExtent={4,4};material.masks.tileSize=2;
    material.masks.high={{0,~0u}};material.masks.low={{32,1}};
    material.masks.records.resize(33);material.masks.atlas.resize(16,255);
    set.materials={material,material};set.materials[1].terrain=true;
    const auto packed=packWorldLayerMaterialSet(set);
    require(packed.words[0]==1&&packed.words[1]==2&&packed.words[2]==3,"Packed set header");
    require(packed.words[5]==~0u&&packed.words[4+32+14]==1,"Packed material headers");
    require(packed.words[6]>=68&&packed.words[6]+32*32<=packed.words.size(),"Packed layer table range");
    require(packed.textureBytes==92,"Exact retained mip accounting");
    const auto rejects=[&](const WorldLayerMaterialSet& value) {
        try{(void)packWorldLayerMaterialSet(value);return false;}catch(const std::invalid_argument&){return true;}
    };
    auto invalid=set;invalid.textures[0].srgb=false;require(rejects(invalid),"Colour map must be sRGB");
    invalid=set;invalid.textures[1].srgb=true;require(rejects(invalid),"Normal/data map must be linear");
    invalid=set;invalid.textures[2].mips.pop_back();require(rejects(invalid),"Incomplete authored mips");
    invalid=set;invalid.textures[2].mips[1].width=3;require(rejects(invalid),"Malformed mip dimensions");
    invalid=set;invalid.textures[2].mips[0].rgba[0]=0;require(rejects(invalid),"Mask upload must equal validated atlas");
    invalid=set;invalid.materials[0].tiling[15]=0;require(rejects(invalid),"Invalid terrain tiling");
    invalid=set;invalid.materials[0].selectedLayers[1]=32;require(rejects(invalid),"Invalid ground attribute layer range");
    invalid=set;invalid.materials[0].uvScale[1]=std::numeric_limits<float>::infinity();require(rejects(invalid),"Nonfinite projection");
    invalid=set;invalid.textures[1].identity=invalid.textures[0].identity;require(rejects(invalid),"Ambiguous texture identity");
    invalid=set;invalid.materials[0].globalNormal=99;require(rejects(invalid),"Missing optional texture");
    std::cout<<"PASS layered records, 128 sparse high/low selections, bit 31, periodic and non-power-of-two boundaries, invalid payload rejection\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
