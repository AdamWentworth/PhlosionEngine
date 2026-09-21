# M2-A checkpoint

Status: accepted by root Astra; first M2 pass complete. User authorized the
implementation commit on 2026-09-21; see repository history for that commit.

- Baseline main `08f57919785671d1120928b74fe56cbafad5265a`, clean before this plan.
  M1 is committed. User requests first M2 pass, no automatic commit/push.
- PLAN defines a CPU-only Jolt-backed static collision/query module, standalone
  collision contract executable, pinned manifest integration and scope limits.
- Existing vcpkg baseline resolves Jolt 5.4.0#0; no dependency/baseline upgrades.
- Root Astra owns architecture, acceptance and shared docs; one implementation
  worker owns only the paths in PLAN. Native/render/editor behavior is unchanged
  by scope; full CPU/build checks required, no new visual qualification claim.
- Static doctor ready: Astra/xhigh root, Flash/high role; config hash unchanged
  at `1c901d09c3060eab1d41cf8fbde2fd6a96447bef1d2e687817d57bc124b3220a`.
  No engine/ancestor AGENTS or project config override found. Doctor does not
  prove inference/billing. Prior verified routing reused; user permits Astra
  fallback if Flash is unavailable.
- Flash child /root/cyberpunk_m2_collision returned HTTP 402 Insufficient Balance
  before editing and was interrupted. Root explicitly disclosed fallback;
  an explicitly selected Astra implementation child receives the unchanged
  bounded contract. Root retains architecture/shared docs and final review.
- Astra worker delivered the collision module, manifest/build integration,
  standalone CPU contracts, usage guide and REPORT. Root reviewed the actual
  source, tests, dependency/link evidence and full build/CTest logs.
- Full enabled VS2026 Debug build with /WX passed; all ten CTest entries passed
  (292.07 seconds). The collision executable imports only Windows/C++ runtime
  DLLs. The resolved Jolt 5.4.0 build uses the MSVC x64 SSE2 baseline, without
  added AVX/SSE4 features. Raw evidence is retained under artifacts/m2-a/.
- One consolidated review correction added transformed-mesh world-space
  contact tests and owner-thread rejection/independent-thread world lifetime
  tests. The affected /WX rebuild and all eight collision groups passed
  (0.09 seconds); full regression evidence was reused for unchanged code.
  Root inspected the final source and evidence. No production defect or
  tolerance changes; no remaining review findings.
- Root documented the opt-in target, engine/project ownership boundary,
  readiness progress and verification evidence. ACCEPTANCE records the final
  bounded decision. No renderer/host/game source changes or GPU claims.
- Stop after this first pass. The next M2 brief should cover fixed-step
  kinematic movement, grounding, sliding, slopes, steps and recovery tests.
  The initial pass performed no staging, commit or push. On 2026-09-21 the user
  requested the outstanding collision commit. Root rechecked the complete change
  scope and prior acceptance, rebuilt the collision target, and reran all eight
  collision contract groups successfully. Evidence: artifacts/m2-a/
  build-collision-precommit.log and ctest-collision-precommit.log. The previously
  accepted full build and 10/10 regression evidence remains applicable; no
  production code changed during the Wes digression. No push is authorized.
