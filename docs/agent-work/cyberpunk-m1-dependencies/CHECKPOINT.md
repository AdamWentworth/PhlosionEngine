# M1-B checkpoint

Status: M1-B accepted by Astra.
Date: 2026-09-20
Baseline: engine main `e2ec9266e21672c8b2bfe6655ce0a26a8e8f3d92` (clean).

Prior roadmap and M1-A work were committed before this pass, as requested.
See [PLAN.md](PLAN.md) for contracts, scope and acceptance. Root owns plan,
checkpoint, roadmap and docs-index updates; one native astra_flash_builder owns
the bounded implementation, tests, API documentation and verification evidence.

Worker: `/root/cyberpunk_m1_dependencies`, native `astra_flash_builder`, fresh
child context. Routing uses the previously verified DeepSeek V4.1 Flash setup;
the read-only doctor passed before dispatch with unchanged configuration.

## Accepted result

Added the owner-thread PhlosionAssetGraphLoader session over M1-A's background
byte reader, with caller-provided ID/path/kind resolution, required closure,
per-session deduplication, required hash/cycle checks, explicit skipped optional
edges, asset/edge/encoded-byte limits, terminal ownership and cancel/reset reuse.
The public dependency-only prefab -> scene -> raw fixture exercises existing
archive encoders and consumers. Existing formats and loader behavior are intact.

Astra reviewed all new production and test source, build registrations, API
documentation and the report against baseline e2ec926, including untracked
files. One consolidated correction bounded the provider gate wait with timeout
evidence that cancellation cannot discard, strengthened successful late-hash
coverage, added missing/corrupt required-child failures after a valid parent,
and corrected usage/lifetime comments. Astra inspected those changes and the
final build/test logs before accepting. Root then linked the API documentation,
updated roadmap progress and clarified that pump has no decode frame-time bound.

Verification on the corrected implementation:
- Full `cmake --build build-presentation --config Debug --parallel 4` passed;
  VS2026, editor/tests enabled, engine warnings treated as errors.
- Full `ctest --test-dir build-presentation -C Debug --output-on-failure` passed
  5/5 entries, all fifteen C++ contracts. Contract runtime: 1.94 seconds.
- A direct contract run passed. Earlier revision also passed twelve repeats;
  no repeat campaign was used in place of the final full suite.
- Earlier mutation checks caused the intended failures when required cycle
  recording or hash constraints were removed; mutations were reverted.
- Final whitespace and documentation-link checks passed.

Evidence: [REPORT.md](REPORT.md), plus ignored local `artifacts/m1-b/` logs,
especially `build-correction.log`, `ctest-correction.log` and
`contracts-correction.log`. The separate CI build tree was not rebuilt locally.

## Limits and workspace

Ready verifies required container-level content only, not typed-profile, model,
material or JSON semantics. Encoded-byte accounting excludes decoder/provider
temporaries, reader staging, metadata and transferred results. Decode runs on
the owner thread; blocked synchronous provider I/O can delay destruction.
No cross-session cache, ranged streaming, production caller migration, renderer
change, GPU qualification or private-content readiness is claimed.

The accepted M1-B implementation and root documentation integration are local,
unstaged and uncommitted. Only the prior roadmap/M1-A commit was authorized for
this transition and created as e2ec926; nothing was pushed. Worker completion
was interrupted for host bookkeeping after each review submission. Astra was
the planner/reviewer; the pinned native Flash role used the previously verified
DeepSeek route without a configuration/model change or new paid smoke test.

## Next action

Select and plan the first production consumer integration with explicit archive
decode and resource-lifetime boundaries, then qualify the rendered synthetic
fixture on OpenGL, Vulkan and Direct3D 12. M1 and the full game-readiness roadmap
remain open. Hardware/content budgets and private character qualification still
need their own evidence. Review/commit M1-B under the user's next Git instruction.
