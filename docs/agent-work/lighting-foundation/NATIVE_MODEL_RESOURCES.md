# Scene-owned native model resources

Date: 2026-10-30. Baseline engine 6675100, game 0487f1e, ModelSupport da6a969.
Implementation, optimized builds, CPU/GPU checks and processing ran on gpu-worker
Windows. controller owns review, integration and commits.

## Trace and connected change

This continues VIEW_RECORDING_BATCHES.md. The existing reference anchor is
renderMaterial.cxx / PrepareRenderStagePassesRange: cached immutable prepared
material-stage inputs have explicit ownership and validity. This independent
implementation extends Phlosion's existing retained glTF/native backend resource
contract to the native model producer. It does not reproduce the complete source
scheduler or establish authenticated retail performance.

The D3D12 diagnostic tail of 840 native submissions measured 0.0567 ms mean per call:
0.0527 ms drawing/submission (93%) and 0.00246 ms scratch preparation (4%).
These are instrumented call timings, not whole-frame timings. Deferred host and
native logs flush independently, so they cannot be joined by text order.
The evidence preserves the initial unusable zero-attribution parser and
the corrected call-level summary.

Model provider ABI23 adds the caller-owned resource scope to the frame. Normal
PokemonCyberpunk battles supply that scope; legacy unmanaged previews remain
valid. Both direct and collected model providers admit it and reject failed
allocation status before returning success. Cache entries hold only immutable
native geometry/material handles. The backend validates owner, scope and
generation; camera, live lighting, model/tint, material scalars, palettes and
motion are still supplied on each draw. Same-address palette mutation remains
covered. Multi-instance batches, anonymous/dynamic geometry and frame-snapshot
textures retain ordinary submission. Draw and transparency ordering is unchanged.

CPU geometry/material lookup caches each cap at 1024 entries. They own identity
strings, clear when renderer/scope changes and replace invalid handles. The
existing material cache now permits replacing an existing entry at capacity.
No live native object is evicted by these CPU tables. Existing backend admission
and fenced physical retirement govern lifetime and capacity exhaustion.

## Accounting correction found during qualification

Native actors previously used unmanaged GPU caches. Their newly managed payload
exceeded the old environment-only battle scope limit in the initial run. The
full-scene qualification stopped at battle activation; the failed payload,
logs and captures remain under initial-budget-* and candidate-v8.

Battle scenes with native cast now reserve a separate 256 MiB actor allowance
alongside their existing environment/foliage allowance. This is a bounded
admission limit, not an upfront allocation or a change in visual quality.
The measured newly accounted native payload is about 112.4 MiB for this cast.
The final runs have zero allocation failures; the runtime battle releases all
managed scene scopes/resources. Provider-level tests cover failure reporting and
scope closure through exceptions. This avoids accepting a partial prewarm and
reporting an unrelated glTF failure later.

## Verification

Sixteen CPU groups/checks pass: eight engine, one ModelSupport, six game and the
private foliage check. Ten native GPU checks pass with D3D12/Vulkan validation:
glTF preparation, same-address motion palettes, native resource reuse,
view recording and scope retirement on each API. Native reuse/retirement images
match their ordinary references exactly. The first native counter test treated
cumulative statistics as per-frame values; only that assertion was corrected,
and the initial test failure is retained.

The first qualification covers twelve runtime/editor scene pairs: wet/dry/orbit/
battle runtime and wet/battle editor on both APIs. Ten baseline scenes reuse the
previous qualified candidate after all 124 payload files were verified identical;
their original manifests/reuse receipts are preserved. Two baseline scenes and
all initial candidate scenes ran here. Maximum channel difference was 2/255,
with only four dry-scene pixels over 1/255. All four battle pairs were byte-exact.

Final review removed an unnecessary full WorldTextureData copy in the new draw
path and applies single-instance overrides once before either submission path.
The final source was rebuilt and rechecked: sixteen CPU groups/checks, ten native
checks, four game/editor battle pairs and two native-afterimage pairs. All four
final battle pairs and both final four-ghost captures are byte-exact. The earlier
full-scene evidence is first-pass-qualification.json; final affected-case evidence
is qualification.json and trail.json. Twelve fresh final timings and four final
diagnostics use measure6/diagnose3; first-pass timing evidence is kept separately.
All 61 shader/material payload files
are unchanged. No rain, lighting equation, quality tier or authored detail was cut.

Steady battle frames reuse the same 14 native geometry/material bindings for
154 additional prepared draws per frame on both APIs, with zero stale draws.
Required recording packets and view batches are unchanged.

## Matched optimized timing

EVGA GPU1, native 1920x1080, Medium, night/overcast, RelWithDebInfo. Twelve fresh
primary runs: battle ABBA and a wet-field AB control per API, 140 steady frames
each. Primary runs use totals-only profiling with detailed battle tracing off;
four separate diagnostic runs retain phase/host tracing. Readbacks occur after
the measured interval. Accepted groups have no thermal flags/API errors and
matched settings. These are Phlosion measurements, not retail-game comparisons.

| API / scene | GPU mean (ms) | CPU frame mean (ms) | CPU p95 (ms) |
| --- | ---: | ---: | ---: |
| d3d12 / wet | 16.655 -> 16.590 | 16.744 -> 16.616 | 26.228 -> 26.697 |
| d3d12 / battle | 17.160 -> 17.083 | 19.183 -> 19.208 | 30.422 -> 31.187 |
| vulkan / wet | 18.309 -> 18.379 | 18.152 -> 18.231 | 19.213 -> 23.411 |
| vulkan / battle | 17.775 -> 18.115 | 19.332 -> 18.014 | 29.656 -> 19.412 |

Separate instrumented diagnostics (not additive to nested whole-frame scopes):
- d3d12: actor CPU scope 4.920 -> 4.779 ms.
- vulkan: actor CPU scope 5.374 -> 4.778 ms.

The final primary battle runs show D3D12 essentially unchanged: CPU frame mean
19.183 -> 19.208 ms, GPU 17.160 -> 17.083 ms. Vulkan CPU frame mean falls
19.332 -> 18.014 ms and p95 falls 29.656 -> 19.412 ms, but GPU mean rises
17.775 -> 18.115 ms. Both Vulkan candidate primaries show that tradeoff; it is
not a demonstrated GPU optimization or sustained 60 FPS result. The wet-field
control moves by less than 0.08 ms GPU on either API.

Separate final diagnostic actor scopes improve by 0.141 ms on D3D12 and
0.595 ms on Vulkan, but total diagnostic CPU frames are effectively unchanged.
The earlier pass had mixed/reversed diagnostic results, retained under
first-pass-*. This limits any claim of a repeatable whole-frame speedup; the
primary Vulkan CPU-tail improvement is a measured result for this test, not
a general performance guarantee. Final primary GPU temperatures peak at 63 C
without thermal-limit flags.

The accepted architectural result is bounded, scene-owned native resource reuse,
with unchanged appearance and correct retirement. It does not solve the remaining
native view/material preparation or D3D12 CPU tails. No quality reduction was used;
sustained 16.67 ms whole-frame acceptance remains open.

## Remaining work

Shared preparation/submission remains **90-95% estimated technique coverage**.
This extends prepared resource ownership to the native model producer. Native
camera-dependent batch preparation, broader material/view scheduling, remaining
glTF/backend submission work and long-frame CPU tails remain. C7/C8, Linux
qualification and sustained 16.67 ms whole-frame acceptance remain open.

Evidence: artifacts/lighting-foundation/actor-preparation-20261030, including
native-attribution.json, baseline-reuse.json, runtime-contracts.json,
qualification.json, trail.json, analysis-matched.json, recording-diagnosis.json,
tested-source.json and final locked-payload-comparison.json.
