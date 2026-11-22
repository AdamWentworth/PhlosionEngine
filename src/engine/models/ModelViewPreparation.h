#pragma once
#include "ModelInstance.h"
#include "ModelLighting.h"
#include "engine/render/IWorldDeformationPreparation.h"
#include <cstring>
#include <type_traits>

namespace engine::models {
// Collection-owned CPU descriptions only. Pose/geometry/material inputs belong
// to the collected frame; projection, native pass state and motion remain live.
class ModelViewPreparation {
public:
    static constexpr std::size_t capacity=16;
    ModelViewPreparation()=default;
    explicit ModelViewPreparation(ModelCollectedFrame frame):frame_(std::move(frame)){}
    struct Stats { std::uint64_t requests=0, builds=0, reuses=0; } stats;
    struct Request {
        glm::vec3 position,up,facing;
        render::WorldEnvironmentLighting environment;
        int preset=0;
        bool instancing=false;
    };
    struct View {
        std::vector<batches::WorldIndexedBatch> batches;
        mutable std::array<std::unique_ptr<batches::PreparedWorldIndexedSubmission>,2> plans;
        mutable std::uint64_t requests=0;
        void prepare(bool instancing) const {
            if(!plans[0])plans[0]=std::make_unique<batches::PreparedWorldIndexedSubmission>(batches,false);
            if(instancing && !plans[1])plans[1]=std::make_unique<batches::PreparedWorldIndexedSubmission>(batches,true);
        }
        const batches::PreparedWorldIndexedSubmission& submission(const IRenderBackend& renderer,bool allowAutoInstance,bool preparedOnly=false)const {
            const auto index=allowAutoInstance && renderer.supportsWorldIndexedMeshInstancing() ? 1u : 0u;
            ++requests;
            if(!plans[index]) {
                if(preparedOnly)throw std::logic_error("Unprepared model submission policy");
                plans[index]=std::make_unique<batches::PreparedWorldIndexedSubmission>(renderer,batches,index!=0);
            }
            return *plans[index];
        }
    };
    struct SubmissionStats { std::uint64_t requests=0,plans=0,batches=0; };
    SubmissionStats submissionStats()const {
        SubmissionStats result;
        for(const auto& entry:entries_) {
            result.requests+=entry->view.requests;
            for(const auto& plan:entry->view.plans)if(plan){++result.plans;result.batches+=plan->drawCount();}
        }
        return result;
    }
    bool plan(const Request& request,std::string& error) {
        const Key key{request.position,request.up,request.facing,request.environment,request.preset};
        for(auto& existing:planned_)if(Key{existing.position,existing.up,existing.facing,existing.environment,existing.preset}==key) {
            existing.instancing|=request.instancing;error.clear();return true;
        }
        if(planned_.size()==capacity){error="Too many admitted native model views";return false;}
        planned_.push_back(request);error.clear();return true;
    }
    std::size_t plannedCount()const{return planned_.size();}
    void appendDeformations(std::vector<render::WorldSkinPalette>& output) const {
        for(const auto& entry:entries_)for(const auto& batch:entry->view.batches) {
            if(!batch.gpuSkinning || !batch.skinMatrixCount)continue;
            const auto count=std::size_t(batch.skinMatrixCount)*(batch.gpuSkinningMode?32u:16u);
            const float* data=batch.sharedSkinMatrices?batch.sharedSkinMatrices:batch.skinMatrices.data();
            if(!batch.sharedSkinMatrices && count>batch.skinMatrices.size())
                throw std::logic_error("Incomplete collected deformation palette");
            if(batch.deformation)output.emplace_back(batch.deformation);
            else output.emplace_back(data,count);
        }
    }
    bool preparePlanned(const ModelLighting& lighting,std::string& error) {
        for(const auto& request:planned_) {
            auto* view=resolve(request.position,request.up,request.facing,request.environment,lighting,request.preset,error);
            if(!view)return false;
            view->prepare(request.instancing);
        }
        error.clear();return true;
    }
    const View* resolve(
        const glm::vec3& position,
        const glm::vec3& up, const glm::vec3& facing,
        const render::WorldEnvironmentLighting& environment,
        const ModelLighting& lighting, int preset, std::string& error,bool allowCreate=true) {
        ++stats.requests;
        Key key{position,up,facing,environment,preset};
        for(const auto& entry:entries_) if(entry->key==key) {
            ++stats.reuses;error.clear();return &entry->view;
        }
        if(!allowCreate){error="Unplanned native model view";return nullptr;}
        auto next=std::make_unique<Entry>(key);
        if(!frame_.prepareView(position,up,facing,next->view.batches,&error))return nullptr;
        for(auto& batch:next->view.batches)batch.environment=environment;
        if(lighting.prepare && !lighting.prepare(next->view.batches,preset,error))return nullptr;
        ++stats.builds;
        const auto* result=&next->view;
        if(entries_.size()<capacity)entries_.push_back(std::move(next));
        else {entries_[replace_]=std::move(next);replace_=(replace_+1)%capacity;}
        error.clear();return result;
    }
private:
    struct Key {
        std::array<float,9> camera;
        std::array<std::byte,sizeof(render::WorldEnvironmentLighting)> environment;
        int preset;
        Key(const glm::vec3& p,const glm::vec3& u,const glm::vec3& f,
            const render::WorldEnvironmentLighting& e,int mode):
            camera{p.x,p.y,p.z,u.x,u.y,u.z,f.x,f.y,f.z},preset(mode) {
            static_assert(std::is_trivially_copyable_v<render::WorldEnvironmentLighting>);
            std::memcpy(environment.data(),&e,sizeof(e));
        }
        bool operator==(const Key& other)const {
            // Copied representations can miss on padding, never falsely reuse
            // different values (the same rule as WorldEnvironmentPacketCache).
            return preset==other.preset && environment==other.environment &&
                std::memcmp(camera.data(),other.camera.data(),sizeof(camera))==0;
        }
    };
    struct Entry {
        Key key;
        View view;
        explicit Entry(Key value):key(std::move(value)){}
    };
    std::vector<Request> planned_;
    ModelCollectedFrame frame_;
    std::vector<std::unique_ptr<Entry>> entries_;
    std::size_t replace_=0;
};
} // namespace engine::models
