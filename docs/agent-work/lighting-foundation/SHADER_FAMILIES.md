# Independent transparent shader families — 2026-11-03

The normal Windows renderers now give falling rain its own vertex and fragment
programs. A rain edit no longer changes the ordinary character/surface programs
or their cache keys. This is shader organization and iteration work; no rendering
parity category is rescored, and the rain/lighting equations and quality budgets
are retained.

## Ownership and supported scope

WorldMaterialProfile version 3 adds explicit transparent families: a unique ID,
nonoverlapping material modes, HLSL pixel/vertex fragments and three SPIR-V
programs (direct vertex, standard fragment, dual-source fragment). Version 2
base profiles remain readable. Version 2 with a family declaration is rejected;
old engines reject version 3 instead of silently ignoring required programs.

D3D12 selects family source and matching DXC/FXC vertex stages in the existing
constant-material factory, including warmup. The existing faster FXC policy for
falling rain remains. Vulkan owns separate family shader modules and display/HDR
blend/depth pipelines; rain's existing constant-mode warmup selects those modules.
Module/library identities and teardown cover the family programs. Missing
programs, overlapping ownership, reserved engine modes and non-color/opaque
warmup requests are errors.

Families are color-only direct/instanced transparent consumers. An active family
draw in a Vulkan scene batch causes its existing direct/instanced replay before
any draw commands are emitted. An unused entry in a material table does not
disable indirect submission. Opaque surface/shadow passes and engine composition
passes retain the base profile; they cannot silently substitute it for a family.
This is not a general opaque-material-family or indirect-family shader ABI.

PokemonCyberpunk owns falling-rain modes 241–244 and their composition hook.
Its splash 245 and material-specific Hybrid impact 240 remain in the base
profile, with the same surface-specific splashes and real-puddle rings. Shared
display helpers belong to engine material fragments. ModelSupport's character
shader sources and revision are unchanged. The shelved OpenGL profile retains
its previous monolithic composition and is not a new qualification target.

IRenderBackend embeds the changed profile, so editor ABI57 and model provider
ABI29 require matching rebuilt plugins. The normal game manifest selects the
family; there is no gameplay opt-in or new quality switch.

## Incremental build evidence

Same gpu-worker Windows optimized build, compiler and source restoration procedure
as SHADER_DEPENDENCIES.md. Each source probe restores its original bytes.

| Change | Previous organization | Independent family | Programs rebuilt now |
| --- | ---: | ---: | --- |
| Rain fragment | 38.522 s | 7.894 s | Two rain fragments |
| Rain motion | 7.350 s | 7.033 s | One rain vertex |
| HLSL rain only | 6.748 s | 7.165 s | No SPIR-V |
| Unchanged build | 0.490 s | 0.546 s | None |

Every base SPIR-V/HLSL program hash remains unchanged across each rain-only
edit. The separate controlled constant-edit fixture compiles the former six
fragment consumers in 32.997 s versus 1.999 s for the two family fragments.
Those figures exclude CMake regeneration and are not startup or frame times.

A separate private fixture changes one real rain-light coefficient identically
in the before/after programs. Shipping source is untouched. Existing application
and NVIDIA caches are retained; each changed program is new to those caches.

| Controlled rain edit | Before | Independent family |
| --- | ---: | ---: |
| Vulkan renderer preparation | 129.675 s | 10.823 s |
| Vulkan first completed frame | 158.143 s | 39.203 s |
| D3D12 renderer preparation | 35.521 s | 2.260 s |
| D3D12 first completed frame | 65.456 s | 31.387 s |

These validation-enabled iteration measurements are not factory-cold driver or
first-install measurements. Changing shared engine lighting still correctly
invalidates its consumers. Remaining material families are still combined.

Matched optimized heavy-rain timing uses the EVGA RTX 2060 SUPER, 1920x1080,
Medium, 240 frames and 119 measured frames after warmup, with validation disabled.

| Backend | GPU median before / after | GPU mean before / after |
| --- | ---: | ---: |
| D3D12 | 15.960 / 15.632 ms | 15.747 / 15.686 ms |
| Vulkan | 15.833 / 15.843 ms | 15.825 / 15.850 ms |

No invalid/dropped frames or active thermal flags were present in these runs.
Treat the small frame-time differences as preserved performance, not a new FPS
gain. The large improvement is preparation following an isolated rain edit.

## Verification

Profile contracts cover mode ownership, missing stages, path containment,
version compatibility, source independence, invalid pass assignments and warmup.
The real glslang build fixture verifies exactly three family programs. Engine
shader/cache/library/composition checks and rebuilt game CPU contracts pass.
The game exploration contract now expects ABI57; its former ABI56 assertion was
updated after the required rebuild exposed it.

Optimized runtime/editor checks cover Low/Medium/High/Ultra, heavy rain, camera
motion, battle and station/reference/station transitions on D3D12 and Vulkan.
Strict prepared-program checks cover both D3D12 hosts with empty writable caches.
The new scene-shaders-v1/2026-11-03-rain-family publication contains 69 programs.
Native lighting consumer fixtures retain their independent numerical/image
oracles on both APIs, including specialized characters and foliage.

Matched 1920x1080 heavy-rain image mean absolute channel differences are 0.03323
(D3D12) and 0.01168 (Vulkan) on a 0–255 scale; maxima are 8 and 3. These are
same-API before/after comparisons, not cross-API or retail-image parity scores.
API validation reports no errors in accepted host checks. The two early HLSL
qualification failures exposed missing shared display/helper dependencies;
the final transparent declarations include them explicitly. Rejected attempts
remain in the evidence.

Evidence belongs under
artifacts/lighting-foundation/shader-families-20261103/evidence.zip.
Raw expanded recipes, private content, compiler binaries and task deployments
stay outside Git. The pre-existing semantic-boundary false positives documented
in SHADER_DEPENDENCIES.md are unchanged.

## Follow-up

Keep this boundary for future material families instead of rebuilding all
programs from one dispatcher. Separately, asset loading and battle prewarming
still account for roughly 24 seconds in the prior normal-start trace. Audit
immutable asset sharing, move payload preparation and upload batching next,
without deferring a hitch into the first battle frame.
