# M3 parameter-driven animation state

The asset-independent M3-B pass adds `AnimationStateMachine`, a per-instance
CPU controller above the [blend foundation](ANIMATION_BLEND.md). A graph names
clips, typed boolean and numeric parameters, state events, and transitions.
Numeric conditions support at-least and below thresholds. The first matching
transition in authoring order wins; another transition is considered only
after the active crossfade finishes. This order makes transition priority
explicit without game-specific battle rules in the engine.

`configure` checks graph bounds, unique state/parameter names, clip durations,
marker times, transition references, parameter types and finite fade values.
It copies the graph and leaves the live machine intact on failure. Each
`advance` returns a `PoseBlendRequest` suitable for `blendPose` or
`sampleGltfSceneBlendedPose`, plus events from the newly active state. The
caller reconfigures the machine when its source asset generation changes,
because the copied clip indices and durations belong to that generation.
Source events stop at the transition boundary; target start markers fire once when
that state activates. Looping states can emit events over multiple cycles;
one-shot states hold at their clip endpoint and never replay markers. Event
catch-up has a caller-set bound, and a failed update preserves both controller
time and the previous output frame.

`forceState` cancels the current blend and pending source events, restarts the
requested state at time zero, and defers parameter transitions for one update
so its activation markers can fire. `reset` also restores authored parameter
defaults. The caller owns semantic actions such as spawning effects, playing
audio, or returning control to the overworld, and clears corresponding
parameters or resets the machine when cancelling battle actions.

## Verification

The CPU contract exercises numeric and boolean transitions, intermediate and
completed crossfades, start/impact timing, one-shot hold, forced cancellation,
reset, type errors, invalid updates and transactional graph reconfiguration.
The public animation fixture adds `state-crossfade`: a boolean parameter
selects the cubic clip and yields an analytic -56.25 degree pose during a
four-second transition. Runtime and real-editor captures on OpenGL, Vulkan
and Direct3D 12 compare that pose with expected geometry and pixel content.
Local matrix evidence is under `artifacts/m3-b/`.

This controller does not yet author graphs or events in a versioned scene
format, define additive layers, resolve incompatible matrix-authored node
blends, or integrate production character actions. Those remain content and
game-facing M3 work alongside the camera, UI, audio and VFX systems.
