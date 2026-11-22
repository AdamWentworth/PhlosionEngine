# Joined deformation preparation and shared previous palettes

2026-10-30. Baseline engine 36b7f8c, game 2b53ea3, ModelSupport c7f972e.
Implementation, optimized builds, CPU/GPU checks and image processing ran on
gpu-worker Windows; controller owns review, integration and commits.

## Behavior

Normal D3D12/Vulkan preparation snapshots admitted deformation inputs in ordinary
RAM, removes duplicate contents, reserves disjoint fenced arena ranges on the
owner thread, joins two copy jobs, flushes noncoherent memory on the owner and
publishes ready entries only after success. A single pending palette copies on
the owner. Every copying lane drains write-combined stores with a host store
fence before publishing completion; an owner-thread fence cannot substitute
for draining another core. This also applies to the single-copy path. Allocation/publication capacity is checked first; failures preserve
prior ready entries and roll back the backend arena cursor. Inputs cannot be
mutated under an asynchronous copy because pending bytes are owned.

Native model preparation joins CPU callbacks before admitting GPU work. Each
collected object retains its renderer identity. Prewarm-only work does not
allocate deformation storage. The glTF family admits only selected visible
palettes; vegetation admits species used by planned views. Committed previous
poses are prepared independently of pending current samples. Borrowed inputs
are consumed synchronously by the owned snapshot step.

Both backends use the shared frame palette storage for current and previous
poses. Motion packets keep per-object transform/identity/validity metadata and
reference previous palettes instead of repeating their matrices per draw.
D3D12 ordinary and cached draws now bind the frame base plus a float4 offset;
instance and previous-pose addressing use that same base. The game's D3D12
rain vertex adapter follows this addressing change. Vulkan already had a
frame-wide transient buffer. Existing GPU buffer capacities are unchanged.

No light, rain, foliage-wind, material equation, sample budget, preset or asset
was reduced. Of 61 shader/material payloads, 58
are unchanged; the three intentional vertex payload changes carry shared
palette addressing. This is the normal path, not a selectable graphics feature.
Legacy backends keep their immediate compatibility path.

A representative dry frame services 668 palette requests with zero immediate
uploads, while preparation uploads 12 distinct palettes / 11,904 bytes in three
joins. Wet frames retain one immediate 8,320-byte rain parameter upload.
These counters describe where copies occur; they do not mean all uploads or
all preparation costs disappeared. Previous-palette lookups increase the request
count without adding geometry draws.

## Source correspondence and remaining differences

The read-only source reference is renderSkinManager.cxx, SHA-256
2651cb46920e15e2dc39c3b163743dbcd8a4172b3f8ce9ae2206d68cbb5b7b54.

| Source contract | Phlosion implementation / evidence | Status |
| --- | --- | --- |
| Snapshot producer data before consuming a flush | Owned pending bytes; producer-mutation tests | Qualified Windows |
| Reserve storage, join disjoint CPU copies, then publish ready GPU ranges (Flush, around lines 335-389) | WorldFramePaletteCache::prepare and native owner adapters; two-lane joined work, flush/error/retry tests | Qualified Windows |
| Share current/previous deformation storage between consumers (Pin, around line 585) | Shared frame offsets in native current draws and motion packets; paired/unpaired/instanced native velocity tests | Frame-local adaptation qualified |
| Registered versioned IDs and active update list (Register/Release/Allocate) | Content-checked frame cache; no persistent versioned deformation registry | Missing |
| Four-entry current/previous lifetime, unchanged-pose reuse, keepalive, deferred release and allocator-pressure retirement | Existing frame fences prevent overwrite, but unchanged data is reuploaded each frame | Missing persistent policy |
| Complete producer dependency graph | Joined native battle, glTF and foliage preparation; standalone native previews/legacy callers prepare local batches | Incomplete global graph |
| Full material/stage preparation ownership | Existing admitted native plans and glTF stages | Broader backend/stage mapping remains |

This pass closes the joined frame-upload boundary, not the persistent registered
update manager. Do not treat a similar allocation size or the same number of
copy workers as proof of the full source system. Linux qualification, remaining
C7/C8 work and the sustained 16.67 ms whole-frame target are separate open gates.
The supplied source snapshot is not authenticated as current retail; these
independent tests and Phlosion baseline comparisons cannot establish 100%
retail behavior or performance parity. The matrix's 90-95% remains an estimated
technique-coverage range, not a measured completeness score.

## Verification

Fifteen selected CPU test groups pass, plus the private foliage arithmetic
check (28,200 comparisons, maximum coordinate error 1.19209e-7 m). The model
tests include the upload-owner contract. Tests cover producer ownership,
duplicate coalescing, capacity-before-reservation, copy completion before
flush/publication, reserve/flush failures and retry, previous-pose packet
validation, committed history lifetime, prewarm exclusion and renderer ownership.

The corrected candidate was freshly qualified. Existing fixed-frame baseline
captures were reused only after proving the staged baseline payload manifests
identical; reused-baselines.json identifies every imported capture. All final
timing baseline/candidate runs are fresh.

Thirteen validation-enabled native checks pass across D3D12/Vulkan, including
prepared paired/unpaired palettes, cached/ordinary/instanced drawing, multiple
instance offsets, motion, resources, recording and retirement. Eighteen paired
scenes cover runtime/editor, dry/wet/orbit/battle and Low/Medium/High/Ultra.
Two additional comparisons preserve all four afterimages. Runtime allocation,
rain/wetness and actor-state contracts pass.

- d3d12: 0/9 exact pairs, 7 above the strict 1/255 threshold; maximum channel difference 7/255.
- vulkan: 6/9 exact pairs, 0 above the strict 1/255 threshold; maximum channel difference 1/255.

These are bounded visual-review results, not universal pixel identity.
visual-review.json records acceptance and limitations. Small D3D12 residuals
remain unresolved and are not asserted to be entirely baseline noise.

The first candidate was also reviewed for CPU memory ordering. The final
candidate adds a store fence on every copying thread before it signals the
join. [D3D12 Map documentation](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12resource-map)
identifies weaker ordering for write-combined mappings; Intel's
[SFENCE instruction reference](https://cdrdv2-public.intel.com/782156/325383-sdm-vol-2abcd.pdf)
provides the required store ordering. Optimized builds, CPU/native tests, scene
pairs and timings were repeated for these corrected bytes under fenced/.
The earlier candidate and all its evidence remain available in the parent
folder, but do not stand in for final qualification.

An earlier late-frame timing screenshot had a localized rain-streak difference
of 63/255 (35 pixels over 8/255). A validation-enabled control subsequently
showed the same peak in the baseline too, with that matched pair differing by
only 2/255. The settings differ across those controls; this does not establish
same-setting determinism or prove the ordering gap caused the rain residual.
Final timing-image differences are recorded separately for the fenced build.

The first D3D12 editor comparisons were invalid: the inherited launcher selected
the new fragment profile but an old rain vertex profile. The launcher now
remaps and checks all five shader sections against the selected binary variant.
The two affected editor pairs were rerun; both original failures and corrected
evidence are preserved. The correction changes the qualification launcher,
not the tested scene. A later nested evidence folder also exposed an editor-base
path error; both affected candidate editor runs were invalidated and rerun with
the original portable project root and fully remapped profiles. Initial compile/test-harness failures and their fixes
are also retained in the evidence.

## Matched optimized cost

EVGA RTX 2060 SUPER GPU1, native 1920x1080 Medium, night/overcast,
RelWithDebInfo, validation off. Wet AB and battle ABBA per API give twelve
primary runs with 140 steady frames each; four separate diagnostic runs
retain phase/copy/recording data. Screenshots occur after timing intervals.
All paired settings, thermal flags and API errors are checked.

| API / scene | GPU mean ms | CPU frame mean ms | CPU p95 ms |
| --- | ---: | ---: | ---: |
| d3d12 / wet | 15.928 -> 15.803 | 15.801 -> 15.649 | 16.952 -> 16.610 |
| d3d12 / battle | 17.157 -> 17.088 | 19.367 -> 19.593 | 30.517 -> 30.685 |
| vulkan / wet | 18.426 -> 18.323 | 18.263 -> 18.164 | 19.158 -> 19.482 |
| vulkan / battle | 18.161 -> 18.115 | 18.073 -> 18.038 | 19.344 -> 19.340 |

Use whole-frame results: preparation moved earlier, so an actors-only reduction
would overstate the gain. The final battle active-CPU mean increased by about
0.70 ms on D3D12 and 0.67 ms on Vulkan; GPU means improved only about 0.07 ms
and 0.05 ms respectively. This is a qualified architecture step with additional
CPU cost, not a demonstrated overall performance win. Combining producer uploads
and replacing repeated content checks with registered pose ownership remain
necessary follow-up work. Individual AB wet-field pairs are not enough to claim
a stable speedup. The detailed distribution, diagnostic phases and accepted
capture differences are retained in analysis-matched.json and
recording-diagnosis.json. These timings compare two Phlosion builds, not retail.

Evidence: artifacts/lighting-foundation/deformation-preparation-20261030.
The game roadmap records final strict dependency and payload identity checks.
