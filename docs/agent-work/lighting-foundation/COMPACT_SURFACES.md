# Compact evaluated surfaces - 2026-10-27

The normal D3D12/Vulkan renderer now uses compact shared surface records. This
finishes the storage change and makes participation measurable; C1-C6 remain
incomplete until the remaining material/producer and platform gates are met.
No rain, ray-count, reflection-resolution, lighting or quality-budget reduction.

## Editor evidence correction - 2026-10-27

A subsequent audit found that this task's four editor cases used the current
editor EXE with the older compact-history plugin/material profile. The claims
below about a complete final editor payload and editor material coverage were
too broad. Its 14 runtime cases, four transitions, native checks and memory/
timing results remain valid. The editor baseline images are mixed-payload
regression references only. Correct paths and hash-verified editor qualification
are recorded in [specialized surface coverage](FORWARD_SURFACE_COVERAGE.md).
That later report also supersedes the participation/model tag terminology below.

## Reference and implementation

Research lighting/reflections/SOURCE_RENDER_CALL_PATH.md records
renderGraphNodes.cpp:394-437 (RGB10A2 albedo/normal and RGBA8 properties) and
include_materialOutputs.fx:254-276 (sqrt albedo and packed material outputs).
Phlosion independently uses that storage organization. Its mapped normal remains
normalized XYZ encoded into RGB10; this is not the source best-fit-normal table.
The two-bit alpha holds Phlosion participation classes, not source stencil IDs.
Specialized source skin/decal classes and their dispatch remain outside this result.

The evaluated material is stored after normal mapping and wet-material response.
Both native and composed character PBR entry points share the packer.
The foliage audit found missing Vulkan records and mismatched generic D3D12
records. The project leaf shader now exports its actual two-sided mapped normal,
roughness and dielectric response on both APIs. Ordinary leaf color/transmission
and wind evaluation remain unchanged; no fabricated generic material is substituted.
Allocation, pipeline formats, packing, reflection reads and diagnostics changed
together. Debug display now samples/converts each format instead of copying
incompatible texture formats. No extra production pass was added.

| Plane | Format | Content |
| --- | --- | --- |
| 0 | RGB10A2 UNORM | sqrt linear albedo; alpha unused |
| 1 | RGB10A2 UNORM | mapped normal; alpha participation class |
| 2 | RGBA8 UNORM | metalness, dielectric F0, occlusion, roughness |
| 3 | R11G11B10F | HDR emission |
| 4 | RGBA16F | signed previousUV-currentUV, previous depth, validity |
| 5 | R32_UINT | exact object identity |

Roughness and AO retain eight bits; they are not stored in two-bit alpha.
A physical texel uses **28 bytes instead of 44**: at native 1920x1080,
**58,060,800 instead of 91,238,400 bytes**, saving **31.64 MiB (36.36%) per view/frame-slot set**.
This excludes depth, alignment, temporal histories, diagnostics and other views.
Backends keep a separate surface set per frame in flight; the figures above count
one set, not the whole renderer allocation.
Native tests count actual resource descriptions, rather than asserting only a
formula. Motion/identity formats and shader rejection thresholds are unchanged.

## Coverage

Classes: 0 empty, 1 opaque PBR, 2 masked PBR, 3 forward-only.
Motion-enrolled materials that do not export a PBR record retain their exact ID,
velocity and geometric normal, but are excluded from generic SSR receiver work.
Transparent/sky branches remain outside opaque material capture. Composed
character layers that call the shared PBR preparation contribute a record;
other specialized layers are not silently labelled PBR.

GPU readback now reports classes and per-object counts. This is diagnostic-only
and disabled in timing. Counts describe these views, not all possible materials:

| Runtime case | Empty | Opaque PBR | Masked PBR | Forward-only |
| --- | ---: | ---: | ---: | ---: |
| d3d12-wet | 531778 | 1514726 | 27096 | 0 |
| d3d12-dry | 531775 | 1514770 | 27055 | 0 |
| d3d12-orbit | 486941 | 1569968 | 16691 | 0 |
| d3d12-battle | 0 | 2042652 | 26359 | 4589 |
| vulkan-wet | 531783 | 1514846 | 26971 | 0 |
| vulkan-dry | 531781 | 1514914 | 26905 | 0 |
| vulkan-orbit | 486944 | 1570023 | 16633 | 0 |
| vulkan-battle | 0 | 2042868 | 26143 | 4589 |

Per-actor coverage is retained in coverage.json. Skinning/wind motion does not
by itself establish correct material coverage or source-equivalent shading.
Remaining specialized layers, deformed foliage normals, source class semantics
and Linux qualification remain explicit gates.

## Verification

gpu-worker Windows, optimized RelWithDebInfo. Native renderer and scene checks use
the EVGA RTX 2060 SUPER, LUID 000000000000be8e. Algebra-only compute fixtures use
their existing first-capable-adapter selection; no timing is inferred from them.

- Five engine and six game CPU suites passed.
- 32 surface cases across both APIs: shader algebra, real MRT formats, mapped
  normals, arbitrary material properties, emission, PBR254, solid/hole masking,
  transparency, unsupported/forward-only draws, abort/reset and odd-size resize.
- Native rigid/skinned motion, contact surface-consumer and legacy shared SSR
  authority checks passed on both APIs; existing expectations were preserved.
- The project-profile native wind fixture passes on both APIs and independently
  checks mapped normal, masked participation, roughness, F0, AO and metalness.
- Four native temporal/reconstruction/reflection runs passed unchanged.
- 18 runtime/editor cases and four field/battle transitions passed. Quality
  variants remain Low off / Medium 2x12 / High 3x12 / Ultra 4x16 for SSR.
- Twelve Medium comparisons to the frozen compact-history build: worst image
  mean 0.01732/255; worst 99th percentile
  0/255; maximum channel difference
  71/255. Bounds remain mean <.5/255,
  p99 <=3/255 and fewer than .1% of channels changing by >8/255.
- No D3D12/Vulkan validation errors; warning IDs: 1328.
  Runtime/editor executable hashes and frozen shader/DLL payloads are verified.

The new UNORM fixture initially assumed exact half-step rounding. Its .457
roughness stored as 116/255, an error of .535 integer steps, on both APIs.
The final check uses .6/255 plus float arithmetic epsilon, consistent with
[Microsoft's float-to-UNORM conversion contract](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#FLOATtoUNORM).
No existing image, motion, contact or reflection tolerance was relaxed.
An initial legacy SSR test omitted its required shared-surface fixture flag;
the corrected harness passed the existing authority assertion.

## Matched warm performance

Native 1080p Medium, same GPU and replay. Each run supplies 140 complete samples
(frames 40-179 of 220), with validation and diagnostic readback off. Wet uses
baseline/candidate/candidate/baseline; dry one pair. Starts <=45 C; thermal
slowdown rejects a run. No cooling, driver, power or clock settings changed.
The benchmark cache had accumulated obsolete Vulkan pipeline variants and exceeded
its 256 MiB save limit. It was backed up and rebuilt for this comparison; the
first new cache was 19,118,689 bytes and saved successfully. No renderer cache
limit, driver or quality setting was changed. Timing uses warmed pipelines.
Baseline is the prior frozen compact-history candidate; full hashes accompany
the evidence. These are worker measurements, not retail Cyberpunk benchmarks.

| API / scene | Before median ms | After median ms | Before / after p95 ms |
| --- | ---: | ---: | ---: |
| d3d12 wet | 32.426 | 32.330 | 33.040 / 32.912 |
| d3d12 dry | 24.712 | 24.560 | 25.226 / 25.075 |
| vulkan wet | 34.314 | 34.088 | 35.409 / 34.922 |
| vulkan dry | 26.588 | 26.356 | 27.003 / 26.775 |

The 16.67 ms goal remains open. Raw profiles include CPU/present statistics,
tails, telemetry and per-pass timings. Memory reduction is established
independently of whether a particular frame-time change exceeds timing noise.

## Next dependency

Finish specialized evaluated-material/normal coverage and connected C1-C6
acceptance, then C7 clustered light/material dispatch and C8 probe/shadow
scheduling. Keep Hybrid material-specific impacts and actual puddle rings.
No overall retail-parity percentage changes merely from compact storage.

Evidence: PROJECTS_ROOT/Phlosion/PhlosionEngine/artifacts/agent-work/compact-surfaces-20261027 on controller and
WORKER_EVIDENCE_ROOT/phlosion-compact-surfaces-20261027 on gpu-worker. Only this task's
delta is returned; the preexisting compact-history work is preserved.
