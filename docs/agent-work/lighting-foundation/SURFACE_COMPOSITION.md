# Primary surface composition - 2026-10-28

## Architecture

PokemonCyberpunk's ordinary D3D12 and Vulkan field/battle paths now compose
eligible standard opaque/masked materials from the shared evaluated surface
buffer. Captured primary depth is published after scratch passes and sky,
then a fullscreen pass combines the same local-light outputs, directional
lighting, contact visibility, diffuse probes, reflections, emission and haze.
Eligible primary geometry is suppressed during the following scoped submission;
specialized opaque, transparent and late effects retain their required draws.
The separate planar reflection capture remains.

This removes geometry replay for enrolled standard materials, not every draw
or every material. It establishes the integration stage after shared surfaces
and classified local lighting, so later scheduling and material work can target
one surface consumer without re-running the corresponding vertex/coverage work.
A useful architectural dependency need not win every isolated timing comparison.
It still must meet correctness and the eventual frame-time target.

The caller declares the capture's lighting profile and camera. Compact albedo
alpha tag 2 enrolls matching standard PBR; tag 0 remains ordinary fallback, and
tag 1 remains specialized forward shading. Cluster dispatch recognizes both
standard tags. No additional surface plane is allocated. Differing reflection
response, lights, camera or shading environment stay forward. Weather material
preparation remains in capture, and evaluated PBR explicitly marks wetness as
already applied so haze does not darken the material again.

Begin requires HDR, matching completed capture/lighting/view/extent, valid
cluster output and an allowed primary scene state. The scope ends suppression
before late draws. A deliberate depth-layer clear also ends suppression.
Cleared/replaced surfaces, retained-probe selection and new views cannot inherit
primary ownership. Direct, cached and prepared instanced submissions implement
the same contract. Vulkan's indirect scene submission declines the owned scope
so its ordinary fallback can filter individual materials. That does not yet
remove the game's CPU traversal.

Fullscreen composition uses captured mapped normals and reconstructed positions.
Directional receiver bias previously used interpolated geometry normals; this
is an explicit input change requiring low-sun visual qualification. The existing
lighting equations, local shadow response, effect budgets and Hybrid rain
surface-specific splashes/puddle rings are retained.

Editor plugin ABI 40 rejects plugins built against the older surface interface.
Rebuild the game plugin with the matching engine; renderer structure-size checks
alone cannot detect added virtual methods.

## Startup cache

The new Vulkan programs exceeded the existing 256 MiB cache limit (270,837,801
bytes in a native fixture). The bounded limit is now 512 MiB. Integrity,
driver/device/UUID checks and atomic replacement remain. The CPU round-trip test
covers a 257 MiB payload. This avoids silently discarding updated pipelines;
it does not eliminate first-use compilation or claim a steady-frame speedup.

## Qualification

Optimized RelWithDebInfo, shader optimization level 3, gpu-worker Windows,
EVGA RTX 2060 SUPER (LUID 000000000000be8e), native 1920x1080 Medium.
No controller build/GPU execution or local inference was used.

- Five CPU contract suites pass, including ABI compatibility and the expanded
  pipeline-cache round trip.
- Thirty distinct composition cases (15 per API) and 28 existing clustered/
  stored-surface/depth regressions pass with API validation. Composition image
  deltas are at most one byte/channel; submission statistics prove the enrolled
  draws/triangles were removed. Cases cover mask/transparency, forward/mixed
  materials, cached instances, stale/changed camera/extent/light state, HDR,
  depth clear, late geometry, weather and retained-probe switching.
- The independent wet-emission reference caught and fixed double wetness
  dampening in the built-in D3D12 helper. It now matches exactly on both APIs.
  The game-composed helper was already corrected. A rejected fully forward
  wet BRDF oracle mixed material quantization into the comparison; the final
  emission oracle isolates ownership without relaxing the one-byte threshold.
- Twelve runtime/editor field, dry, orbit and battle captures were reviewed;
  eight host checks then verify the final ABI-40 payload. Four matched low-sun
  captures qualify the changed receiver-normal input. No API validation error
  was found. Surface diagnostics show actual standard-PBR enrollment, and
  profiles retain clustered/probe/temporal stages with no fresh SSR source draw.
- Scene images are not byte-identical to geometry replay. Mapped receiver
  normals and reconstructed positions change sparse edge/specular pixels,
  especially bright wet-canopy highlights in battle. The reviewed images retain
  the overall lighting, ground, rain and reflections; the low-sun comparison
  introduces no broad soil banding. No surface format/effect budget was reduced.

Evidence is in engine artifacts/lighting-foundation/surface-compose-20261028,
with raw logs, image pairs, failed attempts, source hashes, payload manifests,
settings and thermal records. Earlier successful regression logs were superseded
by later runs; final accepted regressions have their own directory.

## Matched frame cost

GPU milliseconds below are measured separately from loading. Each wet result
pools 280 frames over two runs in baseline/candidate/candidate/baseline order;
dry results contain 140 frames. Frames 40-179 exclude screenshots/readbacks.
API validation is off for timing; profiling is on. Both variants use identical
resolution, Medium budgets and deterministic weather. Shader caches are isolated
by renderer version; no accepted run has a recorded thermal-throttle flag or
API/device error. The original Vulkan wet group was replaced in full after one
run reported a software thermal flag at 58 C. All four original records remain
visible, excluded with an explicit reason; the replacement uses the same ABBA
order, settings and acceptance rule.

| API / scene | Previous geometry | Surface composition | Change |
| --- | ---: | ---: | ---: |
| D3D12 / wet | 22.994 | 20.845 | -2.149 |
| D3D12 / dry | 15.564 | 12.859 | -2.705 |
| VULKAN / wet | 25.055 | 22.334 | -2.721 |
| VULKAN / dry | 17.858 | 14.851 | -3.007 |

The principal change is removal of the primary standard geometry replay:

| Wet scene | Previous geometry | Remaining geometry | New composition | Depth restore |
| --- | ---: | ---: | ---: | ---: |
| D3D12 | 4.100 | 0.501 | 1.405 | 0.055 |
| VULKAN | 4.537 | 0.550 | 1.004 | 0.039 |

These phases are not a complete frame sum; some profile labels are nested.
The primary surface capture still costs about 4.5 ms, so reducing capture work
and improving scheduling remain useful follow-ups. CPU frame-time tails are
also not solved: candidate wet p95 is 36.85 ms on D3D12 and 26.18 ms on Vulkan.
The GPU means above do not establish locked 60 fps.

The first scene after a shader-source change still incurs lengthy compilation.
A larger Vulkan cache prevents overflow/discard, but shader preparation/loading
remains separate unfinished work. These short deterministic GPU windows are not
a long-session frame-pacing or retail-game performance comparison.

## Remaining boundaries

This removes eligible standard primary GPU replay; it does not remove the CPU
scene traversal, specialized/transparent draws, the planar reflection capture,
or complete the broader feature/channel dispatch and C8 probe/shadow scheduling.
Linux qualification and the 16.67 ms target remain open. This is an independent
Phlosion integration stage consistent with COMPLETE_TECHNIQUE.md; it does not
establish identical retail Cyberpunk 2077 behavior or a new whole-renderer
parity percentage.
