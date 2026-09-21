# M1-K checkpoint

Status: accepted by Astra on 2026-09-20; included in the user-authorized commit
`Integrate owned glTF scenes with shared rendering`. Nothing pushed.

- User authorized working toward remaining M1 after choosing bounded engine-side
  integration before lighting. PLAN.md defines the concrete next pass and limits.
- Baseline main cc600e1f9722b7da7c198102a26763a469e8ee4c. Pre-existing root-owned
  edits: docs/CYBERPUNK_ENGINE_READINESS.md and the M1-J CHECKPOINT.md. No staged
  files or other untracked work at orientation.
- Root Astra/xhigh; native astra_flash_builder pins DeepSeek V4.1 Flash/high.
  Doctor static-ready, catalog route present, standalone role inspected. Current
  config file hash 1c901d09c3060eab1d41cf8fbde2fd6a96447bef1d2e687817d57bc124b3220a
  differs from the older checkpoint, but relevant root/worker bindings are
  unchanged. No engine/ancestor AGENTS.md or project config override found.
  Reuse prior verified M1-D host/router inference evidence; no inference smoke or
  settings change. Static checks are not a fresh inference verification.
- Architecture: synchronous CPU-owned glTF scene snapshot plus generic shared
  world draw adapter; reuse pose/import/decode helpers, preserve legacy defaults.
  New raw base decode policy + separate GPU factor and implicit-white absent
  emission are scoped to the new supported scene path. No renderer change.
- Worker: /root/cyberpunk_m1_integration, native astra_flash_builder, clean child
  context with full brief and PLAN path. Pre-existing diff/hashes retained under
  artifacts/m1-k. Root owns shared docs and phase plan/checkpoint; worker owns
  scoped code/tests/guide/report. First handoff reported 29 CPU / 9 CTest /
  36 scene + 78 regression captures passing. Child interrupted on completion.
- Astra inspected the new engine implementation, changed existing sources,
  sample, tests, guard, runner, guides and native contact sheet. Boundary is
  appropriate; acceptance withheld for correctness/evidence gaps detailed in
  ASTRA_REVIEW.md. Same child owns the single consolidated correction pass.
  This first review was followed by the correction history below.

- Flash's correction turn stopped with provider HTTP 402 Insufficient Balance.
  Child interrupted immediately. Partial changes preserved; first handoff's
  REPORT/evidence are historical, not qualification of these changed sources.
  An interrupted build failed at an unused guard variable under /WX (log:
  artifacts/m1-k/astra-interrupted-build.log). Original two root-owned docs
  still match pre-dispatch hashes; file inventory captured in
  artifacts/m1-k/astra-interrupted-files.json. Nothing staged or committed.
- User explicitly chose "Have Astra finish the corrections" in response to
  the routing blocker. This overrides the Flash-only implementation workflow
  for the remaining correction; no new child or provider changes are needed.
- Astra completed all seven review finding groups and added the material-index
  overflow regression. Final full Debug /WX build and scene-only option build
  passed; 29 CPU contracts and all nine CTest entries passed (284.37 seconds).
- Final native evidence: `artifacts/m1-k/matrix/m1-k-accepted-scene/` (36),
  `artifacts/m1-k/regression/archive/m1-k-accepted-archive/` (24),
  `artifacts/m1-k/regression/animation/m1-k-accepted-animation/` (36) and
  `artifacts/m1-k/regression/material/m1-k-accepted-material/` (18). All passed
  on Windows/GTX 1070. Root inspected the scene contact sheet; the final audit
  rehashed all 16 binary identities and passed links/untracked whitespace.
  Logs and review details are in [ACCEPTANCE.md](ACCEPTANCE.md).
- Root integrated the supported-path/fixture guides, verification record and
  readiness checklist. No game repository, shader/backend, legacy cache schema
  or editor plugin ABI changes. Nothing staged, committed or pushed.
- Next M1 work: connect the owned CPU scene to existing asynchronous loading
  and archive activation, then qualify GPU resource release/residency through
  repeated replacement/unload. Define a bounded brief before implementation;
  the current synchronous path is accepted, but all M1 is not complete.
- The user authorized committing the accepted work and requested a percentage
  estimate only afterward. Commit preparation changes status documentation only;
  accepted production sources and verification are unchanged. Do not begin the
  next implementation pass until requested.
