# M1-C checkpoint

Status: M1-C accepted by Astra.
Date: 2026-09-20
Baseline: engine main `b2e03c4ca702589311c7cdac068f8858f01d760e` (clean).

M1-B was committed before starting this pass as requested. See [PLAN.md](PLAN.md).
Scope: a staged scene/prefab resource session over the accepted graph loader and
existing typed archive stores; explicit activation preserves old snapshots on
candidate failure/cancellation. There is no engine application archive caller to
migrate; game/editor call-site and GPU integration remain separate work.

Root owns plan/checkpoint, roadmap/index. Worker `/root/cyberpunk_m1_activation`,
native astra_flash_builder with fresh child context, owns the bounded
implementation/test/docs bundle. Static routing doctor passed
with unchanged configuration; reuse the previously verified DeepSeek route.

## Result and acceptance

Added PhlosionArchiveSession with typed scene/prefab staging over the unchanged
graph loader and existing archive decoders. Explicit activation transfers a
shared immutable snapshot; candidate failures/cancellation/discard preserve the
active archive, and unload clears session ownership while caller-held snapshots
stay readable. Required encoded dependencies and skipped optional metadata are
retained with the root archive. No archive format or production decoder changed.

Astra inspected all new production and test source, registrations, docs and
verification evidence against b2e03c4, including untracked additions. One
consolidated correction fixed a stale-read test predicate (the path had already
been read by a previous candidate), exercised dependency-only prefabs as well
as embedded-file prefabs, verified provider lifetime independently of snapshot
lifetime, and added corrupt required-content rejection while A stays active.
The corrected gate tests require a fresh read and retain timeout evidence.
Astra reviewed those changes and final logs before acceptance. Root then linked
the documentation, updated roadmap progress and clarified the unload wording.

Final verification:
- `cmake --build build-presentation --config Debug --parallel 4`: passed,
  existing VS2026 editor/tests configuration, engine warnings as errors.
- `ctest --test-dir build-presentation -C Debug --output-on-failure`: 5/5 entries
  passed, all sixteen C++ contracts. Contract runtime: 1.56 seconds.
- Direct PhlosionEngineTests run: all sixteen contracts passed.
- Whitespace (including untracked files) and relative documentation links passed.

See [REPORT.md](REPORT.md) and ignored local `artifacts/m1-c/` final build,
CTest, direct-contract and check logs. No repeat or mutation campaign was needed.
Finished worker was interrupted for host completion bookkeeping. Astra planned
and accepted; implementation used the pinned native astra_flash_builder role
and the previously verified DeepSeek route, with unchanged configuration.

## Workspace and remaining limits

M1-B is committed as b2e03c4. Accepted M1-C plus root documentation integration
remain local, unstaged and uncommitted; nothing was pushed. Only this engine
repository was modified. Provider/destructor and owner-thread requirements
remain as documented. Per-candidate graph caps do not bound combined active,
staged, typed-decoded and externally retained snapshot memory.

This is a CPU resource boundary, not a migrated game/editor caller or a GPU
resource transition. Mesh/animation/material/environment contracts and the
rendered public fixture still need qualification; full M1 is not complete.

## Next action

After review/commit under the user's Git instruction, plan the public rendered
fixture and its first consumer using this session. Define a synthetic scene,
asset expectations and matching capture frames before delegating; qualify the
affected behavior on OpenGL, Vulkan and Direct3D 12, including editor preview
when touched. Keep game content adapters and private character qualification
in their own repositories and verification boundaries. Avoid claiming that
these CPU loading passes establish visual or complete game readiness.
