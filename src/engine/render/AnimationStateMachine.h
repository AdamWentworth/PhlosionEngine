#pragma once

#include "AnimationBlend.h"

#include <cstddef>
#include <string>
#include <vector>

namespace engine::render::animation {

enum class AnimationParameterType { Boolean, Number };

struct AnimationParameterDefinition {
    std::string name;
    AnimationParameterType type = AnimationParameterType::Boolean;
    bool initialBool = false;
    float initialNumber = 0.0f;
};

struct AnimationStateDefinition {
    std::string name;
    int clipIndex = -1;
    bool looping = true;
    std::vector<TimedAnimationEvent> events;
};

enum class AnimationConditionKind {
    True,
    False,
    AtLeast,
    Below,
};

struct AnimationTransitionDefinition {
    std::string from;
    std::string to;
    std::string parameter;
    AnimationConditionKind condition = AnimationConditionKind::True;
    float threshold = 0.0f;
    float fadeSeconds = 0.0f;
};

struct AnimationStateGraph {
    std::string initialState;
    std::vector<AnimationParameterDefinition> parameters;
    std::vector<AnimationStateDefinition> states;
    // First matching transition from the active state wins. Transitions are
    // examined only when a previous crossfade has completed.
    std::vector<AnimationTransitionDefinition> transitions;
};

struct AnimationStateEvent {
    std::string state;
    std::string name;
    double absolutePlaybackTimeSec = 0.0;
};

struct AnimationStateFrame {
    PoseBlendRequest pose;
    std::string state;
    bool transitioning = false;
    std::vector<AnimationStateEvent> events;
};

// CPU-only, per-instance runtime. The graph is copied at configure time; it
// borrows no asset arrays thereafter. The caller samples `frame.pose` with
// blendPose or sampleGltfSceneBlendedPose, then dispatches named events.
class AnimationStateMachine {
public:
    bool configure(const PoseSource& source,
                   const AnimationStateGraph& graph,
                   std::string* error = nullptr);
    bool setBool(const std::string& name, bool value,
                 std::string* error = nullptr);
    bool setNumber(const std::string& name, float value,
                   std::string* error = nullptr);
    // Force cancels any blend and pending source events. The forced state's
    // start markers fire on the next advance; transitions wait one update.
    bool forceState(const std::string& name, std::string* error = nullptr);
    // Reset also restores authored parameter defaults.
    void reset();
    // Invalid deltas or event catch-up leave both machine and output unchanged.
    bool advance(float deltaSec, AnimationStateFrame& out,
                 std::string* error = nullptr,
                 std::size_t maxEvents = 256u);

private:
    struct State {
        AnimationStateDefinition definition;
        float durationSec = 0.0f;
    };
    struct Parameter {
        AnimationParameterDefinition definition;
        bool boolean = false;
        float number = 0.0f;
    };
    struct Transition {
        std::size_t from = 0u;
        std::size_t to = 0u;
        std::size_t parameter = 0u;
        AnimationConditionKind condition = AnimationConditionKind::True;
        float threshold = 0.0f;
        float fadeSeconds = 0.0f;
    };

    bool conditionMet(const Transition& transition) const;
    std::vector<State> states_;
    std::vector<Parameter> parameters_;
    std::vector<Transition> transitions_;
    std::size_t initialState_ = 0u;
    std::size_t activeState_ = 0u;
    int previousState_ = -1;
    double activeTimeSec_ = 0.0;
    double previousTimeSec_ = 0.0;
    float fadeElapsedSec_ = 0.0f;
    float fadeDurationSec_ = 0.0f;
    bool startPending_ = false;
    bool suppressTransitionOnce_ = false;
};

} // namespace engine::render::animation
