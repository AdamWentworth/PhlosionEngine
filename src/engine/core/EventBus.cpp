// src/engine/core/EventBus.cpp

#include "engine/core/EventBus.h"
#include <algorithm>

namespace engine {

void EventBus::publish(std::uint64_t type, const void* payload, std::size_t size) {
    std::vector<Handler> handlersCopy;
    {
        std::lock_guard<std::mutex> lock(m_);
        auto it = byType_.find(type);
        if (it == byType_.end()) return;
        handlersCopy = it->second; // copy so handlers can unsubscribe safely
    }
    for (auto& h : handlersCopy) {
        if (h.fn) h.fn(payload, size);
    }
}

IEventBus::HandlerId EventBus::subscribe(std::uint64_t type,
                                        std::function<void(const void* payload, std::size_t size)> fn) {
    std::lock_guard<std::mutex> lock(m_);
    const HandlerId id = next_++;
    byType_[type].push_back(Handler{ id, type, std::move(fn) });
    idToType_[id] = type;
    return id;
}

void EventBus::unsubscribe(HandlerId id) {
    std::lock_guard<std::mutex> lock(m_);
    auto itType = idToType_.find(id);
    if (itType == idToType_.end()) return;

    const std::uint64_t type = itType->second;
    idToType_.erase(itType);

    auto it = byType_.find(type);
    if (it == byType_.end()) return;

    auto& vec = it->second;
    vec.erase(std::remove_if(vec.begin(), vec.end(),
                             [&](const Handler& h){ return h.id == id; }),
              vec.end());
}

} // namespace engine
