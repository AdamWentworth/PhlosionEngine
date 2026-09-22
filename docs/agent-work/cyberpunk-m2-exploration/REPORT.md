# M2-F implementation and qualification report

Date: 2026-09-22. Baseline: `36b6986` on main. User authorized committing the
accepted pass and holding afterward.
Implementation used the explicitly authorized Astra fallback after Flash HTTP
402; this is not evidence of successful Flash inference. Root review and the
automated evidence are accepted; manual coverage is recorded separately below.

## Delivered behavior

The public exploration course runs through the same session and presentation in
the standalone native runtime and the real editor Game preview. It integrates
accepted named actions, SDL mapped-controller snapshots, kinematic characters,
discrete trigger samples and the exploration camera without changing those
algorithms. The course contains a floor, thin wall, ramp, steep slope, stairs,
ledge, sensor, rigid player, landmarks and a blocked/retrying NPC. Reset restores
safe runtime state while retaining authored camera settings; a bounded fall below
the finite floor also returns the player to safety.

The opt-in SDL callback extension preserves the main ABI 34 contract and
`InputEvent`. Main validation precedes optional validation. Absent extensions
retain legacy delivery; malformed present tables reject load. Frame callbacks
continue during pause/inactivity, update focus promptly, prevent duplicate legacy
delivery and publish queued actions once per actual simulation tick. Game input
requires focus, active/focused Game viewport, Playing, positive viewport extent,
and no text editing. Pointer look/wheel use the image rectangle. Exact integer
16:9/21:9 fitting and actual image hover are opt-in; default layout is retained.

The generic bounded replay owns only its virtual mapped controller and balanced
SDL subsystem reference. It queues real SDL key/mouse/window events and uses
virtual axis/button setters; the host owns the event pump. Hidden replay receives
`SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS=1` only through the isolated child's process
environment, as approved in PLAN. Neither the runner nor interactive policy is
mutated. Metadata distinguishes this synthetic focus/background qualification
from physical-controller behavior.

## Verification evidence

Evidence is retained under ignored `artifacts/m2-f/`. The final qualification
uses the frozen binaries from `build-full-03.log` (exit 0); all enabled Debug
targets built. Actual compiler command records and CMake cache are in `compiler/`.
All affected new/platform/editor targets record `/W4 /WX`. The established
aggregate `PhlosionEngineTests` remains `/W1 /WX-`. The arithmetic-heavy image
guard alone uses `/O2` in Debug with assertions/symbols and warning gates retained;
sampling and thresholds are unchanged.

Root recovered and reviewed the completed results after the worker's turn ended
on a usage-limit error. All final command exits are zero: `ctest-all-03` passed
20/20 in 349.04 seconds; `matrix-04` passed 96 captures; `supplemental-01` passed
12; `scene-regression/final-02` passed 36; and `negative-01` rejected all 27
mutations and passed pristine recovery. The required
matrix is `matrix-04/manifest.json`: eight cases, two hosts, three APIs, two exact
ratios, 96 native captures. Supplemental steep/ledge behavior is a separate
12-capture matrix. All captures freeze after actual tick 120. Screenshot frame
160 is independently requested and observed; editor input frame 159 and runtime
input frame 160 reflect different host warmup, not different simulation ticks.
Editor 16:9 content is 832x468 and 21:9 content is 840x360 on this machine; the
standalone host uses those same dimensions for direct viewport comparisons.

The guard independently checks analytic player distances/support, wall/steep
blocking, ledge falling/landing, trigger Enter/Stay/Exit, NPC blocked/retry,
camera obstruction/recovery, gate/rearm/reset and continuous tick traces. Semantic
course identities are compared across hosts rather than allocator-dependent
character/collider IDs. A ray-box oracle predicts authored geometry/material
content from validated camera/player state, excluding unstable edge pixels.
Each visible object's interior must meet its coverage threshold; the player,
floor and landmarks must be present. Cross-host/API comparisons retain MAE <=
0.01, RMSE <= 0.08, changed ratio <= 0.05 at channel tolerance 8. Binaries,
sources, scripts, traces, handshakes, process commands, logs, PNGs, environment,
editor metrics, frame/API records and coverage are checked and hashed.
The staged project descriptor and DLL actually used by the editor are also
hashed; the DLL must match the build output, the descriptor must route to that
plugin, and every editor process must name that exact project once. The source
catalog requires 33 relevant named files under the recorded source root.

Root's `root_evidence_review.py` verified 936 unique referenced files against
current bytes/hashes and confirmed the final result counts. Both exploration
matrices have maximum normalized MAE, RMSE and changed-pixel ratio of zero.
Root inspected all three contact sheets and a raw 21:9 D3D12 editor sensor
capture, confirming visible content and actual Game-view letterboxing.

## Retained failures and corrections

- Initial builds exposed routine missing-include, warning-as-error, MSVC lambda
  and SDL entry-point linkage errors. Raw `build-01` through `build-12` attempts
  remain; later successful builds supersede them.
- The first hidden native replay selected no virtual controller under SDL's
  default background policy. The isolated synthetic replay override above fixed
  that qualification constraint. It does not claim physical/background behavior.
- `matrix-01` captured all 96 children, but the independent content guard rejected
  D3D12's empty floor. The fixture sky quad used near depth zero, masking geometry
  on D3D12. Moving that sample quad to far clip depth one fixed the native output;
  no renderer implementation changed. `probe-03` verified the correction before
  `matrix-02` passed all 96 cases. These are development evidence, not final hashes.
- An early CPU evidence self-test rejected the excessively close obstruction
  view because no useful landmark was visible at its smaller test resolution.
  Moving that case's authored spawn back one unit retains measured camera
  obstruction and meaningful back-wall/player coverage. The contract stayed
  strict; the next five-test exploration run passed.
- `ctest-all-01` passed 19/20 but the existing scene harness correctly rejected
  the editor binary hash after a concurrent rebuild. The failure was a run-order
  error. The full suite is rerun with frozen binaries; the failed raw log remains.
- Root's code review found unsigned JSON narrowing before replay range checks
  and incomplete provenance for the staged editor project. The bounded correction
  checks unsigned values before conversion, tests UINT64_MAX / INT64_MAX+1 and
  valid signed endpoints, binds the descriptor/staged DLL and exact project
  argument, and requires the named source catalog. Four additional native
  negatives exercise those provenance requirements. The preceding `matrix-03`
  96-case, `scene-regression/final-01` 36-case and `ctest-all-02` 20-test runs all
  passed but are retained as pre-correction evidence; final runs are repeated.

## Human check and remaining scope

The user exercised the visible D3D12 runtime and editor with a device exposed by
SDL as `Xbox 360 Controller`. Runtime telemetry corroborates movement, running,
orbit/zoom, focus loss, pause and three resets to authored distance 7. Editor
telemetry corroborates movement, orbit/zoom and focus loss with authored distance
9. Both traces are nonsynthetic and use no background override. The user reported
runtime unplug/reconnect success, but no SDL inventory removal/addition appears
in the trace, so discrete physical detach is not independently corroborated.

Manual editor Pause/Stop/Scene-switch/reset checks remain pending by the user's
explicit choice after root clarified that those transitions were absent from
the editor trace. Automated gate and reset evidence is separate. Raw traces,
launch hashes, observations and summaries are under
`artifacts/m2-f/manual-20260922-155942/`; both windows are closed.

Normal visible launch commands, controls, authored-camera options and opt-in
JSONL telemetry are documented in [the fixture guide](../../EXPLORATION_FIXTURE.md).
For the human check use no replay, automation flag or SDL background override.
The trace records gameplay action/state, selected controller/connectivity, gate
changes, player/NPC positions, trigger counters, camera and reset. It does not log
raw editor text or gated keystrokes. Normal controls/status omit test details.

This qualifies a rigid public sample, not final animation, Outskirt Stand fitting,
asset-specific collision/camera/routes or a production game scene. Those remain
behind the asset handoff and subsequent scene qualification. No dependency,
provider, accepted algorithm, main plugin ABI, or standing image threshold change
was required. Root completed actual code/evidence review; see [acceptance](ACCEPTANCE.md).
