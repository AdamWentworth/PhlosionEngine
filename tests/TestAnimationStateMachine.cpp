#include "engine/render/AnimationStateMachine.h"

#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace {
using namespace engine::render::animation;
using engine::render::model_types::AnimationClip;
using engine::render::model_types::NodeTRS;
using engine::render::model_types::SkinData;

bool near(float a, float b) { return std::abs(a - b) < 1.0e-5f; }

AnimationStateGraph makeGraph() {
    AnimationStateGraph graph;
    graph.initialState = "idle";
    graph.parameters = {
        {"speed", AnimationParameterType::Number, false, 0.0f},
        {"attack", AnimationParameterType::Boolean, false, 0.0f}};
    graph.states = {
        {"idle", 0, true, {{"idle_start", 0.0f}}},
        {"run", 1, true, {{"run_start", 0.0f}, {"footstep", 0.2f},
                          {"footstep", 1.2f}}},
        {"attack", 2, false, {{"windup", 0.0f}, {"impact", 0.5f}}}};
    graph.transitions = {
        {"idle", "run", "speed", AnimationConditionKind::AtLeast, 0.5f, 1.0f},
        {"idle", "attack", "speed", AnimationConditionKind::AtLeast, 0.5f, 0.0f},
        {"run", "attack", "attack", AnimationConditionKind::True, 0.0f, 0.25f},
        {"run", "idle", "speed", AnimationConditionKind::Below, 0.5f, 0.5f},
        {"attack", "idle", "attack", AnimationConditionKind::False, 0.0f, 0.1f}};
    return graph;
}

} // namespace

bool test_animation_state_machine_contract(std::string& failure) {
    std::vector<NodeTRS> nodes(1);
    std::vector<std::vector<int>> children(1);
    std::vector<int> roots{0};
    std::vector<SkinData> skins;
    std::vector<AnimationClip> clips(3);
    for (auto& clip : clips) clip.durationSec = 2.0f;
    std::vector<std::string> names{"body"};
    const PoseSource source{nodes, children, roots, skins, clips, names};
    AnimationStateMachine machine;
    AnimationStateFrame frame;
    std::string error;
    if (!machine.configure(source, makeGraph(), &error) ||
        !machine.advance(0.1f, frame, &error) || frame.state != "idle" ||
        frame.transitioning || frame.pose.baseClipIndex != 0 ||
        frame.events.size() != 1 || frame.events[0].name != "idle_start") {
        failure = "initial state or start marker was wrong: " + error;
        return false;
    }
    if (!machine.setNumber("speed", 1.0f, &error) ||
        !machine.advance(0.25f, frame, &error) || frame.state != "run" ||
        !frame.transitioning || frame.pose.baseClipIndex != 0 ||
        frame.pose.targetClipIndex != 1 ||
        !near(frame.pose.crossfadeWeight, 0.25f) ||
        frame.events.size() != 2 || frame.events[0].name != "run_start" ||
        frame.events[1].name != "footstep") {
        failure = "numeric parameter did not start the expected crossfade/events: " + error;
        return false;
    }
    if (!machine.advance(0.75f, frame, &error) || frame.transitioning ||
        frame.pose.baseClipIndex != 1 || !near(frame.pose.baseTimeSec, 1.0f)) {
        failure = "crossfade completion did not make run the active clip";
        return false;
    }
    if (!machine.setBool("attack", true, &error) ||
        !machine.advance(0.125f, frame, &error) || frame.state != "attack" ||
        !frame.transitioning || frame.pose.baseClipIndex != 1 ||
        frame.pose.targetClipIndex != 2 ||
        !near(frame.pose.crossfadeWeight, 0.5f) ||
        frame.events.size() != 1 || frame.events[0].name != "windup") {
        failure = "boolean parameter did not enter the attack state";
        return false;
    }
    if (!machine.advance(0.5f, frame, &error) || frame.transitioning ||
        frame.events.size() != 1 || frame.events[0].name != "impact" ||
        frame.events[0].absolutePlaybackTimeSec != 0.5) {
        failure = "attack impact did not fire at its authored clip time";
        return false;
    }
    if (!machine.advance(3.0f, frame, &error) || !frame.events.empty() ||
        !near(frame.pose.baseTimeSec, 2.0f)) {
        failure = "one-shot attack replayed an event or wrapped its pose";
        return false;
    }
    const AnimationStateFrame preserved = frame;
    if (machine.advance(-1.0f, frame, &error) || error.empty() ||
        frame.state != preserved.state ||
        !near(frame.pose.baseTimeSec, preserved.pose.baseTimeSec)) {
        failure = "invalid delta did not preserve the previous frame";
        return false;
    }
    if (machine.setNumber("attack", 1.0f, &error) ||
        machine.setBool("speed", true, &error) ||
        machine.setNumber("speed", (std::numeric_limits<float>::quiet_NaN)(),
                          &error)) {
        failure = "typed parameter validation accepted invalid input";
        return false;
    }
    if (!machine.forceState("idle", &error) ||
        !machine.advance(0.0f, frame, &error) || frame.state != "idle" ||
        frame.transitioning || frame.events.size() != 1 ||
        frame.events[0].name != "idle_start") {
        failure = "forced state did not cancel the attack and fire its start marker";
        return false;
    }
    machine.reset();
    if (!machine.advance(0.0f, frame, &error) || frame.state != "idle" ||
        frame.transitioning || frame.pose.baseClipIndex != 0) {
        failure = "reset did not restore the initial state and parameter defaults";
        return false;
    }
    if (!machine.setNumber("speed", 1.0f, &error) ||
        !machine.advance(1.0f, frame, &error) || frame.state != "run") {
        failure = "priority transition did not select the first matching state";
        return false;
    }
    const AnimationStateFrame beforeCatchup = frame;
    if (machine.advance(100.0f, frame, &error, 2u) || error.empty() ||
        frame.state != beforeCatchup.state ||
        !near(frame.pose.baseTimeSec, beforeCatchup.pose.baseTimeSec) ||
        !machine.advance(0.0f, frame, &error) ||
        !near(frame.pose.baseTimeSec, beforeCatchup.pose.baseTimeSec)) {
        failure = "event catch-up failure changed the live state or output";
        return false;
    }
    AnimationStateGraph invalid = makeGraph();
    invalid.states[1].name = "idle";
    if (machine.configure(source, invalid, &error) || error.empty() ||
        !machine.advance(0.0f, frame, &error) || frame.state != "run" ||
        !near(frame.pose.baseTimeSec, beforeCatchup.pose.baseTimeSec)) {
        failure = "failed graph reconfiguration did not preserve the live machine";
        return false;
    }
    invalid = makeGraph();
    invalid.transitions[0].parameter = "attack";
    if (machine.configure(source, invalid, &error) || error.empty()) {
        failure = "mismatched condition and parameter type was accepted";
        return false;
    }
    failure.clear();
    return true;
}
