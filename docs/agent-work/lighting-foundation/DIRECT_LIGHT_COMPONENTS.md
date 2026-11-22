# Direct local-light components - 2026-10-27

This is a C7 prerequisite, not a connected clustered-lighting implementation or
an increase in the roadmap percentages. Normal rendering now calls reusable
per-source evaluators with separate diffuse/specular results. No runtime toggle,
quality cut, new render pass or buffer allocation was introduced.

## Why this precedes dispatch

The former environmentLocalPbr combined direct point/rectangle light with diffuse
probe transport. Reusing that function in a material-classified light-buffer
pass would put probe bounce into the direct-light output. It also exposed only
the sum of diffuse and specular.

world_local_direct.glsl now owns the existing source sampling, rectangle
integrals and indexed point/rectangle PBR evaluators. The indexed evaluators
return direct-only components. The forward traversal sums each source into one
RGB accumulator, and the wrapper adds probe bounce once. The specialized
diffuse-only character route retains its original response and probe integration.

The indexed evaluator outputs are scene-linear **material-weighted outgoing radiance**:
their sum already includes diffuse albedo/metallic and specular F0. A future
buffer consumer must not multiply those factors again. They are not a claim
that all source light-buffer/material layouts are interchangeable.

The canopy FiniteLine response, alternate LTC branch, range/angular behavior,
two-sided local normals, shadow visibility and authored material values remain.
The four point/sixteen rectangle forward arrays remain unchanged; they are not
a clustered light list. Global/sun light, probes, fog, reflection composition,
specialized leaves/SSS and Hybrid surface impacts retain their existing paths.

## Source trace and boundary

Read-only source snapshot, not authenticated as the current retail revision:
- clusteredDeferred.cpp:629/758 collects/packs lights; the active area branch
  remains sphere/capsule rather than proving rectangle LTC.
- renderNode_Lighting.cpp:1164 builds material work lists, and :224 dispatches
  their classified lighting variants.
- lights_compute.fx:212/239 invokes local component evaluation; :432/434 writes
  separate diffuse/specular outputs.
- include_lightingHelpers.fx:465/820 contains standard/local component handling.
- lightIntegrate.fx:96/97/157 reads those outputs and integrates other transport.

Exact file hashes and anchors are in the private source-anchors.json evidence.
No source-snapshot code or assets were added to the engine.

Still required for C7: connected collection/feature/channel records, clustered
GPU culling, material tile lists/indirect dispatch, light buffers and their actual
consumer, with shadow/resource/lifetime handling and default field/battle/editor
integration. A changed function boundary is not that mechanism. Implement those
producer/consumer stages together rather than adding an unused GPU pass.

## Verification

Optimized RelWithDebInfo builds on gpu-worker Windows; controller ran no builds or
tests. Engine baseline 702c35c, game d2cd3a9, ModelSupport da6a969.

- 4,096 mixed point/FiniteLine/LTC synthetic cases on each of D3D12/Vulkan compare
  against the frozen engine forward loop, including shadowed, grazing, metallic,
  rough and out-of-range receivers. Reversed source traversal checks indexed
  evaluator independence. Analytic aligned point-light cases check the diffuse
  and specular outputs individually, including the existing GGX denominator floor.
- Existing finite-light numerical/domain and rectangle packing/LTC tests pass.
- Ten native shared-surface cases cover mapped normals, material properties,
  masked surfaces, unsupported materials and extent changes on both APIs.
- Fourteen full-scene runs: on each API, wet before/after, dry, camera movement,
  battle, editor wet and editor battle. No renderer validation errors; the known
  D3D12 warning 1328 remains. No redundant screen-reflection source draw returned.
- Full-scene tests keep specialized-forward participation and native motion/
  temporal outputs. Only same-host, same-API wet before/after images are compared;
  runtime and editor have different viewport extents.

Native component results:
- d3d12: PASS 4096 mixed point/finite/LTC direct cases; normalized reference error 1.19209e-07, reordered list error 6.00877e-08
- vulkan: PASS 4096 mixed point/finite/LTC direct cases; normalized reference error 1.01874e-07, reordered list error 6.21092e-08

Wet before/after byte-image differences (0-255):
- d3d12: mean 0.00000000, maximum 0.
- vulkan: mean 0.00038773, maximum 1.

Initial fixture-only HLSL adapter failures are retained in evidence. FXC's
aggressive optimization of the repeated synthetic loop was bounded with test-only
loop/fastopt hints; production shaders use their ordinary optimized compilation.
Full-scene D3D12 qualification exposed a brittle translation rule: loop annotations
were applied by matching exact variable names/spacing, so renamed loops lost
the HLSL loop policy. That caused derivative/unrolling compile failures and long
optimizer runs. An explicit PHLOSION_LOCAL_LIGHT_LOOP macro now carries the
backend policy; a CPU check verifies both HLSL and GLSL generated source.
The forward caller supplies PCF visibility and prepares shared LTC context;
pure per-source evaluators receive prepared, view-facing normals. The existing
single RGB forward accumulator is retained. Failed/stopped candidates are kept
in evidence; no incomplete candidate was integrated.
The first visually qualified revision regressed D3D12 geometry cost. A reversed
before/after timing pair confirmed it. The finite forward branch now retains
its original continue/visibility/combined-accumulator structure while reusing
its existing indexed diffuse/specular evaluator. Point-light early-outs,
multiplication after lobe combination, LUT setup after
the point loop, and once-per-receiver preparation were also restored after a
smaller residual D3D12 cost remained. Unlit point BRDF lobes are shared by the
forward traversal and the indexed radiance evaluator; their units are documented
at the interface. The slower revisions were not integrated; their scene results
and timing pairs remain in the evidence.
No image or numerical threshold was relaxed.

## Timing guard, not an optimization result

Native 1920x1080 Medium wet night, EVGA RTX 2060 SUPER
GPU_1_UUID (LUID 000000000000be8e).
Validation/readbacks disabled, 140 warm frames (40-179), one matched before/after
pair per API, starts at or below 45 C and no thermal slowdown flags.

| API | Before GPU median ms | After GPU median ms | Difference ms |
| --- | ---: | ---: | ---: |
| d3d12 | 32.149 | 32.024 | -0.125 |
| vulkan | 33.927 | 33.641 | -0.285 |

Single pairs are a regression guard, not a speedup claim. Full CPU/GPU/present
mean, median and tail distributions and thermal telemetry are retained.
Cold shader preparation remains expensive and was not part of the warm-frame
comparison. The 16.67 ms goal and connected C7/C8 work remain open.

Evidence: WORKER_EVIDENCE_ROOT/phlosion-clustered-lighting-20261027 on gpu-worker,
with reviewed delta and returned evidence under
PROJECTS_ROOT/Phlosion/PhlosionEngine/artifacts/agent-work/clustered-lighting-20261027 on controller.
