#pragma once
#include "engine/render/WorldPlacedProbeCache.h"
#include "engine/render/IWorldProbeScheduling.h"
#include "engine/render/IWorldSurfaceBuffer.h"
#include "engine/render/RenderPhaseProfile.h"
#include <atomic>
#include <vector>
#include <tuple>
#include <ranges>
namespace engine::render {
struct WorldProbeScheduleStats {
    unsigned captures=0,faces=0,filterFrames=0,published=0,waitingForBlend=0;
    unsigned globalFaces=0,globalFilterFrames=0,globalPublished=0;
};
class WorldPlacedProbes {
    struct Record {std::uint64_t id=0,finished=0;WorldLocalReflectionUniforms placement{};float farPlane=0;};
    std::array<Record,4> records_{};
    std::vector<std::uint64_t> plannedIds_;
    std::vector<WorldLocalReflectionProbe> plannedProbes_;
    std::vector<WorldLocalReflectionFace> plannedViews_;
    std::array<float,3> plannedCamera_{};
    unsigned index_=0,face_=0,globalFace_=0;
    std::uint64_t globalGeneration_=0;
    std::uint64_t active_=0,generation_=0,revision_=0,clock_=0,plannedRevision_=0;
    double seconds_=-1;
    bool planned_=false,prewarm_=false;
    WorldProbeScheduleStats stats_{};
    inline static std::atomic<std::uint64_t> nextGeneration_{1};
public:
    const WorldProbeScheduleStats& stats() const{return stats_;}
    std::vector<WorldLocalReflectionFace> captureViews(IRenderBackend& renderer,
        std::span<const std::uint64_t> ids,std::span<const WorldLocalReflectionProbe> probes,
        std::uint64_t revision,std::array<float,3> camera={},double seconds=0) {
        auto* surfaces=dynamic_cast<IWorldSurfaceBuffer*>(&renderer);
        auto* scheduling=dynamic_cast<IWorldProbeScheduling*>(&renderer);
        auto* radiance=dynamic_cast<IWorldLocalReflection*>(&renderer);
        if(!surfaces||!scheduling||!radiance||ids.empty()||ids.size()!=probes.size()||ids.size()>4||!revision||!std::isfinite(seconds))
            throw std::invalid_argument("Invalid placed probe plan");
        for(float v:camera)if(!std::isfinite(v))throw std::invalid_argument("Invalid probe viewer");
        for(auto id:ids)if(id==WorldPlacedProbeCache::globalId)throw std::invalid_argument("Reserved global probe identity");
        for(const auto& p:probes)(void)packWorldLocalReflection(p);
        if(planned_) {
            if(revision!=plannedRevision_ || !std::ranges::equal(ids,plannedIds_) ||
               camera!=plannedCamera_ || seconds!=seconds_)throw std::invalid_argument("Probe plan changed before consumption");
            for(unsigned n=0;n<probes.size();++n)
                if(packWorldLocalReflection(probes[n])!=packWorldLocalReflection(plannedProbes_[n]) ||
                   probes[n].farPlane!=plannedProbes_[n].farPlane)throw std::invalid_argument("Probe placement changed before consumption");
            return plannedViews_;
        }
        // Frame updates still progress while the authored weather clock is paused.
        // Repeated admission within that frame returns above without advancing it twice.
        const float delta=seconds_<0?1.f:std::max(1.f/30.f,float(std::clamp(seconds-seconds_,0.0,.25))*8.f);
        seconds_=seconds;
        if(!scheduling->reserveWorldProbes(ids,delta))throw std::runtime_error("Probe residency unavailable");
        if(revision_!=revision){radiance->invalidateWorldPlacedProbes(ids);active_=generation_=0;face_=0;revision_=revision;}
        ++clock_;stats_={};
        for(auto& r:records_)if(r.id && std::find(ids.begin(),ids.end(),r.id)==ids.end())r={};
        for(auto id:ids)if(std::find_if(records_.begin(),records_.end(),[&](const auto& r){return r.id==id;})==records_.end())
            *std::find_if(records_.begin(),records_.end(),[](const auto& r){return !r.id;})={id,0};
        for(unsigned n=0;n<ids.size();++n) {
            auto& record=*std::find_if(records_.begin(),records_.end(),[&](const auto& r){return r.id==ids[n];});
            const auto placement=packWorldLocalReflection(probes[n]);
            if(record.placement!=placement || record.farPlane!=probes[n].farPlane) {
                if(surfaces->worldProbeSurfaceRevision(ids[n]) && !surfaces->releaseWorldProbeSurface(ids[n]))
                    throw std::runtime_error("Probe relocation could not invalidate material capture");
                radiance->invalidateWorldPlacedProbes(ids.subspan(n,1));
                if(active_==ids[n]){active_=generation_=0;face_=0;}
                record.placement=placement;record.farPlane=probes[n].farPlane;record.finished=0;
            }
        }
        auto found=std::find(ids.begin(),ids.end(),active_);
        if(found==ids.end() || surfaces->worldProbeSurfaceRevision(active_)!=revision) {active_=generation_=0;face_=0;}
        if(!active_) {
            auto priority=[&](unsigned n) {
                const auto r=std::find_if(records_.begin(),records_.end(),[&](const auto& v){return v.id==ids[n];});
                const bool cold=surfaces->worldProbeSurfaceRevision(ids[n])!=revision||!scheduling->worldProbePublished(ids[n]);
                double distance=0;for(int a=0;a<3;++a){const double d=double(probes[n].position[a])-camera[a];distance+=d*d;}
                return std::tuple(!cold,r->finished,distance,ids[n]);
            };
            bool selected=false;
            for(unsigned n=0;n<ids.size();++n) {
                if(scheduling->worldProbeBlending(ids[n]))continue;
                if(!selected || priority(n)<priority(index_)){index_=n;selected=true;}
            }
            if(selected){active_=ids[index_];prewarm_=!scheduling->worldProbePublished(active_);}
            else ++stats_.waitingForBlend;
        } else index_=unsigned(std::find(ids.begin(),ids.end(),active_)-ids.begin());
        plannedIds_.assign(ids.begin(),ids.end());plannedProbes_.assign(probes.begin(),probes.end());
        plannedRevision_=revision;planned_=true;
        std::vector<WorldLocalReflectionFace> result;
        if(active_ && surfaces->worldProbeSurfaceRevision(active_)!=revision)
            for(int face=0;face<6;++face)result.push_back(makeWorldLocalReflectionFace(probes[index_],face));
        plannedViews_=result;plannedCamera_=camera;return result;
    }
    template<class DrawSurface,class DrawSky>
    void update(IRenderBackend& renderer,std::span<const std::uint64_t> ids,
                std::span<const WorldLocalReflectionProbe> probes,std::uint64_t revision,
                const WorldEnvironmentLighting& environment,
                const std::array<std::array<float,4>,32>& emission,unsigned faceBudget,
                DrawSurface drawSurface,DrawSky drawSky) {
        auto* surfaces=dynamic_cast<IWorldSurfaceBuffer*>(&renderer);
        auto* radiance=dynamic_cast<IWorldLocalReflection*>(&renderer);
        auto* scheduling=dynamic_cast<IWorldProbeScheduling*>(&renderer);
        if(!surfaces||!radiance||!scheduling||!planned_||revision!=plannedRevision_||
            !std::ranges::equal(ids,plannedIds_)||probes.size()!=plannedProbes_.size()||faceBudget<1||faceBudget>6)
            throw std::invalid_argument("Probe update differs from admission");
        for(unsigned n=0;n<probes.size();++n)
            if(packWorldLocalReflection(probes[n])!=packWorldLocalReflection(plannedProbes_[n])||
               probes[n].farPlane!=plannedProbes_[n].farPlane)
                throw std::invalid_argument("Probe placement changed after admission");
        planned_=false;
        // A separately owned sky probe supplies directional rough radiance
        // outside local bounds and to local relighting. It has no material capture.
        const auto global=WorldPlacedProbeCache::globalId;
        if(!scheduling->worldProbeBlending(global)) {
            const bool cold=!scheduling->worldProbePublished(global);
            // Cold sky prewarming completes in one update; a lost publication
            // means the renderer was reset, even if the scheduler object survived.
            if(cold){globalFace_=0;globalGeneration_=0;}
            if(!globalGeneration_)globalGeneration_=nextGeneration_.fetch_add(1);
            WorldLocalReflectionProbe sky;sky.sampling=WorldLocalReflectionSampling::PreparedRadiance;
            sky.resolution=WorldPlacedProbeCache::resolution;
            auto lighting=environment;lighting.localHazeDeferred=false;lighting.sky.indirect=true;
            try {
                surfaces->clearWorldSurfaceBuffer();
                RenderPhaseScope phase(renderer,"probe-global-sky");
                for(unsigned n=0;n<(cold?6u:1u) && globalFace_<6;++n) {
                    const auto view=makeWorldLocalReflectionFace(sky,int(globalFace_));
                    if(!radiance->beginWorldPlacedProbeFace(global,globalGeneration_,sky,globalFace_,{}))
                        throw std::runtime_error("Global probe face unavailable");
                    drawSky(view,lighting);
                    if(!radiance->endWorldPlacedProbeFace())throw std::runtime_error("Global probe face failed");
                    ++globalFace_;++stats_.globalFaces;
                }
                if(globalFace_==6) {
                    ++stats_.globalFilterFrames;
                    if(scheduling->filterWorldProbe(global,cold?10u:faceBudget==1?1u:faceBudget==6?4u:2u)) {
                        ++stats_.globalPublished;globalFace_=0;globalGeneration_=0;
                    }
                }
            } catch(...) {
                surfaces->clearWorldSurfaceBuffer();radiance->invalidateWorldPlacedProbes(std::span(&global,1));
                globalFace_=0;globalGeneration_=0;throw;
            }
        }
        if(active_) {
            const auto id=active_;const auto& probe=probes[index_];
            if(surfaces->worldProbeSurfaceRevision(id)!=revision) {
                generation_=0;face_=0;
                RenderPhaseScope phase(renderer,"probe-material-capture");++stats_.captures;
                for(unsigned face=0;face<6;++face) {
                    const auto view=makeWorldLocalReflectionFace(probe,int(face));
                    if(!surfaces->beginWorldProbeSurface(id,revision,face,view.viewProjection.data(),view.resolution))
                        throw std::runtime_error("Probe material capture unavailable");
                    try {drawSurface(view);if(!surfaces->endWorldProbeSurface())throw std::runtime_error("Probe material capture failed");}
                    catch(...){surfaces->clearWorldSurfaceBuffer();throw;}
                }
            }
            if(!generation_)generation_=nextGeneration_.fetch_add(1);
            const unsigned budget=prewarm_?6:faceBudget;
            // The source relighter uses current illumination per face. Use the
            // matching current shadow resources rather than an old lighting snapshot.
            auto lighting=environment;lighting.localHazeDeferred=false;lighting.sky.indirect=true;
            for(unsigned n=0;n<budget && face_<6;++n) {
                const auto view=makeWorldLocalReflectionFace(probe,int(face_));
                try {
                    if(!radiance->beginWorldPlacedProbeFace(id,generation_,probe,face_,ids)||
                       !surfaces->selectWorldProbeSurface(id,face_))throw std::runtime_error("Probe relighting input unavailable");
                    drawSky(view,lighting);
                    if(!surfaces->relightWorldProbeSurface({lighting,view.position,view.forward},emission)||
                       !radiance->endWorldPlacedProbeFace())throw std::runtime_error("Probe relighting failed");
                } catch(...) {surfaces->clearWorldSurfaceBuffer();radiance->invalidateWorldPlacedProbes(std::span(&id,1));active_=generation_=0;face_=0;throw;}
                ++face_;++stats_.faces;
            }
            surfaces->clearWorldSurfaceBuffer();
            if(face_==6) {
                ++stats_.filterFrames;
                const unsigned jobs=prewarm_?10:faceBudget==1?1:faceBudget==6?4:2;
                if(scheduling->filterWorldProbe(id,jobs)) {
                    for(auto& r:records_)if(r.id==id)r.finished=clock_;
                    ++stats_.published;face_=0;generation_=active_=0;
                }
            }
        }
        surfaces->clearWorldSurfaceBuffer();
        if(!radiance->selectWorldPlacedProbes(ids))throw std::runtime_error("Placed probe selection failed");
    }
};
}
