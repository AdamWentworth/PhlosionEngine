#pragma once

#include "engine/render/WorldDeformation.h"
#include "engine/render/WorldTemporalMaterial.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace engine::render {
using WorldMotionMatrix = std::array<float, 16>;
inline constexpr WorldMotionMatrix worldMotionIdentity{
    1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};

// Identity belongs to the scene owner; part is an authored primitive/LOD, never
// a draw index or a position in a compacted visible-instance array.
struct WorldMotionIdentity {
    std::uint64_t object = 0, part = 0, instance = 0;
    bool operator==(const WorldMotionIdentity&) const = default;
};
struct WorldMotionIdentityHash {
    std::size_t operator()(WorldMotionIdentity id) const noexcept {
        return std::hash<std::uint64_t>{}(id.object) ^
            (std::hash<std::uint64_t>{}(id.part) + 0x9e3779b9u +
             (id.object << 6) + (id.object >> 2)) ^ std::hash<std::uint64_t>{}(id.instance);
    }
};

// Frame counts rendered primary views, not simulation ticks. Input matrices
// are the actual rendered (possibly jittered) column-major engine clip-space
// projections (-w..w, Y up); consumers must not add jitter a second time.
struct WorldMotionView {
    std::uint64_t identity = 0, generation = 0, frame = 0;
    int width = 0, height = 0;
    WorldMotionMatrix viewProjection = worldMotionIdentity;
    bool reset = false;
};
struct WorldMotionObject {
    WorldMotionIdentity identity;
    std::uint64_t revision = 0;
    WorldTemporalMaterial temporalMaterial = WorldTemporalMaterial::Geometry;
    WorldMotionMatrix model = worldMotionIdentity;
    WorldMotionMatrix instance = worldMotionIdentity;
    std::span<const float> deformation;
    // CPU-deformed local positions, in the immutable geometry vertex order.
    std::span<const std::array<float,3>> positions;
    WorldDeformation deformationVersion;
    std::uint32_t matrixCount = 0, skinningMode = 0, materialMode = 0;
    // Specialized vertex parameters must travel with their deformation state.
    std::array<float, 12> materialParameters{};
};
struct WorldMotionPose {
    WorldMotionMatrix model = worldMotionIdentity;
    WorldMotionMatrix instance = worldMotionIdentity;
    std::vector<float> deformation;
    std::vector<std::array<float,3>> positions;
    WorldDeformation deformationVersion;
    std::uint64_t revision = 0;
    WorldTemporalMaterial temporalMaterial = WorldTemporalMaterial::Geometry;
    std::uint32_t matrixCount = 0, skinningMode = 0, materialMode = 0, imageIdentity = 0;
    std::array<float, 12> materialParameters{};
};
struct WorldMotionSample {
    // Borrowed until commit/abort/release/clear. Never a simulation-owned palette.
    const WorldMotionPose* previous = nullptr;
    std::uint32_t imageIdentity = 0;
    bool valid = false;
};
struct WorldMotionStats {
    std::size_t objects = 0, valid = 0, deformed = 0, deformationBytes = 0;
    std::size_t rigidChanged = 0, deformationChanged = 0, positionVertices = 0;
};

// Previous state is immutable throughout a capture. Only commit promotes the
// captured poses. Shadow, probe and reflection views must not submit here.
class WorldMotionHistory {
public:
    static constexpr std::size_t maxViews = 8, maxObjects = 16384, maxPositionVertices = 1048576;
    WorldMotionHistory() = default;
    WorldMotionHistory(const WorldMotionHistory&) = delete;
    WorldMotionHistory& operator=(const WorldMotionHistory&) = delete;
    WorldMotionHistory(WorldMotionHistory&&) = delete;
    WorldMotionHistory& operator=(WorldMotionHistory&&) = delete;

    void begin(const WorldMotionView& view) {
        if (active_) throw std::logic_error("Nested motion view");
        if (!view.identity || !view.generation || !view.frame || view.width <= 0 || view.height <= 0)
            throw std::invalid_argument("Invalid motion view identity/extent/frame");
        finite(view.viewProjection);
        auto found = views_.find(view.identity);
        if (found == views_.end()) {
            if (views_.size() >= maxViews) throw std::runtime_error("Motion view budget exhausted");
            found = views_.try_emplace(view.identity).first;
        }
        auto& state = found->second;
        if (state.seenFrame && view.frame <= state.seenFrame)
            throw std::invalid_argument("Motion frame must advance once per rendered view");
        state.seenFrame = view.frame;
        validView_ = state.committed && !view.reset && view.generation == state.view.generation &&
            view.width == state.view.width && view.height == state.view.height &&
            view.frame - state.view.frame == 1;
        currentView_ = view;
        previousViewProjection_ = validView_ ? state.view.viewProjection : view.viewProjection;
        pending_.clear();
        pendingStats_ = {};
        active_ = &state;
    }

    WorldMotionSample submit(const WorldMotionObject& object) {
        if (!active_) throw std::logic_error("Motion object outside a view");
        if (!object.identity.object || !object.revision || !validWorldTemporalMaterial(object.temporalMaterial))
            throw std::invalid_argument("Motion object requires stable identity/revision");
        finite(object.model); finite(object.instance); finite(object.materialParameters);
        if (object.skinningMode > 1 || object.matrixCount > 128 ||
            object.deformation.size() != std::size_t(object.matrixCount) * (object.skinningMode ? 32 : 16))
            throw std::invalid_argument("Invalid motion deformation layout");
        finite(object.deformation);
        if(object.positions.size()>maxPositionVertices-pendingStats_.positionVertices ||
           (!object.positions.empty() && object.matrixCount))
            throw std::invalid_argument("Invalid CPU motion position budget/layout");
        for(const auto& position:object.positions)finite(position);
        if (pending_.contains(object.identity)) throw std::invalid_argument("Duplicate motion identity in view");
        if (pending_.size() >= maxObjects) throw std::runtime_error("Motion object budget exhausted");
        const WorldMotionPose* old = nullptr;
        if (validView_) {
            const auto it = active_->objects.find(object.identity);
            if (it != active_->objects.end() && it->second.revision == object.revision &&
                it->second.matrixCount == object.matrixCount && it->second.skinningMode == object.skinningMode &&
                it->second.materialMode == object.materialMode &&
                it->second.positions.size() == object.positions.size() &&
                it->second.temporalMaterial == object.temporalMaterial) old = &it->second;
        }
        if (!old && nextImageIdentity_ == std::numeric_limits<std::uint32_t>::max())
            throw std::runtime_error("Motion image identity budget exhausted");
        const auto id = old ? old->imageIdentity : nextImageIdentity_;
        WorldMotionPose pose;
        pose.model = object.model; pose.instance = object.instance;
        pose.deformation.assign(object.deformation.begin(), object.deformation.end());
        pose.positions.assign(object.positions.begin(),object.positions.end());
        pose.deformationVersion=object.deformationVersion;
        if(pose.deformationVersion && (pose.deformationVersion->values().size()!=object.deformation.size() ||
            !std::equal(pose.deformationVersion->values().begin(),pose.deformationVersion->values().end(),object.deformation.begin())))
            throw std::logic_error("Motion version differs from submitted pose");
        pose.revision = object.revision; pose.matrixCount = object.matrixCount;
        pose.skinningMode = object.skinningMode; pose.materialMode = object.materialMode;
        pose.materialParameters = object.materialParameters; pose.imageIdentity = id;
        pose.temporalMaterial=object.temporalMaterial;
        auto& current = pending_.emplace(object.identity, std::move(pose)).first->second;
        if (!old) ++nextImageIdentity_;
        ++pendingStats_.objects;
        pendingStats_.valid += old != nullptr;
        pendingStats_.deformed += object.matrixCount != 0;
        pendingStats_.deformationBytes += object.deformation.size_bytes()+object.positions.size_bytes();
        pendingStats_.positionVertices += object.positions.size();
        if (old) {
            pendingStats_.rigidChanged += old->model != object.model || old->instance != object.instance;
            pendingStats_.deformationChanged += !std::equal(old->deformation.begin(),
                old->deformation.end(), object.deformation.begin(), object.deformation.end()) ||
                !std::equal(old->positions.begin(),old->positions.end(),object.positions.begin(),object.positions.end());
        }
        return {old ? old : &current, id, old != nullptr};
    }

    void commit() {
        if (!active_) throw std::logic_error("No motion view to commit");
        active_->objects = std::move(pending_);
        active_->view = currentView_; active_->committed = true;
        stats_ = pendingStats_;
        active_ = nullptr; validView_ = false;
    }
    void abort() noexcept { pending_.clear(); active_ = nullptr; validView_ = false; }
    void release(std::uint64_t view) {
        if (active_) throw std::logic_error("Cannot release motion history during capture");
        views_.erase(view);
    }
    void clear() noexcept { abort(); views_.clear(); nextImageIdentity_ = 1; stats_ = {}; }
    std::vector<WorldSkinPalette> deformationPalettes() const {
        std::vector<WorldSkinPalette> result;
        for(const auto& [id,view]:views_)for(const auto& [key,pose]:view.objects)
            if(!pose.deformation.empty()) {
                if(pose.deformationVersion)result.emplace_back(pose.deformationVersion);
                else result.emplace_back(pose.deformation);
            }
        return result;
    }
    bool active() const noexcept { return active_ != nullptr; }
    bool validView() const noexcept { return validView_; }
    const WorldMotionMatrix& previousViewProjection() const {
        if (!active_) throw std::logic_error("No active motion camera");
        return previousViewProjection_;
    }
    WorldMotionStats stats() const noexcept { return stats_; }
    // Qualification lookup; no per-frame GPU readback is needed by consumers.
    std::vector<std::uint32_t> imageIdentities(std::uint64_t view, std::uint64_t object) const {
        std::vector<std::uint32_t> result;
        if(auto it=views_.find(view);it!=views_.end())
            for(const auto& [key,pose]:it->second.objects)
                if(key.object==object) result.push_back(pose.imageIdentity);
        std::sort(result.begin(),result.end());
        return result;
    }
    std::size_t objectCount(std::uint64_t view) const noexcept {
        auto it = views_.find(view); return it == views_.end() ? 0 : it->second.objects.size();
    }
private:
    template<class Range> static void finite(const Range& values) {
        for (float f : values) if (!std::isfinite(f))
            throw std::invalid_argument("Nonfinite motion input");
    }
    struct ViewState {
        WorldMotionView view;
        std::uint64_t seenFrame = 0;
        bool committed = false;
        std::unordered_map<WorldMotionIdentity, WorldMotionPose, WorldMotionIdentityHash> objects;
    };
    std::unordered_map<std::uint64_t, ViewState> views_;
    std::unordered_map<WorldMotionIdentity, WorldMotionPose, WorldMotionIdentityHash> pending_;
    ViewState* active_ = nullptr;
    WorldMotionView currentView_;
    WorldMotionMatrix previousViewProjection_ = worldMotionIdentity;
    WorldMotionStats pendingStats_, stats_;
    std::uint32_t nextImageIdentity_ = 1;
    bool validView_ = false;
};

class WorldMotionCapture {
public:
    WorldMotionCapture(WorldMotionHistory& history, const WorldMotionView& view) : history_(&history) {
        history_->begin(view);
    }
    WorldMotionCapture(const WorldMotionCapture&) = delete;
    WorldMotionCapture& operator=(const WorldMotionCapture&) = delete;
    ~WorldMotionCapture() { if (history_) history_->abort(); }
    void commit() {
        if (!history_) throw std::logic_error("Motion capture already completed");
        history_->commit(); history_ = nullptr;
    }
private:
    WorldMotionHistory* history_;
};

struct WorldMotionVector {
    std::array<float, 2> previousUvMinusCurrentUv{};
    float previousDepth = 0;
    bool valid = false;
};
// Reference contract for native velocity writers: top-left UV displacement,
// not pixels/sec. Previous depth is 0..1. Behind-eye/near-plane data is rejected.
inline WorldMotionVector projectWorldMotion(const std::array<float, 4>& currentClip,
                                            const std::array<float, 4>& previousClip,
                                            bool historyValid) {
    for (float x : currentClip) if (!std::isfinite(x)) return {};
    for (float x : previousClip) if (!std::isfinite(x)) return {};
    if (!historyValid || currentClip[3] <= 1e-6f || previousClip[3] <= 1e-6f ||
        currentClip[2] < -currentClip[3] || previousClip[2] < -previousClip[3] ||
        currentClip[2] > currentClip[3] || previousClip[2] > previousClip[3]) return {};
    const float px = previousClip[0] / previousClip[3], py = previousClip[1] / previousClip[3];
    const float cx = currentClip[0] / currentClip[3], cy = currentClip[1] / currentClip[3];
    const float dx=(px-cx)*.5f,dy=(cy-py)*.5f;
    if (!std::isfinite(dx) || !std::isfinite(dy)) return {};
    return {{dx,dy}, (previousClip[2] / previousClip[3] + 1) * .5f, true};
}
} // namespace engine::render
