# Scene inputs prepared once per view family

2026-10-29. Implementation, builds, processing and all tests run on gpu-worker.
controller owns review, exact-delta integration and canonical commits.

## Connected change

A render-view family is a primary field/battle view and its dependent shadow,
surface, motion and reflection views. Field glTF actors now prepare owned poses,
world placements and stable identities before those consumers draw. Wes locomotion,
Willie, story actors and interior residents use the same current inputs across
views. Hidden story actors still sample for attachment/diagnostic state; the
truck root is prepared before attached actors. Per-view visibility, diagnostic
hiding, lighting/material options, native packets and history remain view-specific.

Battle glTF cast and stand-in poses sample once at the first dynamic consumer.
Ghost/trail samples stay separate; native Pokemon retain their existing
ModelSupport/engine preparation. There is no cross-frame actor-pose cache.
Prepared field values live until every dependent draw has finished recording;
the existing native recording fence/lifetime rules remain unchanged.

The generic engine GltfSceneContentRevision stores current asset generation,
pose-content hash, exclusion-content hash and static eligibility. The game prepares
it once and shares it between static-shadow and placed-probe revision builders.
Both retain their previous identity formula. Probe placement, light projection,
per-light spatial membership, reload/in-place invalidation and the static-shadow
disable policy retain their existing semantics. It is an owned value, not an
address-based or time-based cache.

This implements the prepare-once/consume-by-view organization identified by the
source trace in NATIVE_COMMAND_PREPARATION.md and the research
lighting/reflections/SOURCE_RENDER_CALL_PATH.md (light collection, static/dynamic
shadow jobs and placed-probe scheduling). Implementation is independent Phlosion
code. This does not establish complete source scheduling or current-retail parity.

Qualification also exposed an existing failure when entering the Colosseum bar:
the accepted D3D12 baseline requested primary composition with disabled outdoor
lighting and aborted at frame 565. Field composition now runs only with enabled
lighting. The specialized interior still draws normally, with native motion and
temporal capture active. This is not a quality option or a lighting reduction.

## Verification

Optimized MSVC RelWithDebInfo runtime/editor builds pass. Nine CPU groups cover
engine contracts, retained preparation, motion, shadow/probe revisions, field and
battle state. The game test's stale ABI40 assertion now matches the pinned
engine ABI45; the plugin interface itself did not change in this task.
One cached GltfSceneDrawPlan object produced LNK1163; rebuilding that generated
object cleared it. Original failure receipts are retained.

Twenty-four native D3D12/Vulkan validation runs cover relighting with wetness,
emission and haze, and placed-probe stability/partial/publication/narrow updates.
Twelve baseline/candidate scene pairs cover runtime wet/dry/orbit/battle and editor
wet/battle on both APIs, with no native API errors or new D3D12 warning IDs.
The final corrected candidate was checked after the interior guard change.

Two additional story pairs preserve game/animation/attachment state; two candidate
interior runs complete entry and draw all five residents plus the player on both
APIs. Interior comparison to the accepted baseline is unavailable because that
baseline crashes at entry; this limitation is explicit in reference.json.
The first reference replay also contained an editor-only Escape that exits the
standalone runtime; that failed baseline receipt is retained and the replay
was corrected before the accepted reference checks.

Preparation diagnostics establish one preparation scope per render-view family.
Normal field frames sample 2 poses for 22 draws, battle samples 3 for 33 draws;
interior checks validate all five residents plus the player and one preparation
scope across geometry consumers. The disabled indoor environment omits the
per-pose counters from its handshake. These are sample scene observations,
not a general fixed actor/view count. The state comparisons include animation,
battle, exploration, motion history, rain, surfaces and weather audio.
Shader/material payloads are byte-identical to the accepted baseline.
Full-scene sheets and amplified difference crops are reviewed separately from
the strict max1 numerical result; see visual-review.json and image details.
No rain, light, foliage, assets, quality budgets or shader calculations were changed.

## Matched measurement

EVGA RTX 2060 SUPER GPU 1 on gpu-worker, native 1920x1080 Medium, fixed night/overcast,
optimized build, validation off, phase profiling on. Wet ABBA plus dry AB yields
12 runs; frames 40..179 are analysed. Settings are matched and all runs retained.
The actor column includes the new actor-prepare scope plus all actors scopes,
so moving the work outside the old draw scope cannot manufacture a saving.

| API / scene | GPU mean | Whole CPU mean | Active CPU mean | CPU p95 | CPU p99 | Actor preparation + drawing CPU |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| d3d12 / wet | 17.643 -> 17.584 | 18.149 -> 17.858 | 13.618 -> 12.791 | 28.608 -> 28.571 | 32.398 -> 30.865 | 2.447 -> 1.612 |
| d3d12 / dry | 12.376 -> 12.555 | 12.803 -> 12.839 | 9.230 -> 9.023 | 20.771 -> 21.048 | 22.656 -> 22.105 | 1.888 -> 1.291 |
| vulkan / wet | 18.472 -> 18.390 | 18.304 -> 18.240 | 6.562 -> 5.991 | 19.509 -> 19.429 | 20.075 -> 20.312 | 1.527 -> 0.986 |
| vulkan / dry | 13.641 -> 13.616 | 13.463 -> 13.441 | 4.628 -> 4.128 | 14.698 -> 14.685 | 15.386 -> 15.271 | 1.237 -> 0.760 |

Maximum observed GPU temperature: 62 C; no accepted run
has a thermal-slowdown flag. Full means, medians, tails, per-pass measurements and
memory remain in analysis-matched.json. This is a same-machine before/after
comparison, not a retail Cyberpunk benchmark. The 16.67 ms whole-frame gate is not
closed by a reduction in actor preparation; remaining GPU and CPU costs stay open.

The wet actor preparation-plus-drawing cost falls 2.447 -> 1.612 ms on D3D12
(34%) and 1.527 -> 0.986 ms on Vulkan (35%). Whole active CPU falls
13.618 -> 12.791 ms and 6.562 -> 5.991 ms respectively. GPU means change
by less than 0.1 ms in each wet comparison; this is a CPU preparation saving,
not an established GPU speedup. Whole wet CPU falls 18.149 -> 17.858 ms on
D3D12 and 18.304 -> 18.240 ms on Vulkan.

The dry D3D12 control has a higher GPU mean 12.376 -> 12.555 ms while whole CPU
is nearly flat 12.803 -> 12.839 ms. Wet Vulkan CPU p99 worsens 20.075 -> 20.312 ms,
while wet D3D12 p99 improves 32.398 -> 30.865 ms. These limits remain part of
the result; no run was discarded or replaced.

## Coverage and next work

Shared view preparation/submission remains **75-85% estimated technique coverage**.
This closes repeated primary glTF actor sampling and the duplicate scene-content
digest in shadow/probe preparation. Other producer, material/geometry input and
per-view collection work remains, as do broader shadow/probe scheduling, C7/C8,
Linux qualification and the performance gate. Other matrix rows are unchanged.

Next: carry reusable material/geometry inputs further upstream while keeping
visibility, light/caster membership and history specific to each view. Use the
measured pass costs to choose the next connected producer; do not cut quality to
replace missing architecture.

Worker evidence: WORKER_EVIDENCE_ROOT/phlosion-scene-preparation-20261029.
Controller evidence: artifacts/lighting-foundation/scene-preparation-20261029.
