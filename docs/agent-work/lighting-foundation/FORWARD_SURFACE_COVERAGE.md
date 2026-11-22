# Specialized surface coverage - 2026-10-27

Pokemon body materials now supply their evaluated surface to the normal temporal
renderer and consume its prepared screen-reflection radiance. This closes the
visible SSS material gap found in the Outskirt battle audit. It does not complete
C1-C8 or establish a new retail Cyberpunk visual/performance percentage.

## Implementation and ownership

The previous battle capture had 4,589 motion-only pixels: Espeon/Poochyena body
primitives supplied identity and motion without evaluated reflection attributes.
PokemonModelSupport now exports its actual SSS albedo, mapped normal, roughness,
occlusion and dielectric F0 on D3D12/Vulkan. The ordinary character shader still
owns wrapped diffuse, subsurface fill, fibre detail and direct-light response.
No generic PBR material replaces that shading. Scene-enabled specular consumes
the prepared SSR result before Fresnel/occlusion; missing hits retain the prior
ambient fallback. Neutral inspection lighting stays on its previous path.

Engine surface metadata now separates participation from shading model:

- Normal RGB10A2 alpha: 0 empty, 1 opaque evaluated, 2 masked evaluated,
  3 motion-only (no evaluated reflection record).
- Albedo RGB10A2 alpha: 0 standard PBR, 1 specialized forward shading.
  The latter supplies reflection/contact attributes, not a complete deferred
  SSS lighting record. Future material dispatch must retain its forward shader.
- The physical six-plane layout remains **28 bytes/pixel**. No production
  attachment or geometry pass was added. Optional diagnostic readback now includes
  the packed albedo/model field; it is disabled in timing.

The source map remains lighting/reflections/SOURCE_RENDER_CALL_PATH.md:
renderGraphNodes.cpp:394-437 and include_materialOutputs.fx:254-276 describe
compact shared material outputs and class-aware consumers. Our two-bit values
are Phlosion metadata, not copied source stencil IDs. Pokemon-specific SSS
adaptation belongs to ModelSupport; generic packing, temporal binding and tests
belong to the engine. The game only adds per-object model-count diagnostics.

The new receiver oracle also exposed a generic Vulkan bug. Temporal SSR produced
the result and depth, but did not bind that depth to the material's depth-check
sampler. It depended on earlier contact/reflection state. The resolve now binds
its own captured depth explicitly, matching the D3D12 ownership. The independent
fixture deliberately omits the contact pass, so that dependency cannot hide.

## Actual coverage

Counts are final runtime GPU readbacks at native 1080p. They establish these
specific field/battle views, not every possible model or camera.

| View | Empty | Opaque evaluated | Masked evaluated | Motion-only | Specialized forward |
| --- | ---: | ---: | ---: | ---: | ---: |
| d3d12-wet | 531778 | 1514726 | 27096 | 0 | 0 |
| d3d12-dry | 531775 | 1514770 | 27055 | 0 | 0 |
| d3d12-orbit | 486941 | 1569968 | 16691 | 0 | 0 |
| d3d12-battle | 0 | 2047241 | 26359 | 0 | 4589 |
| vulkan-wet | 531783 | 1514846 | 26971 | 0 | 0 |
| vulkan-dry | 531781 | 1514914 | 26905 | 0 | 0 |
| vulkan-orbit | 486944 | 1570023 | 16633 | 0 | 0 |
| vulkan-battle | 0 | 2047457 | 26143 | 0 | 4589 |

Per-object identities/classes/models are retained in coverage.json. No visible
enrolled opaque material remains motion-only in these views. The specialized
model tag prevents falsely presenting those body pixels as standard PBR coverage.
Transparent materials still follow their separate path.

## Verification

All builds/checks ran on gpu-worker Windows, optimized RelWithDebInfo. Native
renderer and scene runs selected EVGA RTX 2060 SUPER, LUID 000000000000be8e.
Algebra-only compute fixtures retain their existing first-capable-adapter policy.

- Five engine and six game CPU suites, plus nine character/boundary contracts.
- Existing compact-surface, motion, wind, contact, legacy shared reflection and
  temporal reconstruction fixtures passed on D3D12 and Vulkan.
- New SSS GPU checks verify actual mapped normal, sqrt albedo, AO, roughness,
  F0, metalness, exact identity/motion, specialized tag, opaque/masked participation
  and cutout holes.
- The real project shader reflects a red previous-frame wall while the current
  wall is blue. The assertion checks the final receiver color as well as the SSR
  image; all four tiers, cuts, exposure, missing motion, roughness, hit motion and
  odd-size/retirement checks pass on both APIs.
- 18 runtime/editor cases and four field/battle transitions passed. Runtime dry,
  wet, moving-camera and battle views, editor wet/battle, and all four quality
  selections use the normal renderer. There is no duplicate SSR source redraw.
- Final editor EXE, loaded plugin and referenced shader-profile hashes are checked.
  No validation errors; warning IDs: 1328.
- Twelve Medium comparisons retain the existing bounds: mean <0.5/255,
  p99 <=3/255 and fewer than 0.1% of channels differing by >8/255.
  Worst mean 0.01730/255; worst p99
  0/255; maximum difference
  71/255. Images are comparative regressions,
  not an assertion that adding a reflection must be pixel-identical.

Two fixture setup mistakes were corrected without relaxing tolerances: blue=255
in the two-channel normal map requires reconstructing Z, and the receiver test
must enable scene-linear radiance like the game. The subsequent Vulkan consumer
failure identified the real missing-depth binding described above.

### Correction to the prior editor evidence

The compact-surfaces task's editor project still referenced the older
compact-history plugin and material profile. Its executable was current, but
that did not qualify the complete final editor payload. Runtime/native checks
and storage measurements remain valid. The four old editor comparisons are
historical mixed-payload images, not proof of final editor material coverage.
This task corrects the project paths and verifies the plugin and all referenced
D3D12/Vulkan shader files against the frozen candidate before launching.
Its four editor runs qualify the current complete payload; comparisons to the
old mixed-payload images are labelled regression comparisons only.

## Warm timing guard

Native 1080p Medium, same EVGA GPU/replay, optimized binaries. One warm pair per
scene/API, 140 complete samples each (frames 40-179 of 220), validation/readback
off, starts <=45 C, no thermal slowdown. Full GPU/CPU/present means, medians,
tails, per-pass profiles and telemetry are retained. A single pair is a
regression guard, not a precision speedup estimate.

| API / scene | Before median ms | After median ms | Before / after p95 ms |
| --- | ---: | ---: | ---: |
| d3d12 wet | 32.234 | 32.120 | 32.817 / 32.885 |
| d3d12 battle | 31.903 | 31.833 | 32.620 / 32.585 |
| vulkan wet | 33.916 | 34.155 | 34.499 / 34.732 |
| vulkan battle | 32.105 | 32.307 | 33.025 / 33.131 |

This is material/consumer correctness work, not the missing clustered lighting
optimization. The **16.67 ms target remains open**. Neither these measurements
nor the supplied source snapshot establish performance parity with retail.

## Remaining gates

Finish the applicable C1/C2 producer audit (other layered character paths,
deformed foliage normals, unsupported CPU-deformed/instanced models, billboard/
transparent boundaries) and connected C1-C6 acceptance. Linux Vulkan remains a
release gate. C7 clustered/classified lighting and C8 placed-probe/shadow
scheduling remain the major missing workload stages. No overall parity estimate
changes for this bounded coverage milestone. Hybrid surface-specific rain
splashes and rings on actual puddles are unchanged.

Evidence: PROJECTS_ROOT/Phlosion/PhlosionEngine/artifacts/agent-work/material-coverage-20261027 on controller and
WORKER_EVIDENCE_ROOT/phlosion-material-coverage-20261027 on gpu-worker, including exact
source/product hashes, failed-fixture diagnostics, final checks and timing.
