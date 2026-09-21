# Astra acceptance: engine-facing M1 closure

Accepted 2026-09-20 (local date), against main `56cdf24` plus this reviewed
change. The user subsequently authorized committing the accepted M1 work and
deferred M2. The four agreed engine-facing M1 exit gates are complete. Private
game-content qualification remains separate; no push is authorized.

## Scope and routing

The requested Flash worker failed with HTTP 402 before editing. The user
explicitly authorized Astra fallback. Root Astra implemented bundle L and the
consumer/verification integration; a separately selected `gpt-6-astra` worker
implemented the independent native GPU foundation. The completed worker was
interrupted. Reports: [L/integration](REPORT-L.md), [GPU foundation](REPORT-M.md).
Static router doctor evidence is in [CHECKPOINT.md](CHECKPOINT.md); it is not a
claim of successful Flash inference or billing for this pass.

## Acceptance review

Specification: real store-only Scene/Prefab glTF loading, worker decode, atomic
publication, preserved active state, cancellation/stale-result suppression,
retained snapshots, explicit byte accounting, native ownership and repeated
live-host reclamation are implemented through consumer-accessible interfaces.
The fixture joins these interfaces with the existing character/environment,
animation and material path on every supported API and the actual editor.

Code review covered modified and new source files: importer resolution, session
thread/epoch/stop ownership, renderer preflight and scope balancing, backend
cache namespaces, completion boundaries, dedicated Vulkan allocations,
descriptor recycling, independent live caches, failure status and accounting.
An adjacent GL profile-teardown ledger issue and D3D12 explicit-shutdown cleanup
were corrected. CPU helper tests cover debit/rebuild and failed completion.
Native material-profile switching itself is outside the fixed-profile matrix;
its GL teardown correction has source/CPU evidence, not a hot-reload visual claim.

The native guard correction selects the authored capture frame for each mode:
42 with lifecycle warmup, 6 for legacy. No expected-content or parity thresholds
were loosened. Production code remains in `src/engine/render`; sample content
and deterministic test orchestration remain in `examples`, evidence guards in
`tools`, and automated checks in `tests`. No plugin ABI, shader feature, cooked
format, game content or provider settings were changed.

## Verification

All paths below are relative to ignored `artifacts/m1-closure/`.

- Full Debug `/WX` build: `build-final.log`, exit 0. Final guard/contracts rebuild:
  `build-guard-final.log`, exit 0.
- Full CTest: `ctest-final.log`, 9/9 passed, 296.29 seconds. After the guard-only
  correction, both affected entries passed again in `ctest-guard-final.log`
  (88.79 seconds), including all 32 registered C++ contracts. The unchanged
  other seven entries retain their passing evidence.
- `lifecycle/lifecycle-final`: 36 captures, six cases by three APIs by two hosts,
  12 unique reload/release cycles per process, matching frame-42 observations.
- `regression/scene/scene-01`: 36 legacy captures; archive `archive/archive-01`:
  24; animation `animation/animation-01`: 36; material `material/material-01`: 18
  (the last three directories are also under `regression/`).
- All 150 captures passed expected-content/parity/provenance guards. Final
  `final-audit.json` rehashed all 20 recorded binary identities and checked sizes.
  All 36 lifecycle PNGs are byte-identical to the legacy scene PNGs, themselves
  byte-identical to M1-K's accepted PNGs. The contact sheet was visually reviewed.
  Automated lifecycle evidence tests reject 20 adversarial mutations in addition
  to the existing scene harness's 29 mutations. Whitespace and changed Markdown
  link checks passed.

Commands use the existing `build-presentation` tree and the four
`tools/*_fixture/run_capture_matrix.ps1` runners, with `-Lifecycle` only for the
managed scene matrix and `-ExpectedBaseline 56cdf24` where supported. The first
`lifecycle-01` run found only the stale frame-6 manifest requirement; it is
historical, not the accepted final run.

## Measured accounting

Each API supplied 156 measured archive decodes across its 12 final captures.
Encoded archive graph payloads were 16,453–22,741 bytes; decoded geometry/pixel
payload was 14,100 bytes per loaded scene. These are tiny public inputs.

| API | Decode min / median / max (ms) | Live native buffers / textures | Material / texture descriptor entries | Managed payload bytes | API allocation bytes |
|---|---:|---:|---:|---:|---:|
| OpenGL | 2.657 / 2.890 / 4.227 | 16 / 30 | 0 / 0 | 15,264 | 15,264 estimated |
| Vulkan | 2.723 / 2.914 / 4.917 | 8 / 30 | 48 / 0 | 15,264 | 36,528 |
| D3D12 | 2.589 / 2.806 / 4.442 | 16 / 30 | 48 / 30 | 15,264 | 3,014,656 |

Native totals include the scene and independent probe. Different allocation
sizes reflect backend allocation granularity; these are not driver/process
memory. Equal-sized reload cycles retained stable native totals. Every unload
case reached zero managed objects, descriptors, bytes and scopes while the host
continued rendering. Retirement was observed at the next frame, not shutdown.

## Limits and next gate

One Windows/NVIDIA GTX 1070 Debug host and the documented opaque, shared-UV,
bounded skeletal glTF subset. Shared environment/fallbacks, heap/pool capacity,
transient resources, metadata, decode temporaries and external CPU holders are
outside the logical payload cap. Cancellation suppresses publication rather
than interrupting an active read/decode. Destruction joins; archive interpretation
and GPU maintenance can block. This is not general streaming, hard latency,
peak-memory or district-scale performance qualification.

Next recommended milestone: a bounded M2 walkable scene with collision,
character movement, camera and input, followed by useful dynamic-lighting work.
No M2 implementation is included in this pass.
