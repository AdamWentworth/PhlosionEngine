#pragma once

#include <glm/mat4x4.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace vfx::runtime {

using EffectId = std::uint64_t;
using AnchorId = std::uint64_t;

// A game supplies adapters for its chosen effect implementations. Callbacks
// must not throw; stop is called once before the host releases an effect.
class HostedEffect {
public:
    virtual ~HostedEffect() = default;
    virtual void update(float dt, const glm::mat4& world) noexcept = 0;
    virtual void stop() noexcept = 0;
};

struct EffectAnchor {
    AnchorId id = 0;
    glm::mat4 world{1.0f};
};

class EffectHost {
public:
    EffectHost();
    ~EffectHost();
    EffectHost(const EffectHost&) = delete;
    EffectHost& operator=(const EffectHost&) = delete;

    bool spawn(std::unique_ptr<HostedEffect> effect, const glm::mat4& world,
               float lifetimeSeconds, EffectId& out,
               std::string* error = nullptr);
    bool attach(EffectId effect, AnchorId anchor, const glm::mat4& local,
                std::string* error = nullptr);
    bool detach(EffectId effect, std::string* error = nullptr);
    bool update(float dt, std::span<const EffectAnchor> anchors,
                std::string* error = nullptr);
    bool stop(EffectId effect, std::string* error = nullptr);
    void stopAll();
    std::size_t activeCount() const;

private:
    struct Instance {
        EffectId id;
        std::unique_ptr<HostedEffect> effect;
        glm::mat4 world{1.0f};
        glm::mat4 local{1.0f};
        AnchorId anchor = 0;
        float remaining = 0.0f;
    };
    void checkOwner() const;
    const std::thread::id owner_;
    std::vector<Instance> instances_;
    EffectId nextId_ = 1;
};

} // namespace vfx::runtime
