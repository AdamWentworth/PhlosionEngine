# Shader and pipeline startup
Status: fixed S0.1-S0.6 contract qualified; shared Vulkan pipeline-preparation follow-up, 2026-11-02.
Scope: supported Windows D3D12/Vulkan runtime and editor, ordinary Low/Medium/High/Ultra.
This is a subcategory of loading/resource management; asset streaming/residency and
steady-frame performance retain their own gates.

## Shared Vulkan pipeline preparation, 2026-11-02

Ordinary supported Vulkan devices now use graphics pipeline libraries for the
backend's world/debug/sprite/fullscreen graphics family. Vertex input,
pre-rasterization, fragment shader/depth state and output/blend state compile
separately; identical parts share one joined creation. Unsupported devices or
unrepresented state use the original complete-pipeline path. Quality presets,
shader instructions, rendering math and budgets are unchanged. D3D12 is unchanged.

The cache belongs to one backend and clears before pipeline/render-pass teardown.
Keys include the relevant layout, render pass, shader generation, specialization
bytes and fixed/dynamic state. Fresh shader registrations prevent recycled native
handles from reusing old code. Per-entry locks join concurrent requests and retain
failure results; independent entries compile in parallel. The existing checked
persistent driver cache remains. No new runtime/compiler dependency is introduced.

Driver feedback records valid durations and cache-hit flags separately from wrapper
elapsed time. Trace names identify source call, material, blend, depth, target and
shader files. PHLOSION_SHADER_PREPARATION_TRACE enables diagnostics. The disable-
library and library-LTO environment controls are internal comparisons, not user
rendering modes. Fast linking is the qualified normal path; link-time optimization
was not needed for the measured frame cost.

### Controlled new-program comparison

gpu-worker, EVGA RTX 2060 SUPER, NVIDIA 610.88, Windows, optimized RelWithDebInfo,
1920x1080 Medium heavy rain, validation enabled for both preparation runs.
Each candidate receives a distinct OpModuleProcessed metadata string in the nine
project shader modules. Removing only those instructions yields identical
executable/declaration bytes for every pair. Driver feedback confirms substantial
new compilation; the OS/global driver cache was not purged. This isolates repeated
compilation without changing shader instructions, not a factory-cold device.

| Renderer preparation | Complete pipelines | Shared libraries |
| --- | ---: | ---: |
| Controlled new shader identities | 360.427 s | 129.891 s |

That is 64.0% less renderer-preparation wall time. The original path created 85
complete pipelines. The library path created 90 shared parts and 85 cheap links,
reusing other part requests. General pixel variants accounted for the largest
delays (up to 32.609 s per driver request); specialized rain variants were around
3.5 s. These durations under parallel preparation are not additive process CPU time.

An empty application cache with warm NVIDIA state prepared the original renderer
in 2.651 s. It must not be described as driver-cold. Warm validation runs include
layer overhead; normal no-validation launches are recorded separately.

### Frame cost and compiler comparison

Short paired 240-frame runs (119 measured frames after warmup), no validation,
no active thermal-throttling flags and no invalid/dropped profiler frames:

| GPU median | Complete pipelines | Shared libraries |
| --- | ---: | ---: |
| Heavy rain | 15.746 ms | 15.761 ms |
| Dry | 12.388 ms | 12.450 ms |

Heavy-rain image mean absolute channel difference is 0.00675/255; maximum is 2/255.
Reviewed images preserve rain, surface splashes/rings, foliage, fog, reflections
and lighting. This supports preserved cost, not a universal GPU speed improvement.

DXC was tested on the composed world fragment and surface-data fragment programs.
The SDK executable advertises SPIR-V options but returns 'SPIR-V CodeGen not
available'. The comparison uses Microsoft's verified standalone v1.9.2609.
Pinned SPIRV-Cross v1.4.341.0 converts the same programs to HLSL, with explicit
Vulkan bindings, locations, specialization IDs and push offsets. Reflection and
API validation check the adapters. This is one round-trip candidate, not a verdict
on a future natively authored HLSL Vulkan renderer.

The candidate matches the tested image (maximum channel difference 1/255), but its
largest pixel-stage driver compilation remains about 30 s. With the same library
implementation, it measures 16.210 ms versus 15.765 ms in rain, and 12.868 ms versus
12.535 ms dry. It does not justify replacing the current compiler. Normal builds
retain glslang; trial compilers/generated sources stay outside repositories.
The qualified SDK DXC/FXC policy for D3D12 stays intact.

### Final state-isolation correction

The first library revision passed the ordinary scene comparison but failed
independent transparent indirect consumer case 11 (black output). The old
complete-pipeline path passed the same case. Each library now receives only the
Vulkan state belonging to its subset; in particular, fragment compilation cannot
see another subset's blend state. A CPU contract rejects foreign subset inputs.
All 32 independent numerical image cases pass after this correction. Failed
evidence and the old-path control are retained.

The new-program figure above is remeasured on this corrected revision. Its
heavy-rain image mean absolute channel difference is 0.00656636/255,
maximum 1/255. Final ordinary GPU medians are 15.714 ms wet and
12.544 ms dry (same 240-frame/119-measured procedure; no thermal flags or invalid
frames). Earlier comparison rows and the rejected DXC measurements remain above
as the implementation history; they are not substituted for this final check.

### Verification and scope

New CPU contracts cover sharing, specialization/layout/render-pass identities,
recycled shader handles, concurrent requests, failure joins, cleanup and unsupported
state fallback. Existing shader-preparation, pipeline-cache and engine contracts
pass. All 32 native image cases run without layers; GI lifecycle and the final
prepared consumer also run with synchronization validation. Runtime/editor,
presets, scene transitions and GPU fallback
results are retained in the task receipt. Locked-source integration is checked
before handoff. Source/binary/package identities and rejected runs remain recorded.

Evidence: artifacts/lighting-foundation/vulkan-pipeline-startup-20261102/evidence.zip.
This qualifies the tested Windows raster path; Linux and other GPU performance
remain unmeasured. New programs still incur driver compilation, and separate
roughly 24-second field/battle asset preparation remains. No parity category changes.

References:
[graphics pipeline libraries](https://docs.vulkan.org/samples/latest/samples/extensions/graphics_pipeline_library/README.html),
[creation feedback](https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineCreationFeedback.html),
[DXC SPIR-V](https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/SPIR-V.rst),
[standalone DXC](https://github.com/microsoft/DirectXShaderCompiler/releases/tag/v1.9.2609).


## Development iteration follow-up, 2026-11-02

The prepared-package path already made ordinary launches fast. The repeated
10-15 minute waits were mostly preparation after shader changes: FXC optimization
of large expanded HLSL, plus Vulkan driver compilation of new material programs.
They were not repeated C++ builds or measurements of gameplay frame time.

D3D12 now uses the SDK's DXC, Shader Model 6.0, legacy-compatible HLSL 2016 and
level-3 optimization for general programs. Shadow, surface composition and the
falling-rain modes retain FXC: measured DXC output made those passes slower.
Their vertex stages also use FXC; DXBC and DXIL signatures cannot be linked
within one graphics pipeline. This is an internal compiler policy, not a quality
setting or an optional renderer. Shader math, effect counts and presets are unchanged.

The request identity includes the compiler policy (modern generation 4, legacy 5).
Writable caches hash the actual compiler binaries, including the DXIL validator.
Recipes record the policy; the cooker rejects unknown policies and stale source
identities. Per-request locks avoid collisions between unrelated compilation jobs.
The cooker reuses the persistent developer cache rather than starting another
cache in each output package. Copy dxcompiler.dll and dxil.dll alongside Windows
executables; CMake and the game's preview launcher stage the SDK files. SM6.0 is
checked before D3D12 backend creation. Compiler DLLs are not gameplay assets.

Vulkan's nine composed project programs now run the pinned glslang/SPIRV-Tools
optimizer during the build (-Os). Compiler versions are unchanged; the glslang
host dependency enables its opt feature. Generic engine SPIR-V is unchanged.
This prepares optimized input once, but cannot eliminate the driver's own
GPU-specific compilation.

| Measurement on gpu-worker | Result |
| --- | ---: |
| All 67 final HLSL programs, empty compiler cache | 29.816 s |
| Identical repeat cook | 0.304 s |
| D3D12 development launch, empty application cache, no prepared package: renderer / first frame | 37.410 / 67.936 s |
| D3D12 repeat with prepared package: renderer / first frame | 0.773 / 29.725 s |
| Nine optimized Vulkan project programs, offline build | 12.966 s |
| Vulkan newly changed programs: renderer / first frame | 350.742 / 378.767 s |
| Vulkan repeat: renderer / first frame | 0.931 / 28.239 s |

The previous captured FXC cook took 650.464 s for 66 programs, of which 28
required compilation. It is historical context, not an identical all-cold
workload. The new 67-program figure genuinely starts with an empty application
compiler cache. NVIDIA/OS caches were not cleared. Vulkan still needs roughly
six minutes for these new driver programs; the previous driver-warm baseline
cannot establish a matched driver-cold speedup. About 24 seconds of field/battle
asset preparation remain and are separate from the compiler work.

Matched 1920x1080 Medium heavy-rain checks on the EVGA RTX2060 SUPER, optimized
RelWithDebInfo, 239 measured frames after warmup:

| GPU median | Prior version | Qualified compiler policy |
| --- | ---: | ---: |
| D3D12 | 16.653 ms | 15.925 ms |
| Vulkan | 16.433 ms | 15.710 ms |

These are short paired observations, not a broad hardware or retail benchmark.
Longer trials encountered thermal throttling and are excluded; retained telemetry
shows no thermal flags while the application owned the GPU in the reported runs.
An all-DXC candidate regressed D3D12 to 18.233 ms and was rejected. Per-pass
profiling identified rain, surface composition and shadows before selecting the
paired compiler policy. The first diagnostic attempt to mix DXBC/DXIL stages
failed pipeline validation; final pipelines pair compatible stages.

Final matched-image mean absolute channel differences are 0.01449/255 for D3D12
and 0.00661/255 for Vulkan. Side-by-side images preserve lighting, foliage, rain,
surface splashes and puddle reflections. Independent native oracles and ordinary
host checks are recorded with the evidence; numeric/image equivalence is scoped
to the tested scene and supported Windows APIs. This does not claim retail
engine identity, Linux qualification or zero cost for future changed shaders.

Evidence: artifacts/lighting-foundation/iteration-startup-20261102/evidence.zip.
It retains compiler/cache failures, rejected timing candidates, final source and
payload identities, actual recipes' identity audit, host/native checks and images.
Raw expanded shader recipes, private game assets and compiler DLLs are excluded.

References: [DXC migration](https://github.com/microsoft/DirectXShaderCompiler/wiki/Porting-shaders-from-FXC-to-DXC),
[DXIL stage signatures](https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/DXIL.rst),
and [glslang optimization](https://github.com/KhronosGroup/glslang).

The sections below retain the original S0 qualification and historical timings.

## Fixed acceptance gates
1. Trace prepared shader lookup, cache validity, GPU creation and preparation scheduling
   against the reference, recording explicit API/representation adapters.
2. Measure shader compilation, GPU pipeline creation, asset preparation and first-frame
   readiness independently; distinguish cold application caches from driver/OS caches.
3. Prepare actual required shader variants ahead of gameplay and support portable
   prepared shader artifacts without coupling normal launch to research inputs.
4. Persist compatible device pipelines, reject stale/corrupt inputs safely, use atomic
   writes and share the intended cache across launch directories.
5. Join bounded preparation before consumers run; avoid duplicate preparation and preserve
   normal material/preset/scene changes, useful loading progress and cancellation/lifetime.
6. Qualify cold, repeat, update, malformed/missing/unwritable cache and first-use transitions
   on both APIs in optimized builds. Preserve images/Hybrid impacts and record remaining costs.

Coverage: 6/6 fixed shader/pipeline-startup gates (100% estimated within this boundary).
The broader loading/resource-management category remains unscored.

## Reference audit
Read-only reference root: REFERENCE_SOURCE_ROOT/extracted/source/root/R6.Root/Mainline.
This is the supplied snapshot, not an authenticated retail revision.

- dev/src/common/shaderCache/src/shaderCacheManager.cpp:18-93: resolver opens prepared
  shader storage, exposes lookup and an explicit load completion counter.
- shaderCacheReadonly.cpp:26-77,91-131,271-321,385-408,543: staged asynchronous reads,
  version/magic checks, pending/failed/ready states and joined lifetime.
- shaderCacheBuilder.cpp:241-341: build-time preparation, include identity and persistent
  shader/material/reflection records. staticShaderCache.cpp:146-196,257-280 adds static
  entries and cache validity. Shipping prepared programs is separate from device compilation.
- dev/src/common/renderer/src/renderShaderMap.cpp:252-307: bounded job preparation of
  compute/graphics PSOs from shader, output-format and input-layout descriptions, joined
  before use. The source TODO about moving the join is not treated as implemented overlap.
- dev/src/common/gpuApi/src/dx12/gpuApiDX12PSOCache.cpp:795-872,904-1058,1073-1158:
  persistent pipeline libraries, device/runtime rejection, pending stores, asynchronous
  saving and explicit lifetime. Header/version/root-signature identity and size limits matter.

Phlosion adapters: prepared DXBC records use exact expanded source/entry/target/flags.
Compiler identity is recorded by preparation and isolates writable developer caches; shipping
bytecode stays portable across host compiler installations. Per-state D3D12 cached binaries replace the reference's cloned
pipeline-library container; they preserve shader/root/fixed-state and device/driver
compatibility with driver-rejection fallback. Vulkan retains its native UUID/driver-checked
pipeline cache and build-prepared SPIR-V. Four-worker jobs join owned outputs before
publication. These are implementation choices, not byte-for-byte source identity.

API references:
[Microsoft cached pipeline state](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_cached_pipeline_state)
and [Vulkan pipeline-cache synchronization](https://docs.vulkan.org/refpages/latest/refpages/source/vkCreateGraphicsPipelines.html).
Raw reference code is not copied into the engine or its evidence bundle.

## Delivered mechanisms
- Engine-owned portable HLSL preparation, keyed by expanded source, entry point,
  target and flags. Normal lookup uses the prepared package first, then a writable
  compiler-specific cache, then ordinary compilation on a development miss.
- Writable HLSL records identify the actual loaded d3dcompiler_47 DLL, not the
  executable import thunk. The regression test compiles CPU-only HLSL and checks
  the cache namespace against independently read DLL bytes.
- D3D12 PSO records include root signature, shader bytes, every relevant fixed
  state, device and driver identity. Rejected driver data falls back to creation.
  Vulkan keeps its existing checked native cache, now in the shared user cache.
- Four joined workers prepare Vulkan pipelines and game-declared D3D12 material
  variants. Shader modules, root signatures and output storage outlive all jobs.
  First profile selection precedes backend construction, avoiding an initial
  default-material build followed by a project-material rebuild.
- Main-thread SDL event pumping and preparation titles accompany compiler/pipeline
  waits. Cancellation joins active work before unwinding. Editor ABI49 rejects
  old plugins after the renderer base material-profile layout extension.
- The game supplies its observed 32 material variants and a pinned publication of
  62 optimized shader programs, including High/Ultra compute variants. The normal
  launcher restores/verifies it through the existing asset-package workflow.

## Measured startup
gpu-worker, EVGA RTX2060 SUPER, Windows, 1920x1080, optimized RelWithDebInfo,
normal Medium battle replay. These are single paired startup observations, not
statistical steady-frame benchmarks. Cold means empty application shader/PSO
cache; OS files and NVIDIA driver caches were not purged.

| Time to first completed frame | Before | Prepared package |
| --- | ---: | ---: |
| D3D12, cold application cache | 687.073 s | 33.384 s |
| D3D12, repeat | 29.212 s | 28.577 s |
| Vulkan, cold application cache | 29.947 s | 29.765 s |
| Vulkan, repeat | 28.353 s | 28.137 s |

D3D12 cold HLSL work changes from 65 misses / 664.242 s to zero compilation,
68 prepared lookups / 41.53 ms. The new startup prepares some variants earlier;
request counts are not identical. Repeat D3D12 uses 84/84 cached PSOs; the old
path created 110 including replaced default pipelines. Vulkan creations drop
169 to 97. Vulkan does not expose a cache-hit feedback counter in this trace:
its logged hit=0 is not evidence of a miss. Summed parallel pipeline timings
are CPU job totals, not elapsed critical-path duration.

The complete 180-frame D3D12 test process falls from minutes to 38.73 s; the
orchestration including telemetry takes 40.90 s versus 703.15 s before. Prefer
first-frame figures above when discussing loading. About 6.2 s of field asset
preparation and 17.5-18.0 s of battle preparation remain on repeat launches.
This pass does not claim a steady FPS gain or close the 16.67 ms frame target.

The prepared programs still require offline compilation. Qualification captured
actual programs from the runtime/editor and reused those exact compiler results
in the offline cooker; packaging is not evidence that compilation became free.
The small real-HLSL tests independently exercise cold compilation, unchanged
reuse, source invalidation, stale recipe rejection and unwritable outputs.

## Operational handoff
The default writable cache is LOCALAPPDATA/Phlosion/ShaderCache. Prepared HLSL is
project-local .phlosion/shaders; Vulkan SPIR-V remains build-prepared. Never ship
GPU/driver PSO records as portable assets. Record input/manifest identities, ship
only manifest.json and d3d12 from the cooker output, and keep .compile-cache and
expanded source recipes in the private development task.

For a changed shader/content set:
1. Build optimized runtime/editor and collect the normal scene, battle and all
   supported presets with PHLOSION_SHADER_RECIPE_DIR pointing to a private folder.
2. Run tools/render/PrepareShaderPackage.ps1 with -Recipes, -Cooker and -Output.
3. Publish the selected manifest/program files as a new game asset release,
   review/pin it in config/assets/environment_packages.json and restore it.
4. Qualify empty writable caches with PHLOSION_REQUIRE_PREPARED_SHADERS=1, plus
   normal fallback tests. This is a diagnostic guard, not a rendering opt-in.

PHLOSION_SHADER_CACHE_DIR and PHLOSION_PREPARED_SHADER_DIR isolate qualification
inputs; PHLOSION_SHADER_PREPARATION_TRACE records stage timings. Gameplay selects
the normal renderer using Low/Medium/High/Ultra. Shader edits not covered by a
prepared publication compile normally and create new keys; the prepared file is
never reused for changed source. Old cache generations have no automatic disk
eviction yet. Compiler DLL changes invalidate writable developer entries; prepared
DXBC is portable and is deliberately not tied to the local compiler installation.

Broader streaming/residency, concurrent asset loading, other platforms, changed
content outside the qualified scene/hosts, and retail image/FPS identity remain
separate. Generic editor project-to-project package selection outside an explicit
package override is not a qualified multi-project cache-management system.


## Qualification and remaining loading work
Evidence: artifacts/lighting-foundation/shader-startup-20261101/evidence.zip.
The archive retains unsuccessful attempts, subsequent fixes, tested payload/source
hashes, exact commands, structured results, timings and representative images.
Raw reference sources, expanded recipes and private game assets are excluded.

| Gate | Evidence |
| --- | --- |
| S0.1, preparation and validity trace | source-anchors-initial.json, source-pso-anchors.json and this report's explicit adapters |
| S0.2, independent startup costs | startup-baseline-summary.json, prepared-summary.json and retained per-stage logs |
| S0.3, prepared actual programs | preset-package.json, warmup-coverage.json, package-checks.json, normal-preset runs |
| S0.4, validity and recovery | CPU cache/cooker tests, compiler-identity-audit.json, extra-validation.json |
| S0.5, joined preparation/lifetime | CPU bounded-job/progress/cancellation tests, plugin-checks.json, scene transitions |
| S0.6, both ordinary APIs and preserved content | qualification.json, extra-validation.json, visual-validation.json, final locked-source checks |

Sixteen runtime/editor Game-preview cases cover wet Low, battle Medium, orbit
High and dry Ultra, including cold/repeat runs. Additional checks exercise
corrupt/missing shader and PSO inputs, unwritable cache roots, runtime/editor
developer-cache sharing and normal prepared-package lookup without an override.
The release guard is tested separately: a warm writable developer cache cannot
conceal a missing prepared publication.

An additional station/reference/station Scene-view test found the overhead
orthographic layout camera entering perspective-only temporal/contact/reflection
reconstruction. The game now excludes that authoring camera from those effects;
ordinary perspective Game views retain them. Both APIs qualify the transitions
with no pending scopes/allocation failures. Initial missing-catalog and camera
failures remain in the evidence, as does the initial missing High/Ultra program
check that led to the complete 62-program publication.

Editor ABI49 accepts rebuilt game/model-preview plugins and rejects ABI48.
The CPU suite covers all 38 engine groups, the shader-preparation and Vulkan
cache executables, exploration/battle-sequence/arrival contracts and asset-package
tests. Cached programs are unchanged compiler output; matched normal scene images
have mean absolute byte differences below 0.026/255 in the reported comparisons.
There is no lighting, rain, reflection-budget or resolution cut.

Next loading target: the approximately 24 seconds of repeated field/battle asset
preparation. Audit its duplicate reads/decoding, independent jobs and retained
resource handoff before choosing the fix. Shader cache eviction, generic editor
project-to-project package ownership and broader streaming/residency remain
explicit follow-ups. None is counted as delivered by this shader-startup score.

## Incremental build ownership (2026-11-02)

See [shader build dependencies](SHADER_DEPENDENCIES.md). Engine and project GLSL
rules now consume compiler-emitted includes, and project hooks publish their
final output only once. A Direct3D-only rain edit stops rebuilding all nine
Vulkan material programs (38.946 s to 6.748 s on the qualified worker). This
preserves shader bytes and does not claim reduced new-program driver time.
