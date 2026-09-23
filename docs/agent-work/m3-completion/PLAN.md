# M3 completion plan

Baseline: PhlosionEngine `ba77e68` on `main`, clean working tree before these
planning documents. M3-E's 23-entry Debug CTest suite passed. Asset names
required by the M3 gate were not found in the game checkout. The pinned Flash
worker route returned HTTP 402, so root implemented the approved bundles.

## Phase F: CPU audio mixer

Completed in the engine working tree. `Phlosion::Audio` is an opt-in static
target without SDL or renderer links. Contract: 48 kHz
interleaved float stereo output, validated mono/stereo decoded PCM clips,
Music/Sfx/Ambient/Voice buses, master/bus/voice gain, mute, deterministic
sample-time fades, looping, spatial mono panning/attenuation, stable voice IDs,
stop/stop-all and owner-thread lifetime. Reject invalid clips/parameters/frames
without mutating mixer or caller output. Keep decoded clip ownership safe across
playback. Prove the numeric mix, clip end/loop, fades, spatial edge cases,
cleanup, malformed inputs and thread ownership in a standalone CPU CTest.
The full Debug build and all 24 CTest entries passed after this phase. No
device or music codec claim is made by the CPU target.

## Phase G: SDL output and failure path

Completed in the engine working tree. The opt-in SDL queued-device adapter's
dummy-driver contract covers bounded queueing, null sink, startup/retry/shutdown,
simulated device failure and main-thread ownership. The full
Debug build and all 25 CTest entries passed. Physical-device coverage remains
open.

## Phase H: VFX and battle lifecycle

The generic effect spawn/attach/stop lifecycle is implemented in the
PhlosionVFX working tree. Its full Debug build and both standalone contracts
pass. Game-owned battle event bindings and synthetic cancel/skip/exit tests
remain. Do not move Pokemon rules into the engine or VFX library.

## Phase I: native presentation and asset gate

Build a synthetic double-battle fixture and qualify all affected runtime/editor
images on OpenGL, Vulkan and Direct3D 12 with expected-content checks. Complete
native text/HUD, camera, animation, audio and VFX integration. Use the actual
Espeon/Umbreon and environment assets when the handoff is present; until then
the final M3 gate remains open and must be reported as such.
