# glTF character import verification

Status: Implemented CPU contract (M1-F)
Last updated: 2026-09-20

## Goal and boundary

Character meshes, skeletal animation and material metadata already work through
the engine's glTF path. This pass adds no runtime feature and no new asset
format; it records what that path already does with public, asset-independent
CPU tests, and it states the limits of that evidence honestly.

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
APIs, or refactor `Model`. Production sources are unchanged by this pass.

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
uploads a texture or creates a graphics context.

| Contract | Coverage |
| --- | --- |
| `gltf_scene_import_contract` | Named hierarchy with translated, rotated and nonuniformly scaled nodes; authored child ordering; mesh-only and mesh-plus-skin node assignments; scene roots for an absent `scene` member, for an authored `scene: 1`, and for a document with no scene list, each list holding parentless nodes; a structurally coherent skinned triangle (JOINTS_0/WEIGHTS_0 present, joint indices addressing the skin's joints, weights normalized, mesh and skin assigned together); joint ordering; nonidentity inverse bind matrices including column order; omitted inverse binds resolving to identity; two clips with LINEAR and STEP translation/rotation/scale channels, key times, values including quaternion xyzw order, sampler/target associations, clip names and duration from the last key across samplers; re-import into prefilled containers and a null name list; missing file, malformed JSON and a missing external buffer failing at the loader boundary; an otherwise empty valid document importing empty containers |
| `gltf_material_import_contract` | Default and out-of-range material indices; default OPAQUE factors; authored metallic/roughness, normal scale, occlusion strength, emissive factor with `KHR_materials_emissive_strength` applied once, alpha cutoff and double-sided state; OPAQUE/MASK/BLEND mapping; the existing BLEND-to-OPAQUE/MASK base-texture alpha normalization with literal 2x2 `CPUTexture` pixel arrays |
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

An import test proves that listed values are parsed and mapped. It does not
prove appearance or runtime behavior:

- No pose is sampled, no skinning matrix is evaluated and no pixel is drawn.
  Skeletal animation playback, joint deformation and per-backend shading remain
  unverified by these contracts. The positive fixture carries coherent
  JOINTS_0/WEIGHTS_0 attributes so it exercises a valid skinned document, but
  that is structure, not deformation: no vertex is transformed by a skin.
- Full vertex import, index generation, texture decoding and material
  appearance are not exercised; material factor checks do not imply material
  parity on OpenGL, Vulkan and Direct3D 12.
- A private character is not qualified. Existing three-API captures from other
  passes remain that pass's evidence; in particular the rigid fixture marker is
  not skeletal-animation qualification.
- `buildSceneData` drops CUBICSPLINE tangent entries and keeps only the value at
  each key, while `ModelAnimation` samples every non-STEP channel with linear
  interpolation or spherical interpolation. Cubic-spline authoring therefore
  loses its tangent fidelity today. This is a recorded limitation, not desired
  behavior; restoring cubic interpolation is a later bounded runtime change and
  would need native multi-API visual qualification.
- Morph-target weight channels are not represented: `ChannelPath` has only
  translation, rotation and scale, and other channel paths are skipped.
  Advanced blending, animation events, root-motion consumption and managed
  attachments remain separate work.
- Malformed-content coverage is limited deliberately. The contracts assert that
  a missing file, malformed JSON and a missing external buffer fail at the
  loader boundary; they do not feed corrupt accessors into fastgltf iterators
  or claim broad validation of hostile documents.

## Related documentation

- [Verification](VERIFICATION.md) for the CI scope and the dated evidence
  record.
- [Material profiles](MATERIAL_PROFILES.md) for the project material path.
- [Asset architecture](PHLOSION_ASSET_ARCHITECTURE.md) for optional cooked
  formats; adopting them is not required to use the existing glTF path.
