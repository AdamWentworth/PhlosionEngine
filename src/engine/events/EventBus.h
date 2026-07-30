// src/engine/events/EventBus.h
#pragma once

#include "Event.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

// Thread-safe event bus with token-based unsubscription.
// Use via EngineServices/CoreServices (no legacy EventManager wrapper).

struct EventTypeHash {
    std::size_t operator()(EventType t) const noexcept {
        return static_cast<std::size_t>(t);
    }
};

class EventBus {
public:
    using Listener = std::function<void(const Event&)>;
    using SubscriptionId = std::uint64_t;

    EventBus() = default;
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    SubscriptionId subscribe(EventType type, Listener listener) {
        std::lock_guard<std::mutex> lock(mutex_);
        const SubscriptionId id = ++nextId_;
        listeners_[type].push_back(Entry{id, std::move(listener)});
        return id;
    }

    bool unsubscribe(EventType type, SubscriptionId id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = listeners_.find(type);
        if (it == listeners_.end()) return false;

        auto& vec = it->second;
        const std::size_t before = vec.size();
        vec.erase(
            std::remove_if(vec.begin(), vec.end(),
                           [&](const Entry& e) { return e.id == id; }),
            vec.end()
        );
        return vec.size() != before;
    }

    void emit(const Event& event) const {
        std::vector<Listener> snapshot;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = listeners_.find(event.getType());
            if (it == listeners_.end()) return;

            snapshot.reserve(it->second.size());
            for (const auto& e : it->second) snapshot.push_back(e.fn);
        }
        for (const auto& fn : snapshot) fn(event);
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        listeners_.clear();
    }

private:
    struct Entry {
        SubscriptionId id;
        Listener fn;
    };

    mutable std::mutex mutex_;
    mutable std::unordered_map<EventType, std::vector<Entry>, EventTypeHash> listeners_;
    SubscriptionId nextId_ = 0;
};
