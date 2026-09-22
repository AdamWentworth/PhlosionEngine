# M2-E checkpoint

Status: accepted by Astra on 2026-09-22; committed with this checkpoint as
`Add exploration camera rig and collision-aware framing`.

- User requested committing accepted M2-D and continuing. M2-D committed as
  `bd763913ab35d770fec7716ad1c70ccdc1b5baa0`; worktree was clean afterwards.
- PLAN defines an opt-in CPU exploration camera rig, separate authored settings,
  collision-aware boom, explicit rates/deltas/reset, relative movement helper
  and actual input/character/camera composition tests. Existing hosts/rendering
  and plugin ABI remain unchanged. Native exploration qualification follows.
- Routing doctor: static-ready root Astra/xhigh, unchanged configuration hash
  `1c901d09c3060eab1d41cf8fbde2fd6a96447bef1d2e687817d57bc124b3220a`.
  No project AGENTS/.codex overrides found. Recent Flash HTTP402 and prior user
  authorization support explicit Astra fallback; no provider setting changes.
- Root owns architecture, shared documentation and acceptance. One worker owns
  implementation, tests, narrow CMake, camera guide, report and local artifacts.
- Dispatched explicitly selected Astra worker
  `/root/cyberpunk_m2_camera_astra` with a clean child context and bounded PLAN.
- Worker finished and was explicitly interrupted. Root reviewed the actual
  header/source, all ten test groups, CMake integration, guide and raw evidence;
  no outstanding findings. See [ACCEPTANCE](ACCEPTANCE.md) and [REPORT](REPORT.md).
- Delivered `Phlosion::Camera`: copied immutable authored settings, owner-thread
  follow/orbit state, rates/deltas, analytic damping, immediate inward sphere
  obstruction response, reset, copied hit metadata and relative movement.
  Real InputActions/character/camera composition repeats walk/run, blocking,
  orbit-relative movement, focus/rearm and obstruction recovery/reset.
- Final full Debug build and all fifteen CTest entries passed (333.88 seconds).
  New targets use actual `/W4 /WX`; the camera test imports only Windows/C++
  runtimes. Wrong-owner destruction exits expected 73. Original compiler cache
  flags and the known legacy aggregate warning exception remain unchanged.
- Evidence under ignored `artifacts/m2-e/` includes raw commands/logs/exits,
  compiler/link records, import proof, fifteen source/document/binary hashes
  and seventy evidence hashes. Root matched all fifteen delivered hashes.
  Failed development runs remain recorded; no rerun of the already-passing full
  suite was needed during acceptance. PLAN remains the original hashed contract.
- Root updated roadmap, verification, ownership/consumer documentation and
  related collision/character cross-references. The user subsequently authorized
  committing this accepted pass. Root rechecked scope, whitespace and recorded
  source/binary hashes; no implementation/test changes required another run.
  The commit contains M2-E and its documentation only. No push was requested.
- Resume with a public walkable runtime/editor fixture using the accepted input,
  character, trigger and camera APIs. First define host-owned action cadence,
  controller selection, viewport capture/focus and mouse delta/scroll delivery;
  InputActions still has no motion/scroll bindings. Preserve the existing plugin
  ABI unless a reviewed extension is actually needed. Use synthetic/public
  engine fixture content; game-specific Outskirt Stand assets stay in the game.
- Native qualification must cover both hosts on OpenGL/Vulkan/D3D12 at matching
  frames and 16:9/21:9, with expected scene/player/camera content, hotplug/focus
  recovery, blocking and reset. Physical controllers and game-scale behavior
  remain unverified. M2 is not complete. Camera qualification here is final
  radial sphere clearance, not a swept eye trajectory or whole-frustum clearance.
