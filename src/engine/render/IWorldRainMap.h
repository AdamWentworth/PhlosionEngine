#pragma once
#include "engine/render/IWorldDirectionalShadow.h"
#include "engine/render/IWorldResourceScope.h"
#include <string>
#include <vector>

namespace engine::render {
// Independent persistent weather depth. Shadow drawing pipelines are shared;
// weather targets and their publication never replace lighting shadow contents.
struct WorldRainMapDiagnostic {unsigned resolution=0;std::vector<float> depth;};
class IWorldRainMap {
public:
    virtual ~IWorldRainMap()=default;
    virtual void prewarmWorldRainMap(WorldResourceScopeId,unsigned resolution)=0;
    virtual bool beginWorldRainMap(WorldResourceScopeId,const WorldDirectionalShadowView&,bool clear)=0;
    virtual bool endWorldRainMap()=0;
    virtual bool publishWorldRainMap(WorldResourceScopeId,unsigned cascade)=0;
    virtual std::string worldRainMapTexture(WorldResourceScopeId) const=0;
    virtual bool captureWorldRainMap(WorldResourceScopeId)=0;
    virtual WorldRainMapDiagnostic readWorldRainMap()=0;
};
}
