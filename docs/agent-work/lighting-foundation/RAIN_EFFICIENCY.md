# Rain GPU efficiency

The falling-rain R1-R8 contract stays qualified; no category percentage is
rescored. This is a Windows Vulkan execution improvement, preserving the current
rain calculations, geometry/density, mask filtering, volume budgets, shelter
resolution and update schedule, material-specific Hybrid impacts and actual-puddle
rings. D3D12's shader files are byte-identical to the prior qualified version.

## Changes

The measured ordinary Vulkan far-rain pass used the general fragment dispatcher.
D3D12 already prepared a constant-material program for the same draw. Vulkan now
accepts a bounded, game-owned material/standard-pipeline plan and specializes
fragment constant ID 3 before rendering. The engine composition helper propagates
the material mode through expanded direct fragments. The actual draw selects the
prepared program using its material, blend/depth index and scene format.

PokemonCyberpunk declares modes 241-244 with its existing premultiplied-additive,
depth-tested state. Eight programs (four modes, display/HDR) are prepared in the
existing joined queue and released with the profile. These eight Vulkan pipelines
use the existing disk cache; cold pipeline preparation is not improved or
benchmarked as a startup target in this pass. No new feature switch,
quality cut or game-specific material ID is embedded in the engine.
Unlisted, indirect, shadow, surface, reflection and other specialized passes
keep their existing selection. Shader arithmetic and temporal reactive output
are retained. See MATERIAL_PROFILES.md for the optional profile contract.

Vulkan's first shelter-map batch uses the compatible depth-clear render pass
instead of loading old depth and then clearing it through an explicit attachment-clear command.
Continuation batches still LOAD, preserving partial static updates. Published
depth/transform ownership and cadence remain unchanged. Native partial-update,
masked/opaque, independent-cascade and atomic-publication checks pass.
No independently significant shelter-map timing gain is claimed.

An exact-zero lighting-rejection trial was tested and discarded: it did not help
Vulkan and increased D3D12 far-rain cost. Its patch and measurements are retained
only in task evidence. The delivered rain source files match the recorded input
bytes; the optimization is pipeline preparation, not a new rain approximation.

## Verification

- Optimized RelWithDebInfo engine, runtime and editor builds on gpu-worker; GPU1 is
  the EVGA RTX 2060 SUPER, LUID 000000000000be8e. controller performs no builds/GPU tests.
- Editor/plugin ABI advances from 52 to 53 because the material profile changes
  the shared backend layout. Both hosts are rebuilt together; GPU programs match
  the measured candidate byte for byte. Final strict-pin checks repeat both hosts
  on both APIs after this version-only update.
- Engine contracts include plan bounds, duplicates, invalid/overflowing inputs,
  manifest loading and empty-profile ownership. Existing engine contracts pass.
- Actual native shelter/volume tests and independent game rain lighting/motion
  fixtures pass on D3D12 and Vulkan, with no API validation errors.
- 26 strict prepared-shader host cases cover runtime/editor, all four tiers,
  dry/wet, orbit/battle, both Outskirt scenes, daylight and repeated rain-off/on.
  Cycle traces verify intensity transitions and zero allocation failures.
- The existing immutable scene-shaders-v1/2026-11-01-falling-rain release remains
  valid. HLSL content did not change; strict hosts report no source compile misses.
  Vulkan SPIR-V is generated from the current engine/game composition at build.
- Both host/preset sheets, weather views and before/after captures are manually
  reviewed. The Vulkan diagnostic comparison has mean absolute difference
  0.00962/255 and maximum difference 2/255 per channel. These are same-scene
  regression measurements, not a retail visual similarity percentage.
- Exact source/payload hashes and final strict dependency-pin/host receipts are
  retained with the evidence. Integration preserves intervening/unrelated edits.

## Matched optimized timing

1920x1080 Medium, same identified worker GPU and scene inputs. Two wet
repeats/revision/API in ABBA order; one dry control per revision/API. Each sample
uses steady frames 121-359 (239 frames). GPU totals have no nested pass timestamps
and validation is disabled; separate diagnostic runs identify pass costs.
Thermal-slowdown attempts are excluded. Milliseconds:

| API | Dry before | Dry after | Heavy rain before | Heavy rain after | Heavy-rain change |
| --- | ---: | ---: | ---: | ---: | ---: |
| d3d12 | 13.887 | 14.038 | 16.486 | 16.158 | -0.328 ms |
| vulkan | 14.064 | 14.048 | 18.364 | 17.185 | -1.179 ms |

| API | Heavy-rain GPU mean | GPU p95 | CPU-frame median | Host-frame medians |
| --- | ---: | ---: | ---: | --- |
| d3d12 | 16.349 | 17.232 | 16.123 | 16.305, 16.326 |
| vulkan | 17.451 | 18.659 | 17.163 | 17.323, 17.393 |

The whole-frame target remains 16.67 ms. GPU duration alone does not establish
end-to-end 60 FPS. Per-run host means/tails, clocks, temperatures, source settings
and any rejected samples are in performance-analysis.json. D3D12 is a control:
its unchanged shader path must not be presented as a structural speedup.
These are Phlosion measurements, not matched retail Cyberpunk FPS.

Separate initial diagnostic means showed Vulkan far rain 2.362 -> 1.089 ms and
near rain 0.150 -> 0.055 ms. Pass timestamps are not added to total-only frame
results. Shelter-map work stayed around 0.5 ms; map copies, synchronization,
staged draws and publication remain, so a direct clear does not make that stage
free. The legacy haze pass remains about 1.2 ms on this GPU. Deferred impact
submission markers do not establish the actual GPU cost of individual splashes.

## Next category and boundaries

Return to mist/haze/light scattering. Reuse the qualified spatial lighting volume
where the complete fog producer/consumer chain permits it, and replace duplicate
legacy work. Its current cost is an opportunity to investigate, not a promised
saving. Full fog composition, local density/emitters, wet-material flow,
sandstorms and character/hair wind retain their own acceptance gates.
Linux qualification and retail pixel/FPS identity remain outside this Windows pass.

Evidence: artifacts/lighting-foundation/rain-efficiency-20261101/evidence.zip,
performance-analysis.json, visual-review.json and handoff-manifest.json.
integration-final.json and integration-evidence.zip record final canonical
revisions, strict dependencies and exact runtime/editor checks.
