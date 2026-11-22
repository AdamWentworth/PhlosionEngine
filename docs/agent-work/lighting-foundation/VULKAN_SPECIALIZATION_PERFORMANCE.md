# Vulkan haze and shadow specialization — 2026-10-28

## Scope and implementation

This bounded backend pass follows OPAQUE_REUSE_PERFORMANCE.md. Its wet-scene
mean Vulkan/D3D12 gap was 2.975 ms; haze accounted for 1.271 ms and main directional
shadows for 1.088 ms. This located an implementation difference, not a general
claim that either graphics API must be faster.

Vulkan now creates dedicated material-247 integration and material-248 composition
pipelines using a specialization constant. They execute the existing haze
equations at the existing quarter-size integration extent. Blend/depth behavior
and composition's preserved scene alpha remain unchanged. Ordinary dispatch
remains available for other draw contexts. D3D12 already compiled constant-mode
haze variants.

Shadow shaders now omit contact, screen-reflection and local-reflection branches,
and bind constant-zero planar state. The CPU descriptor producer already clears
these inputs during shadow drawing. This gives the compiler the same information
earlier. It preserves material evaluation, alpha windows/cutouts, vertex
deformation, the caster policy, resolution, depth format, atlas and filtering.
Built-in and composed game shadow shaders receive the same change. There is no
new setting or alternate game path.

No rain, lighting, reflection or quality budget is reduced. Hybrid surface-specific
splashes and puddle rings remain unchanged. These are independent Phlosion
specializations within the existing C7/C8 architecture. They do not add a recovered
lighting mechanism or raise a technique-coverage percentage.

## Verification

All builds and execution used gpu-worker Windows, optimized RelWithDebInfo, EVGA RTX
2060 SUPER (LUID 000000000000be8e, UUID
GPU_1_UUID). controller only orchestrated, reviewed and
integrated. Engine editor/native tests and game runtime/plugins built successfully.

Five CPU CTest entries passed: engine contracts, local haze, static-shadow cache,
game shadow cache and exploration. Native GPU qualification made 24 baseline/
candidate image comparisons in 48 runs with validation enabled: 15 haze cases
and nine shadow cases. All 24 pairs were pixel-identical. Cases include source
off/blocked/blue/both, foreground protection, odd extent, repeated haze calls,
moments/PCF, borders/self/range edges, and masked cached/fresh/refreshed shadows.
Native thresholds were not changed. Final built shadow SPIR-V is byte-identical
to the separately native-qualified payload.

Twelve validation-enabled game runs completed: wet/dry/orbit/battle runtime
and wet/battle editor on both APIs. Expected battle state, motion/surface coverage,
temporal reflection output, clustered stages and placed-probe activity were
checked; no renderer validation errors. Eleven initial scene images match their
accepted references exactly. The D3D12 daylight case was additionally controlled
and repeated as described below. Native image limits remain unchanged.

The first D3D12 daylight image exceeded this pass's additional one-byte
full-scene comparison: versus a fresh frozen baseline, maximum difference was
four bytes at 175 pixels (0.0084% of 1920x1080), mean 0.00737 byte/channel.
The unchanged candidate's repeat matches the fresh baseline within one byte,
and differs from its own initial capture by up to three bytes. HLSL, runtime
inputs, motion and temporal diagnostics match. This demonstrates capture
variation on unchanged code; its underlying cause was not isolated here.

Later frame-185 timing images are also not universally pixel-identical.
Vulkan wet differs by two bytes at one pixel; dry by at most one.
D3D12 wet maximum is nine bytes, with six pixels over four bytes; dry maximum
is 67, with 153 pixels over four bytes (0.0074% of the image), concentrated in
small highlights/edges. Mean errors are 0.00647 and 0.00939 byte/channel.
Full-size images and magnified worst-difference crops were reviewed; no broad
lighting, geometry, shadow-boundary or rain change was visible. Keep these
limits explicit rather than calling all full-game frames exact. D3D12 code
was not changed. Initial comparisons, fresh controls, repeat and reviews remain
in the evidence. Native acceptance was not relaxed.

## Matched warm timing

Baseline engine 393d2471c33bc7d05eec911959fa4d51e2dfb2c4 and game
b2618a25231586fdadf85c4c61b6bd3c3273a265; frozen qualified payload hashes retained.
Final source hashes and staged payload hashes accompany the evidence.
Native 1920x1080 Medium; wet is night/Hybrid heavy rain, dry is noon/clear.
Vulkan wet order is baseline/candidate/candidate/baseline. Each Vulkan version has
280 measured wet frames and 140 dry frames. D3D12 controls have 140 wet and
280 dry frames per version; its dry pair was repeated to check run variation. Frames 40–179 of 220 supply statistics; validation and diagnostic
readbacks are disabled. Profiling remains enabled. Every run starts at or below
45 C. Frame-185 capture completion bounds the thermal check after the measured
interval; startup, compilation and captures are excluded from frame statistics.

All times in milliseconds, baseline -> candidate.

| API / scene | GPU median | GPU mean | GPU p95 | GPU p99 |
| --- | --- | --- | --- | --- |
| vulkan / wet | 26.500 -> 25.971 | 26.584 -> 26.014 | 28.093 -> 27.096 | 28.276 -> 27.318 |
| vulkan / dry | 19.230 -> 18.227 | 19.254 -> 18.336 | 20.289 -> 19.505 | 20.465 -> 19.598 |
| d3d12 / wet | 23.923 -> 23.829 | 23.966 -> 23.852 | 25.434 -> 25.509 | 25.649 -> 25.802 |
| d3d12 / dry | 15.611 -> 15.684 | 15.640 -> 15.934 | 17.501 -> 17.556 | 17.642 -> 17.647 |

Vulkan wet median improves by 0.529 ms (2.0%). Its targeted mean scopes change:

| Scope | Baseline | Candidate |
| --- | --- | --- |
| haze | 3.029 | 2.621 |
| shadow | 1.721 | 1.344 |
| local-shadows | 1.000 | 1.062 |
| geometry | 4.662 | 4.810 |
| post-compose | 1.586 | 1.591 |

The selected GPU reached 66 C. No thermal flags occurred through the completion
marker, and no invalid/dropped profile frames or renderer errors were recorded.
All measured frames retain three probe-relight spans, no static material
recapture and no removed screen-source draw. CPU/present distributions are
retained in final-analysis.json. D3D12 has no intended performance change:
its control differences are reported, not attributed as an optimization.

Current candidate wet median remains 2.14 ms behind the D3D12 control.
The small repeated wet saving is the accepted result; the larger isolated
first-trial saving is not substituted for it.

These are instrumented warm timings, not shipping FPS, cold-start measurements
or a matched retail Cyberpunk 2077 benchmark. Initial isolated haze and
shadow diagnostics are retained separately; final repeated results govern the
reported saving.

## Remaining work

The backend gap is not closed. Retain the shared roadmap: split residual
contact/depth/reconstruction cost into actionable scopes; address repeated
opaque geometry/coverage and the extra planar-reflection pass; continue C8
probe filtering/publication priorities and shadow refresh scheduling.
Preserve complete publication and moving actor/light/weather freshness.
This pass does not solve cold shader preparation, Linux qualification or the
wet-scene 16.67 ms target. No driver, GPU clock/fan/power settings or quality
presets were changed.

## Evidence

Worker: WORKER_EVIDENCE_ROOT/phlosion-vulkan-gap-20261028.
Controller: artifacts/lighting-foundation/vulkan-gap-20261028.
See build-final.json/logs, cpu-checks.json/logs, native-baseline/checks.json,
native-haze/checks.json, native-shadow/checks.json, scenes-final.json,
final-checks.json, final-analysis.json, tested-source.json/.patch, staged hashes,
per-run manifests/profiles/telemetry/images and daylight review/control records.

The offline shader-optimizer diagnostic was unavailable because the installed
glslang binary has no linked optimizer. It changed no product/compiler settings.
Only the measured specialization changes above are retained.
