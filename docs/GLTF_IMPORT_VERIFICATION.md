# glTF character import verification

Status: Implemented CPU contracts (M1-F), extended with cubic sampling (M1-H)
and texture decoding (M1-I)
Last updated: 2026-09-20

## Goal and boundary

Character meshes, skeletal animation and material metadata already work through
the engine's glTF path. M1-F recorded its existing behavior with public,
asset-independent CPU tests, without adding a runtime feature or asset format.
M1-H preserves cubic tangents and verifies sampled translation/rotation/scale
through the same importer and the shared production evaluator.

The verified entry points are the existing production functions:

- [`loader::tryLoad`](../src/engine/render/gltf/FastGLTFLoader.h) parses a real
  glTF path with the engine's fixed option set (`LoadExternalBuffers`,
  `LoadExternalImages`, `DecomposeNodeMatrices`, `GenerateMeshIndices`) and
  supported-extension mask, and returns nothing when parsing fails.
- [`buildSceneData`](../src/engine/render/gltf/ModelFastGltfSceneData.cpp) turns
  a parsed `fastgltf::Asset` into node defaults, names, children, mesh/skin
  assignments, selected scene roots, skins and animation clips.
- [`resolveMaterialRenderInfo`](../src/engine/render/gltf/ModelFastGltfMaterial.cpp)
  resolves CPU material metadata for one material index.

The tests do not reimplement the importer or its math, add test-only runtime
APIs, or refactor `Model`. M1-F left production sources unchanged; M1-H's
bounded runtime changes are described in [animation verification](ANIMATION_VERIFICATION.md).
The five CPU texture decode helpers are verified separately against real encoded
payloads in [glTF texture decoding
verification](GLTF_TEXTURE_VERIFICATION.md), which also records their precise
limits.

## What the CPU contracts execute

[`TestGltfSceneImport.cpp`](../tests/TestGltfSceneImport.cpp) and
[`TestGltfMaterialImport.cpp`](../tests/TestGltfMaterialImport.cpp) author tiny
public fixtures (JSON plus explicit little-endian binary data) in a uniquely
named temporary directory owned by the instance that created it, and load them
through `loader::tryLoad`. Shared helpers live in
[`GltfImportTestSupport.h`](../tests/gltf_import/GltfImportTestSupport.h); the
directory name is reserved with `create_directory` so simultaneous instances
and concurrent processes cannot share or delete each other's fixtures, and
[`TestGltfImportFixture.cpp`](../tests/TestGltfImportFixture.cpp) holds the
regression for that ownership.
[`fastgltf::validate`](https://github.com/spnda/fastgltf) is also run on parsed
fixture data as a stricter parse-level structural check; it is not proof of spec
conformance or of rendering. None of the contracts constructs a `Model`,
uploads a texture or creates a graphics context. In the texture contract's
selected invalid-reference probes, deliberately out-of-range
texture/image/sampler references load successfully and reach the decoder's own
guards; the contract logs those documents' `fastgltf::validate` verdicts and
asserts the loader acceptance and decoder result rather than the verdict.

| Contract | Coverage |
| --- | --- |
| `gltf_scene_import_contract` | Named hierarchy with translated, rotated and nonuniformly scaled nodes; authored child ordering; mesh-only and mesh-plus-skin node assignments; scene roots for an absent `scene` member, for an authored `scene: 1`, and for a document with no scene list, each list holding parentless nodes; a structurally coherent skinned triangle (JOINTS_0/WEIGHTS_0 present, joint indices addressing the skin's joints, weights normalized, mesh and skin assigned together); joint ordering; nonidentity inverse bind matrices including column order; omitted inverse binds resolving to identity; two clips with LINEAR and STEP translation/rotation/scale channels, key times, values including quaternion xyzw order, sampler/target associations, clip names and duration from the last key across samplers; a third CUBICSPLINE clip whose three samplers must keep the raw per-key tangent triplets and xyzw rotation order and whose `samplePose` results match independently hand-computed Hermite values; re-import into prefilled containers and a null name list; missing file, malformed JSON and a missing external buffer failing at the loader boundary; an otherwise empty valid document importing empty containers |
| `gltf_material_import_contract` | Default and out-of-range material indices; default OPAQUE factors; authored metallic/roughness, normal scale, occlusion strength, emissive factor with `KHR_materials_emissive_strength` applied once, alpha cutoff and double-sided state; OPAQUE/MASK/BLEND mapping; the existing BLEND-to-OPAQUE/MASK base-texture alpha normalization with literal 2x2 `CPUTexture` pixel arrays |
| `gltf_texture_import_contract` | The five existing CPU decode helpers over genuine PNG/JPEG payloads reached through `loader::tryLoad`: external-URI, image data-URI, bufferView and GLB routes, per-role selection, sampler metadata, a lossy JPEG patch, placeholders, alpha-mode integration and bounded negative probes. See [glTF texture decoding verification](GLTF_TEXTURE_VERIFICATION.md) for the precise limits |
| `gltf_import_fixture_contract` | Fixture-helper ownership only: simultaneous and concurrent instances with one prefix get distinct directories beneath the system temporary directory, a destroyed instance removes only its own directory, sibling markers and preexisting sentinel content survive, and unsafe prefix/child-name components are refused |

Run the contracts directly:

```powershell
cmake --build --preset debug --target PhlosionEngineTests --parallel 4
ctest --preset debug -R PhlosionEngineContracts --output-on-failure
```

`PhlosionEngineTests.exe` runs all registered contracts and labels results by
name, reporting the first failure detail per contract. The contracts run from
the CTest build directory and need no graphics device.

## Engine policies recorded here

The blend normalization in `resolveMaterialRenderInfo` is an engine policy, not
a glTF guarantee: a BLEND material with a base texture whose alpha is
effectively opaque becomes OPAQUE, and one that is mostly binary cutout becomes
MASK with the cutoff clamped into `[0.1, 0.9]`. A material with no base texture
pixels, or with intermediate alpha values, stays BLEND. The contract pins this
behavior in `caseBlendNormalizationPolicy` so a future change is deliberate; it
does not declare the policy correct for every asset.

`MaterialRenderInfo` computes normal, metallic, roughness and occlusion factors
for the CPU caller. The current `Model` path copies emissive factor, alpha mode,
alpha cutoff and double-sided state into `Submesh`; the other factor fields are
CPU metadata here. How a scene feeds such factors into a backend belongs to the
project material path and is outside this contract.

## Evidence limits and remaining gaps

Import checks prove that listed values are parsed and mapped. The added cubic
samples also verify CPU playback, with these boundaries:

- M1-H samples cubic T/R/S poses, but no GPU skinning or pixels are exercised
  by these CPU contracts. Joint deformation and per-backend shading require
  the separate native animation evidence. The original positive fixture carries coherent
  JOINTS_0/WEIGHTS_0 attributes so it exercises a valid skinned document, but
  that is structure, not deformation: no vertex is transformed by a skin.
- Full vertex import, index generation and material appearance are not
  exercised; material factor checks do not imply material parity on OpenGL,
  Vulkan and Direct3D 12. Texture decoding through the same loader is covered by
  the sibling texture contract (M1-I), whose guide records the remaining gaps
  (no runtime call sites for three of the five helpers, no GPU sampler or UV
  binding evidence, and a source-derived factor-only emissive gap in the legacy
  OpenGL Model path without captured GPU qualification).
- A private character is not qualified. Existing three-API captures from other
  passes remain that pass's evidence; in particular the rigid fixture marker is
  not skeletal-animation qualification.
- `buildSceneData` keeps CUBICSPLINE outputs exactly as authored, including the
  raw per-key `[in-tangent, value, out-tangent]` triplets, and the shared CPU
  pose sampler evaluates them with componentwise cubic Hermite interpolation
  (M1-H). The import contract authors a cubic translation/rotation/scale fixture
  and checks the raw triplets plus hand-computed sampled results; native
  multi-API qualification of that behavior lives in
  [ANIMATION_VERIFICATION.md](ANIMATION_VERIFICATION.md).
- Morph-target weight channels are not represented: `ChannelPath` has only
  translation, rotation and scale, and other channel paths are skipped.
  Advanced blending, animation events, root-motion consumption and managed
  attachments remain separate work.
- Malformed-content coverage is limited deliberately. The contracts assert that
  a missing file, malformed JSON and a missing external buffer fail at the
  loader boundary; they do not feed corrupt accessors into fastgltf iterators
  or claim broad validation of hostile documents.

## Related documentation

- [glTF texture decoding verification](GLTF_TEXTURE_VERIFICATION.md) for the
  CPU decode helpers, genuine image payloads and their limits.
- [Verification](VERIFICATION.md) for the CI scope and the dated evidence
  record.
- [Material profiles](MATERIAL_PROFILES.md) for the project material path.
- [Asset architecture](PHLOSION_ASSET_ARCHITECTURE.md) for optional cooked
  formats; adopting them is not required to use the existing glTF path.
