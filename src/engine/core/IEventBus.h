// src/engine/core/IEventBus.h

#pragma once
#include <cstdint>
#include <functional>

namespace engine {

// Minimal, type-erased event bus for core. Designed to be owned/injected.
// Later you can replace with a faster typed dispatcher.
class IEventBus {
public:
    using HandlerId = std::uint64_t;

    virtual ~IEventBus() = default;

    // Publish raw payload bytes with a numeric type id.
    // (Later you can add typed templates on top.)
    virtual void publish(std::uint64_t type, const void* payload, std::size_t size) = 0;

    // Subscribe to raw payload bytes; returns handler id for unsubscription.
    virtual HandlerId subscribe(std::uint64_t type,
                                std::function<void(const void* payload, std::size_t size)> fn) = 0;

    virtual void unsubscribe(HandlerId id) = 0;
};

} // namespace engine
