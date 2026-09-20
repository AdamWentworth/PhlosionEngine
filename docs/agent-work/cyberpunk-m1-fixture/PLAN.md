# M1-D: public archive-to-render fixture

Date: 2026-09-20. Astra-approved implementation brief.
Baseline: clean engine main `83e02ffb66ccd28e006db1f2f4a3fc0d37ed0fd9`.
M1-C was committed under the user's instruction before this pass. Nothing pushed.

## Objective and architecture

Add a distributable synthetic fixture proving that an archive-session consumer
can present loaded content through all three native renderers in a standalone
executable and the real PhlosionEditor scene preview. This is the next bounded
M1 pass, not completion of M1 or readiness for the Cyberpunk game.

Use one shared fixture data/presentation implementation under
`examples/public_asset_fixture/`, a small runtime executable and an editor
project plugin using the existing ABI. Keep it outside engine production APIs.
Generate deterministic, license-free synthetic archives through existing PHRC/
PHSC encoders, with explicit required dependencies and content hashes. Render
geometry/material/texture data decoded from the activated snapshot; no fallback
hardcoded scene when loading or decoding fails. A small documented versioned
fixture schema is appropriate, not a new general asset format.

The content must visibly exercise textured indexed geometry, depth occlusion,
alpha blending, and a rigid moving marker. A replacement scene changes both
geometry/presentation and a texture at the same logical asset identity, so stale
GPU cache reuse is observable. Keys must distinguish changed content. This pass
does not claim skeletal animation, production PBR/material interoperability,
private Pokemon assets, streaming, physics, UI/audio, or performance budgets.

Use PhlosionArchiveSession for initial activation, failed replacement and valid
replacement. Decode a candidate's fixture content successfully before activating
it; errors preserve the prior visible presentation. Keep content/snapshot/GPU
input lifetimes valid, including shutdown. Existing renderer capture hooks and
editor automation are sufficient integration surfaces; do not introduce a new
editor transport, backend factory or migrate Application's renderer selection.

## Deterministic capture contract

One process per case is acceptable. Within a process, replacement cases must
actually activate A and then attempt the replacement using the same session and
presentation/cache ownership; do not simply start directly with the final scene.
The four cases on each host are:

1. Initial A, logical simulation frame 0.
2. Initial A, logical simulation frame 30 at fixed 1/60 second steps.
3. A then invalid required dependency replacement, preserving A at frame 30.
4. A then valid B replacement, showing B at frame 30.

Logical frame selection may be an explicit fixture argument/environment setting
for the editor plugin. Distinguish that frame from host capture/frame counters;
never infer a matching logical frame from wall time. Preparation has a finite
timeout, fails explicitly on unexpected errors, and finishes before capture.
Record requested/actual backend, GPU, host and logical frame, case, active root/
content identity, load/transition outcome and dimensions. No native fallback may
pass. Runtime and editor can have different viewport sizes; compare APIs within
each host at identical viewport dimensions/camera, and validate expected content
on both. The editor must load the actual plugin through a generated project
descriptor. A fake editor wrapper does not qualify editor coverage.

Use existing screenshot environment controls and editor CLI/metrics. Keep image
outputs and full logs under ignored `artifacts/m1-d/` with per-run fresh paths.
Never accept stale screenshots or metrics after failed processes. Use hidden
windows and bounded process waits, restore modified environment variables, and
avoid destructive cleanup outside the intended artifact directory.

## Verification policy

Provide a reusable PowerShell runner plus image/content guards with no new
external dependency. Existing game parity scripts may inform generic algorithms,
but the engine fixture must run independently of game/private repositories.
Record all 24 captures (4 cases x 3 APIs x 2 hosts) in a machine-readable matrix
with native backend and transition/frame handshakes. Pairwise API comparisons
within each host/case use fixed thresholds: normalized MAE <= 0.01, RMSE <= 0.08,
changed-pixel ratio <= 0.05 at channel tolerance 8/255. Editor chrome must not
pollute scene comparison; compare a reliably identified viewport crop and retain
full screenshots. Do not loosen thresholds to pass. Escalate a real shared
backend/host defect rather than silently dropping coverage.

Before measuring captures, define robust expected-content checks for every
object, checker/changed texture, occlusion, blending and marker position. Use
interior regions/color classes or similarly meaningful checks, with sufficient
margin for native numerical differences. Blank/equal/missing-object/wrong-frame/
stale-A-in-B outputs must fail even if image differences are small. A-vs-failed-A
must be identical for the fixed logical frame; A0-vs-A30 and A30-vs-B30 must have
the expected visible differences. Include meaningful CPU regression coverage for
fixture validation/lifecycle and adversarial tests of the image guards. Do not
put GPU launches in normal CTest; CPU checks may be registered there.

Build all existing Debug targets with warnings as errors in build-presentation,
run the full existing CTest set plus new CPU checks, run the complete local GPU
matrix, inspect the screenshots/contact sheet and record evidence. Report exact
commands, exit statuses, metrics, source identity and limits in REPORT.md.
Applicable sources: docs/ENGINE_BOUNDARIES.md, docs/VERIFICATION.md,
docs/ARCHIVE_ACTIVATION.md, docs/ASSET_DEPENDENCY_LOADING.md, and the standing
consumer parity contract at ../../Games/PokemonAutochess/docs/
RENDERER_PARITY_CONTRACT.md (read-only).

## Ownership and acceptance

One native astra_flash_builder owns discovery, implementation, testing and visual
QA in examples/public_asset_fixture/, new fixture-related tests/tools,
CMakeLists.txt, the necessary test registration, docs/PUBLIC_ASSET_FIXTURE.md,
and this phase's REPORT.md. No engine renderer/editor/core production changes,
game/package changes, dependency updates, commits or pushes without returning a
concrete architecture blocker to Astra. Do not invoke other agents. Preserve
others' edits; the worker is not alone in the workspace.

Astra owns this PLAN, CHECKPOINT, roadmap/index/verification integration and
acceptance of the actual patch and evidence. Worker reports ready for review,
never self-accepts. Target one coherent implementation loop, allowing up to an
hour for implementation and native GPU debugging; checkpoint concrete blockers
or host limits, not arbitrary five-minute intervals. Astra will batch any review
corrections into one follow-up. All 24 native captures and meaningful expected
content checks are required for full M1-D acceptance.

Alternatives rejected: private game fixtures prevent independent distribution;
CPU-only or startup checks cannot establish rendering; separate host render code
would hide parity drift; expanding production backends before evidence creates
unnecessary compatibility risk.

## Organization adjustment before commit

The user asked Astra to verify semantic placement before committing M1-D.
Keep the runnable sample (FixtureData, Session, Presentation, RuntimeMain and
EditorPlugin) under examples/public_asset_fixture/. Move image comparison support,
the guard CLI, process launcher and capture runner to tools/public_asset_fixture/.
Move the automated PowerShell harness to tests/public_asset_fixture/. The existing
C++ contract stays under tests/. Separate the example library from a verification
support library so the sample does not depend on its checker. Tests and the guard
may depend on verification support, which may use the example's shared data types.
Preserve public target names/options, all behavior and every accepted threshold.

This is a bounded relocation/wiring/docs task for the same worker, not another
feature. Update CMake paths, PowerShell script roots/imports, relative links and
the fixture guide/report plus a short example README mapping the directories.
Do not rewrite historical artifact logs; mark old paths as historical where
necessary. Root owns roadmap/index/verification and this PLAN/CHECKPOINT.
The before-state hashes are artifacts/m1-d/reorganization-baseline.json.
Build Debug and run all six CPU/CTest entries, exercise the relocated runner's
complete native matrix once to validate its path/project/plugin resolution, and
compare source hashes to distinguish moves from edits. Report commands/evidence
and final paths. Do not commit; Astra reviews and commits under the user's request.
