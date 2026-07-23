// src/engine/events/Subscription.h
#pragma once

#include "EventBus.h"
#include "Event.h"

#include <utility>

// RAII helper: unsubscribes when destroyed.
// Intended for subs that are naturally scoped (e.g., UI panels).

class Subscription {
public:
    Subscription() = default;

    Subscription(EventBus* bus, EventType type, EventBus::SubscriptionId id)
        : bus_(bus), type_(type), id_(id) {}

    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;

    Subscription(Subscription&& other) noexcept { *this = std::move(other); }
    Subscription& operator=(Subscription&& other) noexcept {
        if (this == &other) return *this;
        reset();
        bus_ = other.bus_;
        type_ = other.type_;
        id_ = other.id_;
        other.bus_ = nullptr;
        other.id_ = 0;
        return *this;
    }

    ~Subscription() { reset(); }

    void reset() {
        if (bus_ && id_ != 0) {
            bus_->unsubscribe(type_, id_);
        }
        bus_ = nullptr;
        id_ = 0;
    }

    [[nodiscard]] bool valid() const { return bus_ != nullptr && id_ != 0; }
    [[nodiscard]] EventBus::SubscriptionId id() const { return id_; }
    [[nodiscard]] EventType type() const { return type_; }

private:
    EventBus* bus_ = nullptr;
    EventType type_ = static_cast<EventType>(0);
    EventBus::SubscriptionId id_ = 0;
};
