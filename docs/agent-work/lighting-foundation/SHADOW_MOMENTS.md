# Captured local-shadow filtering

2026-10-18. This isolates the next measured component after the live finite-source
light response. `world_shadow_moments.glsl` is an independent, engine-owned
implementation of the observed depth-to-distance-moment filtering. This document
records its isolated numerical qualification. The subsequent live integration,
station selection and remaining projection/cache differences are documented in
[LIVE_SHADOW_MOMENTS.md](LIVE_SHADOW_MOMENTS.md).

## Captured contract

The private retail 2.31, RT-disabled capture records 45 local caster draws followed
by compute events 6768 and 6770, using PSOs 20669 and 20670. The draws use a
1024-square D32 depth target, cleared to one, LESS_EQUAL testing and slope-scaled
depth bias. Six geometry/material pipeline variants include opaque and alpha-mask
coverage. These are distinct from directional/cascade shadow passes.

The first compute pass reconstructs normalized radial distance from perspective
depth: `sqrt(1 + ray.x² + ray.y²) / (a + b * depth)`. Ray coordinates are an affine
function of the unbounded sample position; the depth fetch is clamped to the
texture edge. It takes the nearer of this distance and the first component of
the cached array input. An equal-weight horizontal filter stores the mean distance
and mean squared distance. The second pass averages those two components vertically
into one array layer. Both intermediate and final targets are RG16_UNORM, so the
first pass's quantization is part of the calculation. This is a separable box
filter, not a Gaussian or source-size penumbra calculation.

The shader's shared-memory bounds support odd tap counts 1–15. The engine core
requires a map of at least 2x2, valid perspective reconstruction coefficients and
an odd tap count in that range. Resource adapters own integer addressing, layer
selection and storage. The private adapter retains the original normalized-depth
address round trip. The public fixture uses direct integer texels: a small-map
fixture exposed a compiler-dependent off-by-one when round-tripping integer
positions through normalized coordinates. The failed initial fixture log is
retained. This distinction is material: the float64 oracle with direct addressing
differs from the captured horizontal output at 5,090 channels by more than one
UNORM unit, with a maximum 966-unit difference at discontinuities. The private
GPU adapter reproduces the captured addressing and eliminates those differences.
Live integration must qualify addressing across APIs; the native scalar fixtures
alone do not prove identical captured-texture addressing.

The already recovered receiver function uses the first two moments, a 0.00001
variance floor, 0.04 light-bleed reduction and per-light opacity. Receiver distance
is normalized radial distance from the shadow projection origin. The finite
one-sided source branch shifts that origin behind the source and adjusts its
normalizing range. A moment texture with ordinary nonlinear clip depth would
therefore be incompatible, even if its shadows looked plausible.

## Verification

Public tests live in `tests/shadow_moments`; `reference.py` produces an independent
float64 oracle. Thirty-two cases cover tap counts 1/3/7/15, flat/stepped/sloped/thin
blockers, cached occlusion, empty depth, map borders, intermediate quantization and
receiver visibility. The existing finite-light fixture and the new fixture share
only their native compute execution helper. Their expected values and checks are
separate. Run:

```
cmake --build build-presentation --config RelWithDebInfo --target PhlosionShadowMomentTests PhlosionFiniteLightTests --parallel 4
ctest --test-dir build-presentation -C RelWithDebInfo -R "^Phlosion(FiniteLight|ShadowMoments)-" --output-on-failure -j 1
```

Native moment cases pass on OpenGL, Vulkan and D3D12. Maximum absolute reference
error is 5.2663e-7 on GL/Vulkan and 5.1918e-7 on D3D12, against a 4e-5 limit.
The original finite-response cases also pass after sharing the runner.

Private captured-input verification is in
`D:/Projects/Research/CyberpunkRainResearch/tools/shadow_matched`; all bytecode,
disassembly, buffers and replay files stay in that research depot. The owned
replay observes resource states in queue submission order, saves inputs, runs an
independent shader against those same inputs, restores outputs, and then executes
the original pass. Original export files and `resources.bin` remain read-only.
The accepted run is `evidence/shadow-matched/runs/port-seeded-02/report.json`.
It uses a five-tap filter in both directions on layer 2 of a ten-layer,
1024-square array. Horizontal, vertical and chained horizontal-then-vertical
outputs each match **all 2,097,152 stored components bit for bit**: zero maximum
integer error and zero aggregate error. The allowed gate was one UNORM unit; it
was not relaxed. Each tested output is first overwritten with zero and read back;
the independent shaders regenerate every nonzero component. The other nine
layers remain exact. This prevents an unchanged prepopulated output from passing.

The input includes 39,394 populated dynamic-depth pixels and 907,456 cached
occluded pixels. Dynamic depth wins the nearest-distance merge at 35,890 pixels;
the cache wins at 1,012,686. The final map has 257,577 pixels above the receiver's
variance floor. This is substantial blocker/edge coverage, not an all-lit map.

The original shadow array is restored bit-exactly before lighting resumes, and
all 122 original export source files retain their hashes. Later composition has
270 changed half-float channels, maximum absolute difference 0.000244140625 and
two half-float ULPs, within the previously independently measured retail replay
envelope. Do not call the complete frame bit-identical. The original capture and
resource payload are read-only. The initial missing-readback failure and earlier
unseeded comparison remain retained alongside the accepted run.

The qualified engine-core SHA-256 is
`0f00aaf8c604fdb57f2e6b88c2815074d73deec9a41acdbe714a381d8bb26bac`.
Engine summaries/logs are under `artifacts/lighting-foundation/shadow-moments`.

To reproduce in the private depot, run its `tools/shadow_matched/inspect.py`, then
`tools/shadow_matched/prepare.py`; configure/build `evidence/shadow-matched/working/replay`
with CMake into `evidence/shadow-matched/working/build` (Release). Run
`tools/shadow_matched/run.py <fresh-case-name>` for the seeded GPU comparison and
`tools/shadow_matched/analyze.py <case-name>` for the independent float64 coverage
audit. `run.py <fresh-case-name> --baseline` performs readback-only qualification.
Fresh case directories are mandatory; failed evidence is never overwritten.

## Scope and live integration

This numerical comparison covers the first frame's updated shadow layer. The
four-frame capture starts with the cached input already populated; it does not
demonstrate cache creation, invalidation, allocation policy or every source shape.
Rasterizing the original captured geometry is an input oracle, not an independent
port of every caster deformation/material shader. Filter agreement does not
establish whole-shadow-system or whole-engine parity.

The live integration now supplies engine-owned moment targets and two ordered
GPU passes on all three APIs, radial receiver coordinates, border isolation and
an explicit per-light selection with a PCF comparison path. Its acceptance and
cost are recorded separately in LIVE_SHADOW_MOMENTS.md. No new user capture was
needed for integration; a cache-update study would require additional evidence
if exact cache policy becomes the target.
