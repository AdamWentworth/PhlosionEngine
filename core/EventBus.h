// src/engine/core/EventBus.h

#pragma once
#include "engine/core/IEventBus.h"
#include <unordered_map>
#include <vector>
#include <mutex>

namespace engine {

// Simple in-process event bus implementation.
// - Not optimized.
// - Safe enough for single-threaded gameplay usage.
// - Ownership: composition root / world services.
class EventBus final : public IEventBus {
public:
    void publish(std::uint64_t type, const void* payload, std::size_t size) override;
    HandlerId subscribe(std::uint64_t type,
                        std::function<void(const void* payload, std::size_t size)> fn) override;
    void unsubscribe(HandlerId id) override;

private:
    struct Handler {
        HandlerId id{};
        std::uint64_t type{};
        std::function<void(const void*, std::size_t)> fn;
    };

    std::mutex m_;
    HandlerId next_{1};
    std::unordered_map<std::uint64_t, std::vector<Handler>> byType_;
    std::unordered_map<HandlerId, std::uint64_t> idToType_;
};

} // namespace engine
