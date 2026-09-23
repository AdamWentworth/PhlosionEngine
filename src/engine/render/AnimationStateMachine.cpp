#include "AnimationStateMachine.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <utility>

namespace engine::render::animation {
namespace {

bool fail(std::string* error, const std::string& message) {
    if (error) *error = message;
    return false;
}

} // namespace

bool AnimationStateMachine::configure(const PoseSource& source,
                                      const AnimationStateGraph& graph,
                                      std::string* error) {
    if (graph.states.empty() || graph.states.size() > 128u ||
        graph.parameters.size() > 64u || graph.transitions.size() > 512u)
        return fail(error, "animation graph size is outside the supported bounds");
    AnimationStateMachine candidate;
    std::unordered_map<std::string, std::size_t> stateByName;
    std::unordered_map<std::string, std::size_t> parameterByName;
    for (const auto& definition : graph.parameters) {
        if ((definition.type != AnimationParameterType::Boolean &&
             definition.type != AnimationParameterType::Number) ||
            definition.name.empty() ||
            !std::isfinite(definition.initialNumber) ||
            !parameterByName.emplace(definition.name, candidate.parameters_.size()).second)
            return fail(error, "animation graph has an invalid or duplicate parameter");
        candidate.parameters_.push_back(
            {definition, definition.initialBool, definition.initialNumber});
    }
    for (const auto& definition : graph.states) {
        if (definition.name.empty() ||
            !stateByName.emplace(definition.name, candidate.states_.size()).second ||
            definition.clipIndex < 0 ||
            static_cast<std::size_t>(definition.clipIndex) >= source.animations.size())
            return fail(error, "animation graph has an invalid or duplicate state");
        const float duration =
            source.animations[static_cast<std::size_t>(definition.clipIndex)].durationSec;
        if (!std::isfinite(duration) || duration <= 0.0f ||
            definition.events.size() > 256u)
            return fail(error, "animation state has an invalid clip duration or event count");
        for (const auto& event : definition.events)
            if (event.name.empty() || !std::isfinite(event.timeSec) ||
                event.timeSec < 0.0f || event.timeSec >= duration)
                return fail(error, "animation state has an invalid event marker");
        candidate.states_.push_back({definition, duration});
    }
    const auto initial = stateByName.find(graph.initialState);
    if (initial == stateByName.end())
        return fail(error, "animation graph initial state is unknown");
    candidate.initialState_ = initial->second;
    for (const auto& definition : graph.transitions) {
        if (definition.condition != AnimationConditionKind::True &&
            definition.condition != AnimationConditionKind::False &&
            definition.condition != AnimationConditionKind::AtLeast &&
            definition.condition != AnimationConditionKind::Below)
            return fail(error, "animation transition condition is unknown");
        const auto from = stateByName.find(definition.from);
        const auto to = stateByName.find(definition.to);
        const auto parameter = parameterByName.find(definition.parameter);
        if (from == stateByName.end() || to == stateByName.end() ||
            parameter == parameterByName.end() || from->second == to->second)
            return fail(error, "animation transition references an unknown or identical state or parameter");
        if (!std::isfinite(definition.fadeSeconds) ||
            definition.fadeSeconds < 0.0f || !std::isfinite(definition.threshold))
            return fail(error, "animation transition fade or threshold is invalid");
        const bool booleanCondition =
            definition.condition == AnimationConditionKind::True ||
            definition.condition == AnimationConditionKind::False;
        const bool booleanParameter = candidate.parameters_[parameter->second]
                                          .definition.type == AnimationParameterType::Boolean;
        if (booleanCondition != booleanParameter)
            return fail(error, "animation transition condition has the wrong parameter type");
        candidate.transitions_.push_back({from->second, to->second,
                                          parameter->second, definition.condition,
                                          definition.threshold, definition.fadeSeconds});
    }
    candidate.reset();
    *this = std::move(candidate);
    if (error) error->clear();
    return true;
}

bool AnimationStateMachine::setBool(const std::string& name, bool value,
                                    std::string* error) {
    for (auto& parameter : parameters_)
        if (parameter.definition.name == name) {
            if (parameter.definition.type != AnimationParameterType::Boolean)
                return fail(error, "animation parameter is not boolean");
            parameter.boolean = value;
            if (error) error->clear();
            return true;
        }
    return fail(error, "unknown animation parameter: " + name);
}

bool AnimationStateMachine::setNumber(const std::string& name, float value,
                                      std::string* error) {
    if (!std::isfinite(value))
        return fail(error, "animation parameter value must be finite");
    for (auto& parameter : parameters_)
        if (parameter.definition.name == name) {
            if (parameter.definition.type != AnimationParameterType::Number)
                return fail(error, "animation parameter is not numeric");
            parameter.number = value;
            if (error) error->clear();
            return true;
        }
    return fail(error, "unknown animation parameter: " + name);
}

bool AnimationStateMachine::forceState(const std::string& name,
                                       std::string* error) {
    for (std::size_t index = 0u; index < states_.size(); ++index)
        if (states_[index].definition.name == name) {
            activeState_ = index;
            activeTimeSec_ = 0.0;
            previousState_ = -1;
            previousTimeSec_ = 0.0;
            fadeElapsedSec_ = 0.0f;
            fadeDurationSec_ = 0.0f;
            startPending_ = true;
            suppressTransitionOnce_ = true;
            if (error) error->clear();
            return true;
        }
    return fail(error, "unknown animation state: " + name);
}

void AnimationStateMachine::reset() {
    if (states_.empty()) return;
    for (auto& parameter : parameters_) {
        parameter.boolean = parameter.definition.initialBool;
        parameter.number = parameter.definition.initialNumber;
    }
    activeState_ = initialState_;
    activeTimeSec_ = 0.0;
    previousState_ = -1;
    previousTimeSec_ = 0.0;
    fadeElapsedSec_ = 0.0f;
    fadeDurationSec_ = 0.0f;
    startPending_ = true;
    suppressTransitionOnce_ = false;
}

bool AnimationStateMachine::conditionMet(const Transition& transition) const {
    const Parameter& parameter = parameters_[transition.parameter];
    switch (transition.condition) {
        case AnimationConditionKind::True: return parameter.boolean;
        case AnimationConditionKind::False: return !parameter.boolean;
        case AnimationConditionKind::AtLeast:
            return parameter.number >= transition.threshold;
        case AnimationConditionKind::Below:
            return parameter.number < transition.threshold;
    }
    return false;
}

bool AnimationStateMachine::advance(float deltaSec, AnimationStateFrame& out,
                                    std::string* error,
                                    std::size_t maxEvents) {
    if (states_.empty()) return fail(error, "animation state machine is not configured");
    if (!std::isfinite(deltaSec) || deltaSec < 0.0f)
        return fail(error, "animation update delta must be finite and nonnegative");

    std::size_t active = activeState_;
    int previous = previousState_;
    double activeTime = activeTimeSec_;
    double previousTime = previousTimeSec_;
    float fadeElapsed = fadeElapsedSec_;
    float fadeDuration = fadeDurationSec_;
    bool startPending = startPending_;
    if (previous < 0 && !suppressTransitionOnce_) {
        for (const Transition& transition : transitions_) {
            if (transition.from != active || !conditionMet(transition)) continue;
            previous = transition.fadeSeconds > 0.0f
                           ? static_cast<int>(active) : -1;
            previousTime = activeTime;
            active = transition.to;
            activeTime = 0.0;
            fadeElapsed = 0.0f;
            fadeDuration = transition.fadeSeconds;
            startPending = true;
            break;
        }
    }

    const auto advanceClock = [&](double time, const State& state) {
        const double advanced = time + static_cast<double>(deltaSec);
        return state.definition.looping
                   ? advanced
                   : (std::min)(advanced, static_cast<double>(state.durationSec));
    };
    const double nextActiveTime = advanceClock(activeTime, states_[active]);
    const double nextPreviousTime = previous >= 0
        ? advanceClock(previousTime, states_[static_cast<std::size_t>(previous)])
        : 0.0;
    if (!std::isfinite(nextActiveTime) ||
        nextActiveTime > (std::numeric_limits<float>::max)() ||
        (previous >= 0 && (!std::isfinite(nextPreviousTime) ||
                           nextPreviousTime > (std::numeric_limits<float>::max)())))
        return fail(error, "animation playback time exceeds the supported range");

    std::vector<FiredAnimationEvent> fired;
    const State& state = states_[active];
    if (!collectAnimationEvents(state.definition.events, state.durationSec,
                                activeTime, nextActiveTime, fired, error,
                                maxEvents, startPending,
                                state.definition.looping)) return false;
    AnimationStateFrame frame;
    frame.state = state.definition.name;
    for (const auto& event : fired)
        frame.events.push_back({frame.state, event.name, event.absoluteTimeSec});

    fadeElapsed = (std::min)(fadeDuration, fadeElapsed + deltaSec);
    if (previous >= 0 && fadeElapsed < fadeDuration) {
        frame.transitioning = true;
        frame.pose.baseClipIndex =
            states_[static_cast<std::size_t>(previous)].definition.clipIndex;
        frame.pose.baseTimeSec = static_cast<float>(nextPreviousTime);
        frame.pose.targetClipIndex = state.definition.clipIndex;
        frame.pose.targetTimeSec = static_cast<float>(nextActiveTime);
        frame.pose.crossfadeWeight = fadeElapsed / fadeDuration;
    } else {
        previous = -1;
        frame.pose.baseClipIndex = state.definition.clipIndex;
        frame.pose.baseTimeSec = static_cast<float>(nextActiveTime);
    }

    activeState_ = active;
    previousState_ = previous;
    activeTimeSec_ = nextActiveTime;
    previousTimeSec_ = nextPreviousTime;
    fadeElapsedSec_ = fadeElapsed;
    fadeDurationSec_ = fadeDuration;
    startPending_ = false;
    suppressTransitionOnce_ = false;
    out = std::move(frame);
    if (error) error->clear();
    return true;
}

} // namespace engine::render::animation
