#include "EffectHost.h"

#include <glm/matrix.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>

namespace vfx::runtime {
namespace {

bool fail(std::string* error, const char* message) {
    if (error) *error = message;
    return false;
}

void succeed(std::string* error) {
    if (error) error->clear();
}

bool finite(const glm::mat4& matrix) {
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            if (!std::isfinite(matrix[column][row])) return false;
    return true;
}

bool lifetimeValid(float seconds) {
    return std::isfinite(seconds) && seconds > 0.0f && seconds <= 60.0f;
}

} // namespace

EffectHost::EffectHost() : owner_(std::this_thread::get_id()) {}

EffectHost::~EffectHost() {
    if (owner_ != std::this_thread::get_id()) std::terminate();
    stopAll();
}

void EffectHost::checkOwner() const {
    if (owner_ != std::this_thread::get_id())
        throw std::logic_error("EffectHost used outside its owner thread");
}

bool EffectHost::spawn(std::unique_ptr<HostedEffect> effect,
                       const glm::mat4& world, float lifetimeSeconds,
                       EffectId& out, std::string* error) {
    checkOwner();
    if (!effect || !finite(world) || !lifetimeValid(lifetimeSeconds))
        return fail(error, "invalid effect spawn request");
    if (instances_.size() >= 256u ||
        nextId_ == (std::numeric_limits<EffectId>::max)())
        return fail(error, "effect host capacity exhausted");
    const EffectId id = nextId_;
    instances_.push_back({id, std::move(effect), world, glm::mat4(1.0f),
                          0, lifetimeSeconds});
    ++nextId_;
    out = id;
    succeed(error);
    return true;
}

bool EffectHost::attach(EffectId effect, AnchorId anchor,
                        const glm::mat4& local, std::string* error) {
    checkOwner();
    if (anchor == 0 || !finite(local))
        return fail(error, "invalid effect attachment");
    const auto it = std::find_if(instances_.begin(), instances_.end(),
        [effect](const Instance& item) { return item.id == effect; });
    if (it == instances_.end()) return fail(error, "effect is unavailable");
    it->anchor = anchor;
    it->local = local;
    succeed(error);
    return true;
}

bool EffectHost::detach(EffectId effect, std::string* error) {
    checkOwner();
    const auto it = std::find_if(instances_.begin(), instances_.end(),
        [effect](const Instance& item) { return item.id == effect; });
    if (it == instances_.end()) return fail(error, "effect is unavailable");
    it->anchor = 0;
    it->local = glm::mat4(1.0f);
    succeed(error);
    return true;
}

bool EffectHost::update(float dt, std::span<const EffectAnchor> anchors,
                        std::string* error) {
    checkOwner();
    if (!std::isfinite(dt) || dt < 0.0f || dt > 1.0f || anchors.size() > 256u)
        return fail(error, "invalid effect frame");
    for (std::size_t i = 0; i < anchors.size(); ++i) {
        if (anchors[i].id == 0 || !finite(anchors[i].world))
            return fail(error, "invalid effect anchor");
        for (std::size_t j = 0; j < i; ++j)
            if (anchors[j].id == anchors[i].id)
                return fail(error, "duplicate effect anchor");
    }
    for (Instance& item : instances_) {
        if (item.anchor != 0) {
            const auto it = std::find_if(anchors.begin(), anchors.end(),
                [&item](const EffectAnchor& anchor) { return anchor.id == item.anchor; });
            if (it == anchors.end()) {
                item.effect->stop();
                item.remaining = 0.0f;
                continue;
            }
            item.world = it->world * item.local;
            if (!finite(item.world)) {
                item.effect->stop();
                item.remaining = 0.0f;
                continue;
            }
        }
        item.remaining -= dt;
        if (item.remaining <= 0.0f) {
            item.effect->stop();
            continue;
        }
        item.effect->update(dt, item.world);
    }
    std::erase_if(instances_, [](const Instance& item) { return item.remaining <= 0.0f; });
    succeed(error);
    return true;
}

bool EffectHost::stop(EffectId effect, std::string* error) {
    checkOwner();
    const auto it = std::find_if(instances_.begin(), instances_.end(),
        [effect](const Instance& item) { return item.id == effect; });
    if (it == instances_.end()) return fail(error, "effect is unavailable");
    it->effect->stop();
    instances_.erase(it);
    succeed(error);
    return true;
}

void EffectHost::stopAll() {
    checkOwner();
    for (Instance& item : instances_) item.effect->stop();
    instances_.clear();
}

std::size_t EffectHost::activeCount() const {
    checkOwner();
    return instances_.size();
}

} // namespace vfx::runtime
