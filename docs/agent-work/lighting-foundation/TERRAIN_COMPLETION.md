# Layered terrain and static surfaces: E1-E8

Status: all fixed E1-E8 implementation/qualification gates passed. Estimated
coverage is 100% for this supported Windows scene/material contract. Final
canonical revisions and locked-release receipts are recorded with the handoff;
this is not a claim of retail identity or entire-engine equivalence.

## Fixed scope

The existing Outskirt scene uses the connected active multilayer material
technique: immutable layer records and indirect textures, sparse high/low mask
tiles, priority/coverage accumulation, microblend borders, color/roughness/metal
remaps, explicit gradients, global/detail normals and terrain projection.
The Mainline snapshot is traced input, not authenticated current retail.

E1: bounded prepared inputs with provenance, hashes and no runtime research access.
E2: shared immutable residency, sparse masks and complete layer evaluation.
E3: terrain projection/tiling/macro/global normals/specular and distance response;
negative coordinates, slopes, seams and distant coverage.
E4: road, shoulders, court and rocks consume original layer maps; paint stays an
explicit authored surface. No baked composite stands in for missing evaluation.
E5: compiled static geometry/LODs, stable shared identities and conservative
bounds, consistent primary/shadow/probe selection; collision/layout preserved.
E6: evaluated properties feed lighting, contact, reflection, wetness and terrain
inputs to foliage. Hybrid impact variety and actual-puddle rings remain.
E7: bounded ownership/uploads and retirement, scene return and normal presets.
E8: optimized D3D12/Vulkan runtime/editor, CPU/native/independent checks,
dry/wet/moving/battle/return, prepared publication and measured costs.

## Representation and ownership

WorldLayerMaterialSet owns at most 256 materials, 512 RGBA8 textures with complete
mip chains, 256 MiB texture payload and 16 MiB packed parameter/table storage.
Each material has at most 32 128-byte layer records. A validated package pins
every payload by FNV-1a, validates sizes/mips/indices and rejects parent paths,
absolute paths and ADS. Failure leaves the previous immutable snapshot intact.

D3D12 uses an immutable parameter SRV and retained texture table. Vulkan uses
the shared wind/material set with indexed descriptors. Both own GPU uploads under
the existing scope ledger, support startup prewarm outside a frame, reuse
resources across lights/views and retire them behind GPU completion. They do
not retain plugin-owned CPU shared-pointer control blocks after upload; game
assets own those immutable inputs. Native ownership and editor shutdown checks
cover this DLL boundary. Null
material binding restores the nonlayered path. Editor/model ABI62/34 is required.

A named glTF material binds to one package index. The importer discards its
retired baked maps and retains the immutable layer snapshot. Static instances
share GPU geometry keys; skinned identities remain separate. Primary-camera
LOD exclusions also drive shadow, probe and cache revisions. Selection unions every level's decoded bounds in its current node transform,
including small decimation expansions. The final level persists at distance. Presets scale authored distances .75/1/1.25/1.5.

The CPU static-transport evaluator consumes these same immutable inputs. It
uses dry, view-independent normals. Transport v3 adds roughness/F0/dominant
layer/selected coverage to ground records; v1/v2 retain documented defaults.
The ordinary GPU surface uses wetness and view-dependent terrain normal fading.

## Source conventions and deliberate adapters

The source terrain path chooses one dominant projection and blends reconstructed
normal directions; it does not evaluate three full material stacks. Terrain
uses the source 512-metre base, macro color multiplier, nonmetal specular channel,
1.6 normal strength and 20-100 metre normal fade. Source tiling gradients remain
separate from the tiling-map scale. Engine Y-up is explicitly converted to source
Z-up. Mesh materials use their original UVs and tangent basis.

The renderer consumes compiled geometry, matching the active compiled-terrain
route in the inspected snapshot. The disabled alternative runtime material bake
is not the target. General city streaming and arbitrary runtime heightfields
belong to separate asset/loading scope.

The game owns its mask layout, tints, one-kilometre ground, road paint, placement
and LOD distances. Source-specific retail layouts, rock color/roughness choices,
binary assets and exact pixel appearance are not claimed identical. Multilayer
evaluation has no vertex-color albedo term in the traced source shader; discarded
secondary authoring UV/color channels are not a substitute rendering feature.
Textures use prepared uncompressed mip chains rather than the source container
and residency representation. Native tests cover both embedded and composed
game material profiles and ordinary/composition producers.

## Evidence

Worker: WORKER_EVIDENCE_ROOT/phlosion-terrain-complete-20261105.
All implementation, builds, asset preparation and native tests run on gpu-worker.
The original failed fixtures, stale-GI failure and rejected camera setup remain.
No performance or completion claim is based on validation-enabled timings.
Final receipts and measured costs will be added after qualification.

## Completed qualification

Qualification: Windows, optimized RelWithDebInfo with assertions and warnings as
errors, EVGA RTX 2060 SUPER GPU1 (LUID 000000000000c1c7). All work ran on gpu-worker.
128 independent numeric cases per API have maximum absolute error 1.133e-7
against tolerance 2e-5. Native composed-profile rendering covers 18 frames and
108 sampled pixels per API, including first upload inside a capture, replacement,
fallback and nonretention of plugin-owned CPU control blocks. Six engine and
five game CPU contracts, actual prepared-package loading and authoring
reproduction pass. The 108 placements share nine meshes across three LODs;
selection unions all levels' decoded, transformed bounds (decimation expanded
some bounds by up to 7.19 mm).

Twenty-two runtime/live-editor checks plus two editor reference-return checks
pass with API validation and required prepared programs. All four wet presets,
near/far, sunset/night, camera motion and battle are covered. The 1,146 plants,
pose consistency, native surface/motion content and allocation budgets remain.
After the full visual matrix, an additional eight runtime/battle/editor/return
checks qualify the physical-surface LOD fix. Rain shelter, splashes and puddles
use only base geometry: 411,472 cover triangles, identical to the previous
scene after accounting for the intentional asphalt/paint material split. A CPU
regression test verifies that alternate visual meshes cannot become extra rain
surfaces. The engine and shader bytes are unchanged from the full visual matrix.
Editor return restores exactly the original retained payload byte count, with
no allocation failures or pending scopes. Shutdown passes on both APIs.

The package has three material stacks, 34 texture entries and 139,614,612 bytes
of layer texture payload. Imported total retained textures are 218,258,272 bytes.
Station geometry is 34,743,588 bytes; packed transport v3 is 33,777,815 bytes.
It contains 1,995 probes, 2,838 bricks, 495,420 surfels and 245,605 factors.
The explicit authoring entry reproduces all geometry/material outputs exactly.
Three additional microblend source maps are retained in the controller authoring
folder; normal launches consume the three pinned immutable publications.

Costs below use two reversed-order matched pairs per API/weather, normal 175 W,
1080p Medium, validation off and 119 warm frames (121-239). Thermal-flagged runs
are rejected and retained. These are whole-frame costs, not matched retail FPS,
and completion does not mean the 16 ms target is met on every preset.

| API | Weather | Before GPU | Layered terrain GPU | Delta |
| --- | --- | ---: | ---: | ---: |
| d3d12 | dry | 11.498 ms | 13.500 ms | +2.001 ms |
| d3d12 | wet | 16.037 ms | 18.776 ms | +2.739 ms |
| vulkan | dry | 12.824 ms | 15.228 ms | +2.405 ms |
| vulkan | wet | 16.773 ms | 20.314 ms | +3.541 ms |

The original failed fixtures and rejected tests remain in the evidence directory.
They exposed an invalid Vulkan pass resume, a lower-LOD bounds assumption and a
plugin-owned shared-pointer surviving module unload; all three have targeted
regression checks and passing final ordinary consumers. Earlier editor fixture
paths pointing at the old package were corrected without relaxing assertions.

Evidence root: WORKER_EVIDENCE_ROOT/phlosion-terrain-complete-20261105.
See terrain-scene-audit.json, terrain-cost-report.json, reference-final.json,
lod-geometry-union-audit.json, physical-surface-audit.json, terrain-publication.json
and locked-qualification.json.
