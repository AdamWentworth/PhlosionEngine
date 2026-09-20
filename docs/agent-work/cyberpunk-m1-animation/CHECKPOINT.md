# M1-G checkpoint

Status: accepted by Astra after one consolidated correction; authorized for commit.
Date: 2026-09-20.
Baseline: clean engine main 9c0d812ce2520f0fbfae6dd5bcccc0e42e118339.

The user authorized committing M1-F and moving on. Astra committed all 13 accepted
files as 9c0d812, Verify existing glTF character import and material metadata.
Prior build/6 CTest/21 CPU contract evidence remains valid; only commit-status
wording changed before staging. Staged whitespace check passed; tree was clean
after commit. Nothing pushed.

Next bundle exposes the existing CPU pose evaluator without changing behavior,
adds direct CPU tests, and demonstrates weighted GPU deformation in a public
runtime/editor fixture on all three native APIs. PLAN.md owns architecture,
boundaries, exact CPU interface and acceptance. Cubic interpolation and other
runtime feature changes remain outside this pass.

Root remains GPT-6 Astra/xhigh. Current read-only doctor is static-ready with
unchanged config fingerprint
237343dd9547ecc87450fada2a20e801718b62afe11628a371d707320f7d1a26.
The available native astra_flash_builder role pins DeepSeek V4.1 Flash/high.
Reuse host/router route evidence recorded in the M1-D checkpoint; no new paid
smoke test, fallback or settings change. No project override file was found in
the engine's applicable source/docs/test paths or its parent directories.
Explicit .codex/config.toml checks at the engine and each ancestor through D:/
also found no project routing override.

One fresh worker, /root/cyberpunk_m1_animation, owns the bounded implementation.
Astra will wait for the
complete loop and review the actual patch and CPU/native evidence before
acceptance. Only this phase's root-owned planning files are untracked at
dispatch. Leave M1-G uncommitted after acceptance; this turn's commit request
applied to the accepted M1-F work.

## First handoff and review

The worker returned ready_for_review and was interrupted for completion
bookkeeping. Initial evidence: build-full-03.log, ctest-03.log (7/7), tests-12.log
(23 CPU contracts), m1g-run02 (24 animation captures) and archive-regression/
m1g-archive-02 (24 archive captures), all under artifacts/m1-g. Astra inspected
the actual new/tracked source, CPU coverage, mechanical extraction proof,
contact sheet and full editor screenshot. These are pre-correction evidence,
not accepted final results.

One consolidated correction went to the same worker:
- Both hosts switched between separate bind/requested palettes rather than
  updating the same palette allocation; require stable submitted storage and
  observable transition evidence.
- Missing binaries skipped provenance validation. Astra's real-manifest
  mutation passed incorrectly; reproduction is retained in
  artifacts/m1-g/astra-review-aa1d9de93265476eb3a55f0c9ea936ee.
  Also validate tuple/capture-frame/host/clip/backend consistency and vertex Z.
- Synthetic disabled-skinning must apply the nonidentity mesh transform, unlike
  a stale bind palette. Distinguish measured pixel coverage from expected geometry.
- Add the claimed but absent LINEAR scale interpolation case.
- Let the capture runner default to current HEAD after later commits; keep
  explicit baseline enforcement. Atomically acquire harness temp root without
  -Force. Correct inaccurate shell-version and documentation claims.

Astra approved Model.cpp's small delegation to the same trsToMat4 function and
keeping new image primitives local rather than refactoring accepted archive
verification. Production sampling semantics remain frozen.

## Final acceptance

The same worker completed corrections and was interrupted after handoff. Astra
reviewed the stable palette allocation and both host call sites, mandatory binary
evidence and tuple/handshake consistency, corrected disabled-skinning model,
LINEAR scale test, finite/Z checks, and runner baseline handling. Required gaps
above are closed. Root inspected the final contact sheet, checked every final
binary hash and all 24 palette transition records (zero mismatches), and integrated
the guide/index/verification/roadmap. Source scope includes the approved small
Model.cpp delegation; no renderer backend, shader, cache or editor-host changes.

Final evidence under ignored artifacts/m1-g:
- build-full-05.log: full VS2026 Debug build, warnings as errors, passed.
- ctest-05.log: 7/7 entries passed; tests-fix-03.log: 23/23 C++ contracts.
- harness-fix-02.log: positive matrix and 24 negative guard cases passed.
- m1g-run04/: all 24 native runtime/editor animation captures passed; original
  0.01/0.08/0.05 limits and channel tolerance 8 retained. Worst normalized
  MAE/RMSE 0.0000038/0.00029. run04-vs-run03/ records pixel-identical recaptures.
- archive-regression/m1g-archive-02/ and archive-revalidation.log: all 24 archive
  cases passed; recorded host/editor/plugin/guard hashes remained unchanged.
- diff/extraction-diff.log: 183 normalized evaluator statements identical;
  source aliases, delegated helper and call-site order separately reviewed.
- child-default-off/: fixture hosts default off for child consumers, libraries
  remain available with tests enabled, sampler runs with no graphics context.
- Final Markdown links and tracked/new-file whitespace checked by Astra.

Coverage is shared CPU sampling and the world-mesh-textured GPU skinning path
for one public two-joint fixture on Windows/GTX 1070 Debug. No blanket animation,
private-character, legacy Model draw, instanced/indirect, production material or
performance qualification. Cubic-spline and morph-weight gaps remain.

The user has now authorized committing M1-G and proceeding. The next checkpoint
will record its exact revision. Nothing pushed. Next: a bounded pass for known
animation fidelity gaps and/or production character path qualification. Keep the optional
typed-format family separate from required gameplay readiness.
