# M2-F checkpoint

Status: implementation and automated qualification accepted. Physical runtime
and editor controls exercised; manual editor toolbar checks deferred by the user.
User authorized committing the accepted pass and requested holding afterward.

## Resume here

- Root resumed after the worker hit a usage limit and interrupted that completed
  child. No worker remains active. All final launched commands completed with
  exit 0: build-full-03, 20/20 CTest (349.04 seconds), matrix-04 (96),
  supplemental-01 (12), scene-regression/final-02 (36), negative-01 (27 rejected
  mutations plus pristine recovery).
- Root verified 936 unique file hashes, twelve actual compiler flag records,
  the final image sheets and a raw 21:9 editor capture. Code review and the one
  correction cycle are complete. No code edits or repeated full tests needed.
- Visible D3D12 runtime (PID 12704) and editor (PID 30048) were launched and
  closed. Evidence: `artifacts/m2-f/manual-20260922-155942/`. User answered both
  manual questions with all checks passed. Device: `Xbox 360 Controller`.
- Runtime trace corroborates movement/run/orbit/zoom/focus/pause/reset. Editor
  trace corroborates movement/orbit/zoom/focus, but has no toolbar Pause/Stop/
  Scene-switch/reset transitions. On clarification, the user explicitly asked
  to leave those manual checks pending. Do not reopen a test window unless the
  user resumes them.
  No SDL inventory detach/reconnect appears in either trace; retain the user's
  hotplug observation as reported, without claiming independently logged detach.
- Acceptance/report/readiness record that hardware limit. Final evidence catalog
  is `artifacts/m2-f/final-hashes.json`; documentation links, whitespace, empty
  staging and unchanged accepted-contract checks pass. The accepted pass is ready
  for the user-authorized commit. Hold after committing; further asset fitting,
  Outskirt Stand qualification or manual checks require the user to resume work.

## Retained implementation history

- User authorized the remaining M2 work possible before character/environment
  assets arrive. Baseline clean main `36b6986`; no new commit/push is requested.
- PLAN covers opt-in ABI-preserving editor SDL input extension, shared public
  walkable course and real editor game preview/runtime integration, authored
  camera settings, deterministic replay and full native verification.
- Required native matrix: eight states x two hosts x three APIs x two aspect
  ratios = 96 captures, expected-content/state/provenance checks, plus existing
  scene fixture regression. CPU contracts and negative evidence harness required.
- Root owns architecture/shared docs/acceptance. One worker owns implementation,
  testing/debugging/native QA, fixture guide/report and ignored artifacts/m2-f.
- Existing approved Astra fallback applies after Flash HTTP402. Doctor remains
  static-ready, root Astra/xhigh, configuration hash unchanged:
  `1c901d09c3060eab1d41cf8fbde2fd6a96447bef1d2e687817d57bc124b3220a`.
  No project AGENTS/override found; no provider changes or inference probe.
- Dispatched explicitly selected Astra worker
  `/root/cyberpunk_m2_exploration_astra` with the full bounded contract and a fresh
  child context. Root does not edit worker-owned implementation paths.
- User confirmed a physical gamepad is available for a short check at the end.
  Finish automated native qualification first; root will then launch the visible
  course and coordinate actual hardware observations. No physical qualification
  claim until those observations exist. Final asset-dependent scene work remains
  outside the public course's evidence.
- Root approved a narrow hidden-replay environment exception after native SDL
  suppressed the owned virtual pad in a hidden window:
  `SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS=1` only in isolated opt-in replay child
  processes, with restoration and explicit evidence metadata. Normal launches,
  physical checks and user/session hints remain unchanged; see PLAN.
- Development status reported by worker: CPU replay/course contracts pass;
  all 96 capture files were produced in both hosts at exact 832x468 and
  840x360 viewport dimensions. This is not a native PASS. The independent
  content guard rejected D3D12 captures containing sky but no course. Worker
  corrected the sample sky quad from near clip Z=0 to far clip Z=1; D3D12
  runtime/editor re-probes restored the course without renderer changes.
  Two supplemental traversal cases (steep slope and ledge drop) will add 12
  separately guarded 16:9 captures without changing the exact primary 96 cases.
  Final full build/CTest, final matrices, root review and physical check remain.
- Root reviewed all implementation/test/tool changes after source handoff.
  ABI 34, InputEvent and accepted subsystem algorithms are unchanged. One
  correction cycle fixed unsigned replay-number narrowing and strengthened
  actual staged project/plugin plus required-source provenance. Root reviewed
  the fixes and targeted negative tests; no further code correction requested.
  Worker is rebuilding/requalifying the final binaries. Pre-correction matrix-03
  passed 96 with zero pixel differences; scene final-01 passed 36. Those runs
  remain development evidence until final corrected evidence is complete.
