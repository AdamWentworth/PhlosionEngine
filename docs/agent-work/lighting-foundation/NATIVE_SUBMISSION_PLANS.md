# Native submission plans, command state and frame palettes

2026-10-30. Baseline engine 3691ff8, game 758637c, ModelSupport da6a969.
Implementation, optimized builds, CPU/GPU checks and processing ran on gpu-worker
Windows; controller owns review, integration and commits.

## Connected changes

Collection-owned native model views now retain the lowered material descriptors,
opaque/blended ordering, automatic-instancing groups and submission statistics.
Motion-capable and automatic-instancing policies have distinct plans. Camera,
projection, dimensions, motion enrollment, resource scope and native pass bindings
remain live. Plans borrow their immutable owning view/snapshot and die before it.
The existing sixteen-view/256-object bounds remain. Direct previews retain their
ordinary path. No public renderer or model ABI changes.

D3D12 command recording now binds only changed root values/tables, heaps,
pipelines, geometry, topology and viewport/scissor state within an uninterrupted
recording range. Heap/root changes invalidate the relevant cached state. Each
serial range or helper bundle owns a fresh recorder; it never imports an
unproven global command-list state. Outline/surface order remains intact.

Vulkan now binds the smallest contiguous changed descriptor-set range. Its four
dynamic offsets remain attached to set 1; an offset-only change therefore binds
that set. Private range state cannot leak between command buffers or recording
lanes. Effective-draw-state CPU traces compare against ordinary uncached recording.

Both backends now use the same content-checked frame palette cache. D3D12 can
reuse interleaved actor/instance poses, rather than only the last skeleton.
Vulkan retains its content-reuse behavior with common bounded retention and a
fast exact comparison for consecutive palettes. Comparison bytes live in CPU RAM.
A hash is only a lookup aid; exact bytes determine reuse. Same-address mutation
and equal data at different addresses are handled. Failed uploads are not
published. Retention is bounded at 256 entries/8 MiB of logical comparison bytes;
overflow uses ordinary uploads without dropping draws. The owner clears only at
the fenced frame boundary. Native instance offsets remain relative to their
correct frame buffer base.

These are normal paths, with no rendering feature opt-in or detail reduction.
Rain, material-specific splashes/puddle rings, authored lights, shader equations,
sample counts and quality settings are unchanged. All 61 shader/material payload
files match the baseline.

## Source mapping and scope

| Read-only source anchor | Mechanism addressed | Remaining difference |
| --- | --- | --- |
| renderMaterial.cxx, PrepareRenderStagePassesRange (2785) | Owned stage/material preparation reused by consumers | Our native plans are still built lazily on first consumption; the complete producer/material job graph is not implemented. |
| renderGeometryBatcher.cpp, ExecuteRenderGeometry (1385), GetBatchKeyDiff (1491) | Ordered geometry batches and state-difference binding in private recording contexts | API-specific packets, layouts and general material/pass coverage remain Phlosion adapters. |
| renderSkinManager.cxx, BindSkinningBuffer (329), PrepareForFlush (335), Flush (350) | Reusable frame deformation bindings with explicit ownership | Reference active-update/persistent-block ownership and joined upload jobs are not reproduced by a lazy frame cache. |
| renderCollector.cpp, DoCull_MainScene (339), DoCull_Cascades (367), local-shadow collection (877) | Explicit collection/preparation dependencies and joins | Joined glTF/foliage preparation and native recording exist; broader producer dependencies remain incomplete. |

Hashes/line anchors and the requirement audit are in reference.json,
scheduling-reference-index.json and category-audit.json. This supplied snapshot
is not authenticated as current retail. Phlosion baseline comparisons establish
regression behavior, not retail algorithm or performance identity. This pass
does not verify 100% of the shared-preparation category.

## Verification

Sixteen CPU groups/checks pass. Fifteen validation-enabled native checks pass
across D3D12/Vulkan: ordinary and game-material motion, interleaved palettes,
same-address mutation, instanced poses, cache overflow, retained glTF preparation,
native resources, recording and retirement, plus Vulkan editor resize/presentation.
Native capacity cases exceed the 256-entry retention bound and then reuse an
earlier pose, without stale deformation.

Eighteen fresh scene pairs cover runtime dry/wet/orbit/battle, editor wet/battle,
and Low/High/Ultra wet runtime for each API. Two additional afterimage pairs
retain all four ghosts. Scene-state, allocation and shader-payload checks pass.
Eight of nine Vulkan pairs are exact; High differs by at most 1/255.
D3D12 runtime battle and both afterimage pairs are exact. Other D3D12 scene
maxima are 1-7/255. Dry baseline repeats differ by up to 5/255, with candidate
versus repeat at 3/255. Wet baseline repeats stay within 1/255 while the candidate
differs by up to 6/255 (11 pixels above 4, none above 8 out of 2,073,600).
That stable wet residual is not explained by the repeat control. Full sheets and
exact-pixel maximum-difference crops were reviewed without broad geometry,
lighting, material or rain loss. The full scene set does not pass a strict
all-pixel 1/255 gate and is not described as pixel-identical. See visual-review.json,
image-review-details.json and repeat-controls.json for acceptance limits.

Battle diagnostics demonstrate 33 native view requests using 12 retained
submission plans and 60 lowered batch records. In the validation-enabled D3D12
battle sample, 8,075 binding writes replace 39,128 requested writes (31,053
reuses, about 79%). 680 palette requests require 11 uploads and 669 reuses,
avoiding 2,989,312 bytes of repeat GPU upload. These are mechanism counts,
not FPS percentages; Vulkan already had content-based palette reuse.

Harness failures remain in evidence: the initial SSH-launched background build
ended with its SSH session and was restarted as an owned scheduled task; an
instanced test fixture omitted its GPU-skinning flag and was corrected; an
elevated Vulkan launcher ignored the explicit-layer path, so that unvalidated
run was rejected and repeated at normal interactive integrity. No Windows
security protection was changed. The initial timing launcher also lacked its
direct Python helper and stopped before the game started; dependency preflight
was added and the fresh measure4 series is used. Test-only capacity cases were added after the
production build; their target was rebuilt and both APIs passed.

## Matched optimized cost

EVGA RTX 2060 SUPER GPU1, native 1920x1080 Medium, fixed night/overcast,
RelWithDebInfo, validation off. Twelve fresh primary runs use wet AB and battle
ABBA per API, with 140 steady frames each (wet 40-179, battle 80-219).
Screenshots occur after the measured interval. Four separate diagnostics retain
CPU phases and mechanism counts. Paired settings match; API errors and thermal
flags are exclusion conditions. These are Phlosion comparisons, not retail tests.

| API / scene | GPU mean ms | CPU frame mean ms | CPU p95 ms |
| --- | ---: | ---: | ---: |
| d3d12 / wet | 15.795 -> 15.794 | 15.658 -> 15.639 | 16.633 -> 16.619 |
| d3d12 / battle | 17.029 -> 16.966 | 18.887 -> 18.706 | 30.475 -> 30.162 |
| vulkan / wet | 18.267 -> 18.290 | 18.120 -> 18.143 | 23.023 -> 19.633 |
| vulkan / battle | 17.920 -> 18.106 | 18.643 -> 18.040 | 29.233 -> 24.352 |

The directly measured benefit is less recording/preparation work, not a
major GPU speedup. D3D12 diagnostic native recording falls from 1.753 to
0.593 ms; Vulkan falls from 0.526 to 0.402 ms. These sums include both recording
lanes and must not be added to or subtracted from whole-frame time.

Matched battle CPU means improve modestly (D3D12 18.887 -> 18.706 ms;
Vulkan 18.643 -> 18.040 ms). Wet-field whole-frame means are essentially flat.
Vulkan battle GPU mean increases 17.920 -> 18.106 ms in this set; that result is
retained, not called an improvement. The separate Vulkan actor CPU diagnostic
falls 5.159 -> 2.019 ms, but one diagnostic pair does not establish a stable
whole-frame saving of that size. D3D12 actor scope is effectively flat.

The wet D3D12 pair is below 16.67 ms in this session for both baseline and
candidate; that is not a new gain caused by this patch. Battle CPU tails,
D3D12 battle GPU time and Vulkan remain above the overall acceptance target.
All twelve primary runs have matching settings, no API errors or thermal flags.
Primary image maxima are 1-6/255; rain state is identical. This is not retail
performance parity or sustained 60 FPS acceptance.

The diagnostic counters add logging only when profiling is enabled; primary
timing still includes that diagnostic overhead. Ordinary gameplay does not emit
these records. Detailed CPU spans remain bounded without a marker per draw.

## Remaining category work

Shared preparation/submission remains 90-95% estimated technique coverage, not
a measured completeness percentage. The next concrete boundary is preparing
native actor view/material plans in the admitted view-family stage before draw
consumption. Then complete owned deformation updates/upload dependencies and
the remaining material-stage/producer job graph. Do not label a smaller verified
subset as 100% of this category. C7/C8, Linux qualification and sustained
16.67 ms whole-frame acceptance are separate open gates.

Evidence: artifacts/lighting-foundation/actor-submission-20261030. The game
roadmap records the final strict dependency-lock and byte-identical payload proof.
