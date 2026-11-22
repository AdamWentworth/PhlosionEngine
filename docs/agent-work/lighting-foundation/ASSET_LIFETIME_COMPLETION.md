# Asset loading and residency completion contract (M1-M8)

Fixed before implementation, 2026-11-05. Status: M1-M8 qualified for the fixed scope below; exact locked-build and
controller delivery receipts are the final acceptance record.
The category covers the current Windows runtime/editor asset producers and
scene/field/battle transitions. Shader compilation is a separate completed row.
100% requires all gates below, with source trace, tests and qualified delivery.

- M1: typed immutable resources have complete file/dependency and variant identity;
  changed or corrupt inputs cannot reuse stale data or replace a valid live scene.
- M2: repeated and simultaneous requests join one load; field/battle/collision
  reuse the same prepared scene, material and transport inputs where compatible.
- M3: bounded background preparation, priority, cancellation, failure and shutdown
  have explicit ownership; no deadlock, late publication or hidden unbounded queue.
- M4: normal scene/actor/vegetation/effect/audio consumers use the prepared-resource
  path; visual and audio definitions are not independently re-parsed unnecessarily.
- M5: CPU residency and temporary input budgets are explicit; unreferenced cached
  resources expire/evict, active consumers remain valid and pressure is diagnosed.
- M6: GPU admission/upload and retirement preserve immutable reuse and fence-safe
  lifetime; repeated scene return/quality transitions have bounded residency.
- M7: ordinary hosts pre-admit required encounter content without moving loading
  into battle activation; prepared publications and editor recovery remain intact.
- M8: independent failure/concurrency/resource tests, both APIs/hosts/presets,
  content/visual checks, loading and steady-frame costs, locked revisions and
  controller installation qualify the exact delivered build.

Source policies come from resource bank/token/scheduler/throttler and renderer
streaming traces, recorded in the private research repository. The current
location streams complete prepared assets at scene boundaries. Retail city-cell,
virtual texture page, proprietary archive/codec/platform identity are distinct
implementations and must be reported explicitly, not implied by this checklist.
No feature toggle or quality reduction substitutes for a gate.

## Connected implementation

One host-owned ResourceBank joins typed immutable requests and bounds two worker
threads, 256 queued/resident/retired entries, 1 GiB logical resident payload,
1 GiB total input per job and per-file decoder limits. Idle resources retain a
30-second grace period; host maintenance refreshes active consumers and evicts
unreferenced objects under pressure. These are logical payload limits, not an
assertion that the process RSS, decoder scratch and driver allocations fit 1 GiB.

Content fingerprints include every input read by the decoder; same-size,
same-timestamp edits invalidate a snapshot. Variant keys include glTF limits,
LOD groups and immutable layered-material identity. Existing live consumers
survive replacement failure. Complete immutable values publish only after
dependency revalidation and cancellation checks. Nested loads help their queued
dependencies; explicit entry-generation wait edges reject direct and indirect
cross-worker cycles. Cancelled subscriptions drop ownership, and a cancelled
generation cannot publish over a new request for the same key.

Field, battle and collision share glTF scenes and separately admitted layered
materials/indirect transport. Independent actor preparation is queued during
admission. Vegetation input bytes and sliced PCM are cached; mutable poses,
vegetation simulation, audio playback and view histories remain independent.
The VFX ABI exposes borrowed audio metadata for the call, copied by the host;
battle audio no longer parses already-prepared visual effect documents.
No plugin-created payload/deleter is retained in the generic renderer.

Native child scopes attach to a bounded immutable asset owner. D3D12/Vulkan
reuse geometry, textures and layered uploads, while view descriptors, mutable
wind/shelter outputs and temporal histories retain their own scope. The last
consumer schedules retirement; existing backend completion fences precede
destruction. An exhausted shared owner reports failure to the affected child.
Independent live views remain valid.

Editor ABI64, model ABI36 and battle VFX ABI21 require matching providers.
Normal runtime/editor paths use these services without an experimental toggle.
No rendering equations, shaders, scene placements or quality presets changed.

## Gate evidence

- M1/M2: authored glTF exact geometry/material control against the original
  importer; joined requests, type/variant separation, changed external buffer at
  unchanged size/mtime, corrupt replacement and live old-snapshot checks.
- M3/M5: concurrency, priority, nested loads, direct/indirect cross-worker cycles,
  active/queued cancellation, same-key generation replacement, failure/retry,
  queue/key/retired-metadata limits, input/resident pressure, pinned consumers,
  actual payload destruction, idle expiry and shutdown checks.
- M4/M7: original move-audio metadata comparison and exact PCM sharing; field,
  battle, collision and prepared package tests. Normal host traces contain no
  additional CPU resource loads/input reads between admitted field and battle
  frames or during repeated live quality changes.
- M6: native shared-vs-isolated geometry/texture counts, exact image comparison,
  surviving child replacement, final-consumer release/recreate, pressure failure
  recovery and exact allocated/reclaimed payload accounting on both APIs.
- M8: all four tiers in runtime/editor on both Windows APIs, motion/battle views,
  two reference returns and two repeated quality cycles; 58 game CPU tests pass,
  one legacy cooked-model depot test is explicitly skipped. Eight relevant engine
  suites include the new bank/import contracts and existing importer/resource,
  indirect/layered/audio and semantic boundary checks. Final publication requires
  the same checks on locked revisions and actual scene launches, not the picker.

Reference-scene return restores the exact original native payload on both APIs.
Normal scene content remains 1,146 plants and 411,472 shelter triangles, with
Hybrid surface-specific impacts and real-puddle rings. Controlled native images
are pixel-identical; whole-game captures are visually checked without claiming
pixel determinism across independent temporal/weather launches.

## Measured cost

| API | Weather | Before GPU | Current GPU | Before first frame | Current first frame |
| --- | --- | ---: | ---: | ---: | ---: |
| d3d12 | dry | 12.268 ms | 12.270 ms | 20.945 s | 18.263 s |
| d3d12 | wet | 17.536 ms | 17.559 ms | 20.759 s | 16.177 s |
| vulkan | dry | 13.485 ms | 13.459 ms | 20.333 s | 16.094 s |
| vulkan | wet | 18.221 ms | 18.396 ms | 20.360 s | 16.069 s |

Optimized RelWithDebInfo, 1920x1080 Medium, gpu-worker GPU1 EVGA RTX 2060 SUPER,
unchanged 175 W limit. Two reversed-order pairs per API/weather; 119 steady
samples per run (frames 121-239), mean of run medians. Thermal-slowdown pairs
are rejected with originals retained. Timing is separate from API validation.
First completed-frame timestamps exclude harness cooling/capture/shutdown.
Prepared shader/file/application/driver caches are retained; this is repeat
development startup, not factory-cold disk or driver compilation.

The initial matched-content resource inventory falls about 349 MiB per API
(from about 967/968 MiB to 618/619 MiB). The bank retains about 386 MiB of logical
CPU input payload in 82 entries and records 64 reuse hits during admission.
Move-audio preparation falls from about 2.65 seconds to about 0.07 seconds.
Memory reuse and preparation costs are distinct from steady rendering cost.

## Source mapping and limits

Research resource_loading/source-files.json and README.md record the read-only
pending-token, resident-bank, timed-cache, cancellation, throttling and mesh-
streaming trace. This is an original implementation of those observed policies.
The completion contract concerns current Windows whole-asset scene-boundary
streaming. CPU expiry releases unused objects synchronously at bounded host
maintenance points; native uploads still occur synchronously at scene admission.
Those are explicit adaptations of latent destruction/global job scheduling.
City-cell streaming, virtual texture pages, proprietary archive/codec/platform
formats and allocators, Linux/console qualification and retail FPS identity are
not established. Shader/pipeline compilation has its own completed category.

Evidence: WORKER_EVIDENCE_ROOT/phlosion-asset-lifetime-20261105 on the worker and
PROJECTS_ROOT/Phlosion/PhlosionEngine/artifacts/agent-work/asset-lifetime-20261105 on the controller. Failed attempts,
source/dependency/binary manifests, CPU/native/host results and timings remain.
locked-qualification.json binds acceptance to exact commits and prepared programs;
controller-installation.json records hash-verified installation and launcher
receipt validation. Local GPU execution is not claimed. No GitHub push is part
of this task.
