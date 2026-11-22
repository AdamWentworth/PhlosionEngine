#pragma once
#include <stdexcept>
#include <string>
#include <string_view>
namespace engine::render {
inline std::string worldMotionVertexSource(std::string source) {
    const auto replace=[](std::string& s,std::string_view from,std::string_view to) {
        std::size_t p=0;
        while((p=s.find(from,p))!=std::string::npos){s.replace(p,from.size(),to);p+=to.size();}
    };
    const auto vs=source.find("struct VSOut {"), end=source.find("};",vs);
    const auto main=source.find("VSOut main(");
    if(vs==std::string::npos||end==std::string::npos||main==std::string::npos)
        throw std::runtime_error("Motion vertex template contract changed");
    source.insert(end,"float4 motionCurrent : TEXCOORD7; float4 motionPrevious : TEXCOORD8; nointerpolation uint motionId : TEXCOORD9; nointerpolation uint motionValid : TEXCOORD10;");
    const auto bodyStart=source.find("VSOut main(");
    auto body=source.substr(bodyStart);source.erase(bodyStart);
    replace(body,"VSOut main(VSIn i, uint instanceId : SV_InstanceID)","VSOut evaluateMotionVertex(VSIn i, uint instanceId, bool previous)");
    replace(body,"uModel","evaluatedModel");replace(body,"uViewProj","evaluatedVP");
    replace(body,"uMaterialTimeSecVs","evaluatedTiming.x");replace(body,"uMaterialFlagsVs","evaluatedTiming.y");
    replace(body,"uMaterialAtlasWidthVs","evaluatedTiming.z");replace(body,"uMaterialAtlasHeightVs","evaluatedTiming.w");
    replace(body,"uMaterialRect0Vs","evaluatedRect0");replace(body,"uMaterialRect1Vs","evaluatedRect1");
    replace(body,"VSOut o;",R"(
        motionPreviousEvaluation=previous;
        float4x4 evaluatedModel=uModel,evaluatedVP=uViewProj;
        float4 evaluatedTiming=float4(uMaterialTimeSecVs,uMaterialFlagsVs,uMaterialAtlasWidthVs,uMaterialAtlasHeightVs);
        float4 evaluatedRect0=uMaterialRect0Vs,evaluatedRect1=uMaterialRect1Vs;
        VSOut o=(VSOut)0;
    )");
    replace(body,"float4 skinMeta = resolveSkinMeta(inst);",R"(
        float4 skinMeta=resolveSkinMeta(inst);
        if(previous) {
            evaluatedModel=motionLoadMatrix(0);evaluatedVP=motionLoadMatrix(8);
            inst.model0=gMotionWords[motionPacket+4];inst.model1=gMotionWords[motionPacket+5];
            inst.model2=gMotionWords[motionPacket+6];inst.model3=gMotionWords[motionPacket+7];
            float4 meta=gMotionWords[motionPacket+15];
            skinMeta=float4(meta.x>0?1:0,meta.x,meta.y,0);
            evaluatedTiming=gMotionWords[motionPacket+12];
            evaluatedRect0=gMotionWords[motionPacket+13];evaluatedRect1=gMotionWords[motionPacket+14];
        }
    )");
    source="#define PHLOSION_MOTION_VERTEX 1\n"+source+body+R"(
VSOut main(VSIn i,uint instanceId:SV_InstanceID,uint vertexId:SV_VertexID) {
    bool enrolled=instanceId<(uint)gMotionWords[0].x;
    uint id=0,valid=0;
    float4 oldClip=0;
    if(enrolled) {
        float4 packetHeader=gMotionWords[1+instanceId];
        motionPacket=(uint)packetHeader.x;
        motionPaletteWordBase=(uint)packetHeader.y;motionExternalPalette=packetHeader.z>.5;
        float4 meta=gMotionWords[motionPacket+15];
        uint high=(uint)meta.w;valid=high>>16;id=(uint)meta.z|((high&65535u)<<16);
        VSIn oldInput=i;
        uint positions=(uint)packetHeader.w;
        if(positions) {
            if(vertexId<(uint)gMotionWords[positions].x)oldInput.pos=gMotionWords[positions+1+vertexId].xyz;
            else valid&=~1u;
        }
        VSOut old=evaluateMotionVertex(oldInput,instanceId,true);oldClip=old.pos;
        if(worldFoliageDepthProxy)oldClip.z=worldFoliageOriginalClipZ;
    }
    VSOut result=evaluateMotionVertex(i,instanceId,false);
    float4 currentClip=result.pos;
    if(worldFoliageDepthProxy)currentClip.z=worldFoliageOriginalClipZ;
    result.motionCurrent=float4(currentClip.xy,2*currentClip.z-currentClip.w,currentClip.w);
    result.motionPrevious=enrolled?float4(oldClip.xy,2*oldClip.z-oldClip.w,oldClip.w):result.motionCurrent;
    result.motionId=id;result.motionValid=valid;
    return result;
}
)";
    return source;
}
}
