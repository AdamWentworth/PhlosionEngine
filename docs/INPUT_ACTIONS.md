# Named actions and SDL controller input

`Phlosion::Input` (`engine_input`) provides SDL-free, owner-thread named actions
in `engine::input`. Its public header is `engine/input/InputActions.h`; the static
target links only GLM and the standard runtime. `engine_platform` supplies the
opt-in SDL adapter in `engine/input/SdlActionInput.h`. Existing `InputEvent`,
application/editor event pumps and camera behavior are unchanged.

## Action definitions and frames

An `InputActions` instance owns names, bindings, physical state and published
action states. Names contain 1–64 ASCII letters, digits, underscores, dots or
hyphens and are case-sensitive. `define`, `rebind`, `remove`, `state`, controller
registration/selection and ingestion return `bool` with an optional diagnostic.
Failures preserve configuration, physical/action state and caller outputs;
the diagnostic is the exception and is written on rejection. A successful call
does not clear a previous diagnostic. Empty binding lists are valid.

`InputOptions` bounds actions (default 128, range 1–1024), controllers (8, 1–32),
and bindings per action (32, 1–64). Invalid constructor options throw
`std::invalid_argument`. These are per-instance limits, not process memory
budgets. Allocation failures retain normal C++ limitations.

`Button` actions accept only digital bindings with scale `(1,0)` and combine via
OR. `Axis1D` accepts digital/single-axis contributions with scale Y zero and
clamps their sum to `[-1,1]`. `Axis2D` sums directional digital/axis and stick
contributions, then clamps vector length to one. Opposite directions cancel;
diagonals have unit length. Every scale component must be finite in `[-1,1]`.
Unknown identifiers and incompatible bindings reject the complete operation.

Signed axes range from -1 to 1; triggers from 0 to 1. Y follows SDL, positive
down. Use binding scales to invert it. `AxisBinding` defaults to a .15 signed-axis
or .05 trigger deadzone. `StickBinding` defaults to .15 and uses a radial
deadzone; single axes use an axial deadzone. All rescale the remaining range,
with deadzones finite in `[0,.95]`. Stick direction is preserved and its raw
magnitude capped at one before rescaling. A submitted controller snapshot must
contain valid values for every axis; malformed snapshots are wholly rejected.

Ingestion updates current values and latches transitions. `advanceFrame()`
publishes values, `down` (magnitude greater than `1e-6`), `pressed`, and `released`,
then clears the pending edge latches. Queries do not consume edges. A short tap
between advances reports both edges with final `down=false`. Held values persist;
advancing without events clears only the edges. Keyboard repeat-down events are
ignored even if no key is held. Unrelated `InputEvent` types are ignored.

`define` and successful `rebind` are explicit exceptions to frame publication:
they immediately expose that action's value from current allowed physical input,
without synthetic edges, and clear its pending edges. Other actions and their
pending edges are untouched. A failed rebind changes nothing. Removal has no
callback. Hosts choose the action cadence, normally once per simulation tick.

## Controller selection and focus

`addController` returns an opaque, nonzero `ControllerId`. IDs are never reused
across action instances within the linked module. `controllers()` returns copied,
sorted IDs. Zero, foreign and removed IDs reject safely. Select explicitly with
`selectController(id)` or deselect with `selectController(std::nullopt)`;
unselected devices cannot drive actions. Disconnecting the selected controller
clears selection and its contributions, latching releases where appropriate.
It never switches automatically to another device. Keyboard/mouse remains usable
without a selected controller.

`setFocused` models window focus; `setEnabled` independently models gameplay or
viewport capture. Closing either gate clears keyboard/mouse state, neutralizes
actions, discards pending presses and latches releases. Events while gated cannot
activate actions. Reopening does not restore held keyboard/mouse state: fresh
presses work immediately; auto-repeat does not reactivate a key.

Selection changes and gate transitions suppress controller controls individually.
A button rearms when observed released; each stick component rearms at absolute
value <= .05; each trigger at <= .05. Observations while either gate is closed
do not rearm. The axis observation that rearms is treated as zero even if the
binding deadzone is zero; later snapshots use the ordinary binding deadzone.
One neutral component does not rearm another still-held component or button.
Switching away and back requires fresh neutral observations. Selecting the same
ID or setting a gate to its existing value is a no-op. There is no hardware
reset or required reconnection.

## SDL host integration

`SdlActionInput` references an `InputActions` that must outlive it. Both are
noncopyable/nonmovable and every public operation belongs to the constructing
thread. Wrong-thread calls throw `std::logic_error` before mutation, including
queries and adapter construction against another thread's action owner.
Destruction must also occur on that thread; violating this lifetime precondition
terminates. Separate pure action instances can operate on independent threads.

The adapter owns one balanced `SDL_INIT_GAMECONTROLLER` reference and its own
opened handles/registered IDs. Initialization failure throws `std::runtime_error`.
Destroy it before external `SDL_Quit`, including the existing `Window` shutdown,
and before its action owner. It never calls `SDL_Quit`, creates a window/context,
changes hints/mappings/event state, or owns the event queue. SDL's shared subsystem
and controller APIs still obey SDL's host-thread requirements.

Call `refresh()` for startup discovery, then forward SDL controller add/remove/
remapping events from the host. Added events carry a device index; removed events
carry an instance ID. Only mapped SDL GameControllers are registered. Duplicate
discovery is harmless. `devices()` copies names, engine IDs and SDL instance IDs;
`controllerForInstance()` looks up an ID without exposing handles. Selection is
always the caller's decision. `poll()` detects missed disconnections and submits
complete snapshots; it does not discover new devices, which need an add event or
refresh. Remapping events reconcile current values.

Refresh makes per-device progress: an open/capacity failure reports a diagnostic,
closes the failed candidate handle and keeps already usable devices. Multiple
errors are collected; do not ignore a false return. Do not remove adapter-owned
IDs manually from `InputActions`; let the adapter manage their registration.

Pass the host window ID to accept its keyboard, supported mouse-button and focus
events. Other windows are filtered out. ID zero means controller-only. Unmapped
SDL keys and unsupported mouse buttons are ignored. Motion, scroll and text
are not action sources in this API.

The supported host cadence is **drain and forward events, poll, then advance**.
The adapter consumes controller event values into a cached complete snapshot so
queued down/up taps survive even when polling observes the final released state.
Polling uses SDL's current mapped values, normalizing signed endpoints separately
(-32768/32768 and +32767/32767); triggers use controller range 0–32767. The adapter
does not call `SDL_PollEvent` or `SDL_WaitEvent`. If polling generates a duplicate
held/up observation in the queue, forwarding it next frame does not duplicate the
edge. Do not defer a history of intervening transitions across multiple published
polling frames: forwarding that history later correctly latches those transitions.

```cpp
#include "engine/input/SdlActionInput.h"
#include <SDL.h>
#include <stdexcept>

using namespace engine::input;

void runInputExample(SDL_Window* window) {
    // This scope ends before the host destroys its Window or calls SDL_Quit.
    InputActions actions;
    SdlActionInput input(actions, SDL_GetWindowID(window));
    std::string error;
    auto require = [&](bool ok) { if (!ok) throw std::runtime_error(error); };
    require(actions.define({"move", ActionKind::Axis2D, {
        KeyBinding{InputEvent::Key::W, {0, -1}},
        KeyBinding{InputEvent::Key::S, {0, 1}},
        KeyBinding{InputEvent::Key::A, {-1, 0}},
        KeyBinding{InputEvent::Key::D, {1, 0}},
        StickBinding{GamepadStick::Left}}}, &error));
    require(actions.define({"run", ActionKind::Button, {
        KeyBinding{InputEvent::Key::LShift},
        ButtonBinding{GamepadButton::LeftStick}}}, &error));
    require(actions.define({"interact", ActionKind::Button, {
        KeyBinding{InputEvent::Key::E},
        ButtonBinding{GamepadButton::South}}}, &error));
    require(input.refresh(&error));

    // Example caller policy. A player-selection UI could choose a different ID.
    const auto available = input.devices();
    if (!available.empty()) require(actions.selectController(available.front().id, &error));

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) { // Host owns this pump.
            if (event.type == SDL_QUIT) running = false;
            require(input.ingest(event, &error));
        }
        require(input.poll(&error));
        actions.advanceFrame(); // Choose once per simulation tick in the real host.
        ActionState move, run, interact;
        require(actions.state("move", move, &error));
        require(actions.state("run", run, &error));
        require(actions.state("interact", interact, &error));
        // Feed move.value, run.down and interact.pressed into caller gameplay.
    }

    actions.setEnabled(false); // For example, viewport capture is now released.
    require(actions.rebind("interact", {KeyBinding{InputEvent::Key::F}}, &error));
    actions.setEnabled(true); // Fresh keys and neutral controller observations required.
}
```

The guide names are examples; the engine hardcodes no game action names.
Bindings can be composed/replaced in memory; context priority/consumption,
capture UI, persistence, haptics and camera/character wiring are outside this pass.

## Verification boundary

`PhlosionInputContracts` tests the pure target without SDL or graphics imports.
`PhlosionSdlInputContracts` uses actual SDL virtual mapped devices without a
window/GPU, including all standard controls, queued short taps, selection,
hotplug, missed removal, focus/window filtering and independent subsystem
reference survival. Virtual joystick trigger fixtures use raw -32768 at rest;
the adapter reads SDL's converted controller range instead. Test fixtures detach
only their own virtual devices and do not change user hints or mappings.

This establishes the reusable dependency, not the complete M2 exploration exit
gate. Physical controller drivers, native runtime/editor routing, camera motion
and in-scene exploration remain unverified and require later host integration.
