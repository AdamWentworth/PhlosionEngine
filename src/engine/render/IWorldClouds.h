#pragma once
#include "engine/render/IRenderBackend.h"
#include "engine/render/IWorldResourceScope.h"
#include "engine/render/WorldTemporalScene.h"
#include "engine/render/WorldClouds.h"
namespace engine::render {
struct WorldCloudView {
    WorldTemporalView view;
    WorldEnvironmentLighting environment;
    WorldResourceScopeId owner=0;
    unsigned quality=1;
    IRenderBackend::WorldTextureData weather,noise;
};
struct WorldCloudDiagnostic {
    unsigned width=0,height=0,shadowResolution=0,visibleTiles=0;
    std::uint64_t bytes=0,owner=0,identity=0,frame=0;
    std::vector<std::array<float,4>> shadows,integrated,resolved,skyModel;
};
class IWorldClouds {
public:
    virtual ~IWorldClouds()=default;
    virtual void prepareWorldClouds(const WorldCloudView&)=0;
    virtual void resolveWorldClouds(const WorldCloudView&)=0;
    virtual void clearWorldClouds()=0;
    virtual bool captureWorldClouds()=0;
    virtual WorldCloudDiagnostic readWorldClouds()=0;
};
WorldCloudRows packWorldClouds(const WorldCloudView&);
WorldCloudUniforms cloudConsumerUniforms(const WorldCloudView&,bool resolved);
}
