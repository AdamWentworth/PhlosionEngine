// RoundEvents.h

#pragma once
#include "Event.h"
#include <string>

struct RoundPhaseChangedEvent : public Event {
    std::string previousPhase;
    std::string nextPhase;

    RoundPhaseChangedEvent(const std::string& prev, const std::string& next)
        : Event(EventType::RoundPhaseChanged),
          previousPhase(prev), nextPhase(next) {}

    std::string toString() const override {
        return "RoundPhaseChanged: " + previousPhase + " -> " + nextPhase;
    }
};
