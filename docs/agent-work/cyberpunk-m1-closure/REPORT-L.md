# Bundle L and consumer integration report

Baseline: main `56cdf24`. Implemented by root Astra after the requested Flash
worker returned HTTP 402 before editing and the user authorized Astra fallback.
No commits, staging, pushes, game changes or provider configuration changes.

## Delivered

- Store-only glTF/GLB import shares the existing importer, resolves buffers/images
  within the supplied virtual store and preserves failed outputs. Filesystem
  loading remains available. The parser decodes valid URI escapes before path
  normalization/rejection; safe encoded filenames are supported, encoded escape
  and control/query/fragment paths are rejected.
- `GltfSceneSession` composes archive/dependency loading with one glTF decode
  worker. It publishes the matching archive/scene atomically, preserves active
  during failed/cancelled replacement, rejects stale results and supports a new
  request without joining the cancelled decode. Destructor stop/join and retained
  snapshot lifetime are explicit. Active plus accepted candidate logical payload
  is capped; encoded bytes, decoded bytes and decode duration are reported.
- The shared glTF renderer accepts an optional native resource scope, balances
  it on every return and surfaces native allocation failures. Scope zero keeps
  existing behavior. CPU preflight still happens before submission.
- The engine-owned example optionally consumes real encoded scene archives,
  runs 12 unique reload/cancel/retained-draw/release cycles, preserves a second
  live GPU scope, then performs the original six visual cases. Every frame
  records native owned objects/bytes and allocation/reclamation history.
- Lifecycle evidence validation checks frame order, bounded retained counts,
  independent ownership, observed next-frame releases, distinct sources,
  timing/byte fields and zero managed resources after live-host unload. It adds
  20 adversarial CPU evidence mutations without weakening image thresholds.

## Review and verification

CPU checks cover real Scene/Prefab archives, PNG/buffer/data URI/GLB imports,
missing dependencies and malformed content, store/filesystem equivalence,
thread identity, gated cancellation/discard, late completion, worker exceptions,
provider/destructor lifetime, retained poses and exact/overflow budget edges.
The adapter tests cover unsupported/foreign scopes and native failure reporting.

The full warning-clean Debug build passed. All 32 CPU contracts passed and the
full nine-entry CTest suite passed in 296.29 seconds. After the final
manifest-frame guard correction, both affected contracts/harness entries passed
again in 88.79 seconds. Final native acceptance is recorded in
[ACCEPTANCE.md](ACCEPTANCE.md).

During review a test incorrectly expected a safe percent-encoded filename to be
rejected; it now positively verifies the parser's decoded path and independently
rejects encoded traversal/absolute/control/query/fragment paths. The first native
matrix found an obsolete manifest frame-6 requirement. Managed mode now requires
frame 42; legacy mode still requires frame 6. The actual native lifecycle and
content checks passed on that first run. These corrections change neither the
supported material subset nor visual thresholds.

Reusable production code lives under `src/engine/render`, authored content and
deterministic host orchestration under `examples/scene_fixture`, evidence guards
under `tools/scene_fixture`, and automated checks under `tests`. General streaming,
hard frame-time/peak-memory guarantees, private game assets and M2 are outside
this pass. See GLTF_SCENE_SESSIONS.md and WORLD_RESOURCE_SCOPES.md for limits.
