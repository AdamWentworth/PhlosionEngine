# glTF texture decoding verification

Status: Implemented CPU contract (M1-I)
Last updated: 2026-09-20

## Goal and boundary

M1-F checked glTF material metadata against hand-written `CPUTexture` pixels.
M1-I verifies the input boundary underneath it: the existing CPU decode helpers
now decode genuine PNG and JPEG payloads produced by an independent encoder,
reached through the production file loader.

This pass is tests and documentation only. Production sources, shaders,
dependencies, assets and runtime behavior are unchanged, and no graphics
context is created. The contract does not claim GPU sampler or UV binding
behavior, rendered PBR appearance, linear-light color handling, or native
parity on OpenGL, Vulkan and Direct3D 12.

## Verified entry points

- [`loader::tryLoad`](../src/engine/render/gltf/FastGLTFLoader.h) parses a real
  glTF/GLB path with the engine's fixed option set (`LoadExternalBuffers`,
  `LoadExternalImages`, `DecomposeNodeMatrices`, `GenerateMeshIndices`).
- The five existing CPU decode helpers in
  [`ModelFastGltfTextures.h`](../src/engine/render/gltf/ModelFastGltfTextures.h):
  `decodeBaseColorTextureFast`, `decodeEmissiveTextureFast`,
  `decodeNormalTextureFast`, `decodeMetallicRoughnessTextureFast` and
  `decodeOcclusionTextureFast`. None of them needs a `Model` or a GL context.
- [`resolveMaterialRenderInfo`](../src/engine/render/gltf/ModelFastGltfMaterial.cpp)
  consumes the decoder's actual output for the alpha-mode integration check.

## What the CPU contract executes

[`TestGltfTextureImport.cpp`](../tests/TestGltfTextureImport.cpp) authors a glTF
document, an external binary buffer holding padded image bufferViews, one
external PNG file and a valid binary glTF (GLB) container in a uniquely owned
temporary directory, and loads them through `loader::tryLoad`. The encoded
payloads live in
[`GltfTextureFixtures.h`](../tests/gltf_import/GltfTextureFixtures.h) as fixed
bytes captured once from an independent PNG/JPEG encoder, and every expectation
is a literal authored pixel value.

| Case | Coverage |
| --- | --- |
| `caseRoutesReproducePng` | The same non-symmetric 2x2 RGBA PNG (four distinct alpha values) reproduces dimensions, channel order, alpha and row order through the external relative URI, the image data URI, the bufferView-in-external-buffer route (nonzero offset plus padding) and the bufferView route inside a GLB |
| `caseRoleSelection` | All five roles select their own authored image (five distinguishable payloads across three distinct sizes: 2x2, 3x2 and 2x1) and report their own texture-coordinate index; one material whose five roles share one image returns that image for every role; a null `outTexCoordIndex` is accepted |
| `caseSamplerMetadata` | Absent sampler index, explicit empty sampler, all three wrap modes on each axis, nearest/linear magnification and all six minification filter enums, compared as decoder enum metadata only |
| `caseJpegFlatPatch` | A real lossy JPEG through a loaded glTF: exact dimensions and opaque decoded alpha, colour within a justified three-per-channel tolerance |
| `caseDefaultsAndFactors` | Default and out-of-range material indices for every role; a valid material with no texture (base color becomes the authored `baseColorFactor` as RGBA8, other roles use their own placeholders); factor-only base color with literal rounding; the recorded encoded-byte factor multiplication; raw emissive/normal/metallic-roughness/occlusion bytes with scalar factors reported only as metadata |
| `caseAlphaIntegration` | Real decoded opaque, binary-cutout and intermediate-alpha textures feed `resolveMaterialRenderInfo` and drive OPAQUE/MASK/BLEND; an authored base-color alpha factor is what changes the result |
| `caseDecodeErrorFallbacks` | Each present-but-undecodable payload (PNG signature only, truncated PNG, truncated JPEG) is bound to all five roles and, before each decode, the loaded fixture is asserted to select that exact corrupt image through the role's own slot; every role then takes its role-specific decode-error placeholder |
| `caseInvalidSelectionGuards` | Authored out-of-range texture and image indices, a texture with no image index, and an out-of-range sampler index |
| `caseMissingExternalImage` | A referenced-but-absent external image file fails at the loader boundary |
| `caseDecodedTextureRetention` | Previously returned `CPUTexture` values keep their own pixels after later decodes of the same source image with a different role and a different factor: the same bufferView PNG A is decoded as base color and then as normal, and a factored base-color decode of the same image must leave both earlier results unchanged |

The fixture helper and directory ownership are shared with the existing glTF
contracts, so simultaneous or concurrent instances cannot share or delete each
other's temporary directory.

The contract is also hermetic against ambient decoder debug settings. The
decoder can write debug textures under a process-relative
`debug/gltf_textures` directory and can add logging when
`PHLOSION_GLTF_DUMP_TEXTURES`, `PHLOSION_GLTF_DEBUG_ALL` or
`PHLOSION_GLTF_DEBUG_MATCH` are set, so the contract forces those three flags
off for its whole duration with a scoped, process-local override that restores
the caller's previous values on every exit path. It mutates no global working
directory.

## Observed route and boundary behavior

The contract prints what it observed, and the phase report retains the log:

- Under the engine's fixed loader options, external file images and image data
  URIs both arrive as `fastgltf::sources::Array` (the loader loads image bytes
  into the asset), and bufferView images arrive as
  `fastgltf::sources::BufferView` in both `.gltf` and GLB containers.
  The decoder's own URI branch (external file read and inline base64 data URI
  handling) is therefore not reached by this loader path and is not covered.
- The deliberately invalid probe documents that carry an out-of-range texture
  index, an out-of-range image index, a texture with no image index, or an
  out-of-range sampler index are accepted by `tryLoad`, and the decoder's index
  and sampler guards are the boundary that runs; that behavior is pinned here.
  `fastgltf::validate` rejects those same probe documents as `InvalidGltf`,
  which the contract prints to the run log without asserting.
- A missing external image file fails at `tryLoad`; the decoder never runs.

## Recorded engine behavior

These are the current engine's behaviors, recorded so a change is deliberate.
None of them is a claim of glTF conformance or of rendered fidelity.

Base color factor application. When a base color texture is present, a
non-default `baseColorFactor` is multiplied into the decoded encoded bytes and
rounded ([`ModelFastGltfTextures.cpp:515`](../src/engine/render/gltf/ModelFastGltfTextures.cpp)).
These are sRGB-encoded texel values, so this is not linear-light factor
application. Emissive, normal, metallic-roughness and occlusion bytes stay raw;
their scalar factors are reported separately through `MaterialRenderInfo` and
must not be baked twice.

Fallback families. Three distinct outcomes exist. The contract pins the first
two; the third is only reproduced by a standalone artifact probe:

- No usable texture reference (valid material without that texture, or an absent
  material index): base color returns the authored `baseColorFactor` as RGBA8
  for a valid material, or opaque white for an absent material; emissive is
  opaque black, normal is flat `(128, 128, 255, 255)`, and
  metallic-roughness/occlusion are opaque white, with repeat wrap and linear
  filtering.
- Present but undecodable bytes: opaque white base color, opaque black
  emissive, flat normal, and white metallic-roughness/occlusion, with repeat
  wrap and linear filtering (role-specific decode-error fallback).
- Empty or unavailable encoded bytes: base color returns opaque white while
  every other role returns opaque black
  ([`ModelFastGltfTextures.cpp:453`](../src/engine/render/gltf/ModelFastGltfTextures.cpp)),
  which differs from the role-specific decode-error placeholders. This is an
  inconsistency, and because it is not a desired contract the checked-in test
  does not assert it. It is reproduced only by
  `artifacts/m1-i/empty-byte-probe.patch`, a standalone safe probe that a
  reviewer applies to the contract and whose observed values are retained in
  `artifacts/m1-i/correct-probe-empty-bytes-from-patch.log`: base color
  `(255,255,255,255)` and emissive/normal/metallic-roughness/occlusion all
  `(0,0,0,255)`, each at 1x1. Correction is scheduled with the next material pass.

Sampler metadata. A decoded texture keeps repeat wrap, mipmapped-linear
minification and linear magnification when no sampler applies, and an
out-of-range sampler index falls back to those same defaults
([`ModelFastGltfTextures.cpp:497`](../src/engine/render/gltf/ModelFastGltfTextures.cpp)).
This is decoder metadata; it does not prove GPU sampler state.

## Evidence limits and remaining gaps

- Missing emissive texture on the legacy OpenGL `Model` path. A material with a
  nonzero `emissiveFactor` and no emissive texture decodes to an opaque black
  1x1 emissive texture ([`ModelFastGltfTextures.cpp:453`](../src/engine/render/gltf/ModelFastGltfTextures.cpp)).
  `Model::loadGLTFFast` uploads those bytes into `Submesh::emissiveTexID`
  ([`ModelFastGltfLoader.cpp:452`](../src/engine/render/gltf/ModelFastGltfLoader.cpp)),
  the legacy OpenGL draw binds it to texture unit 1
  ([`ModelAnimation.cpp:162`](../src/engine/render/ModelAnimation.cpp)) and
  `assets/shaders/model/model.frag` computes
  `srgbToLinear(texture(u_EmissiveTex, TexCoord).rgb) * u_EmissiveFactor`.
  Because that shader always samples the texture, a black placeholder
  contributes no emission regardless of the factor, while the
  [glTF material specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#materials)
  defines `emissiveFactor` as a linear multiplier over a sampled emissive
  texture that defaults to white, so a factor-only emissive material should
  still emit. This is a source-derived consequence for the legacy OpenGL `Model`
  path only: it is not a captured GPU result and not three-API evidence.
  Whether the same black CPU texture reaches any world-scene adapter, and how
  those backends behave for a material with no emissive texture, is a separate
  and unverified question: the world material paths multiply a sampled emissive
  texture by the factor
  ([`D3D12RenderBackendWorldPipeline.cpp:463`](../src/engine/render/d3d12/D3D12RenderBackendWorldPipeline.cpp),
  [`world_material.glsl:302`](../assets/shaders/vulkan/world_material.glsl),
  [`OpenGLRenderBackendWorldPipeline.cpp:792`](../src/engine/render/opengl/OpenGLRenderBackendWorldPipeline.cpp)),
  and the OpenGL world draw supplies a white fallback texture when the emissive
  texture id is 0
  ([`OpenGLRenderBackendWorldDraw.cpp:421`](../src/engine/render/opengl/OpenGLRenderBackendWorldDraw.cpp)),
  so any such implication is conditional until traced.
- Runtime usage. `Model::loadGLTFFast` decodes and uploads only base color and
  emissive textures
  ([`ModelFastGltfLoader.cpp:440`](../src/engine/render/gltf/ModelFastGltfLoader.cpp)).
  The normal, metallic-roughness and occlusion helpers exist and are verified
  here, but this tree has no engine runtime call site for them. Five passing
  decoder tests are not evidence of five-map Model rendering.
- UV sets and transforms. The contract reports one texture-coordinate index per
  role from material metadata. It does not prove that a mesh's UV binding
  matches, that per-role UV sets or transforms are independent, or anything
  about which UV set a backend samples. The loader also selects one UV set
  (`requiredTexCoordForMaterial` at
  [`ModelFastGltfLoader.cpp:105`](../src/engine/render/gltf/ModelFastGltfLoader.cpp))
  and bakes `KHR_texture_transform` only for the base-color texture that matches
  it ([`ModelFastGltfLoader.cpp:301`](../src/engine/render/gltf/ModelFastGltfLoader.cpp)),
  so a per-role independent UV set or transform is not established here.
- Compressed formats. KTX2 (`KHR_texture_basisu`), DDS (`MSFT_texture_dds`) and
  WebP (`EXT_texture_webp`) parser extensions are enabled in the loader's
  extension mask, but no compressed payload is decoded or claimed here.
  Decoding evidence in this contract is PNG and baseline JPEG only.
- Lossy JPEG. The JPEG expectation is one authored flat patch with a
  three-per-channel tolerance. It is not a general JPEG fidelity or colour
  accuracy claim.
- No visual or multi-API claim. Because no production or rendering code changed,
  this pass needs no native captures and provides none. Texture sampling,
  filtering, UVs, material appearance and cross-API parity remain unverified
  and require a separately scoped native phase.

## Running the contract

```powershell
cmake --build --preset debug --target PhlosionEngineTests --parallel 4
ctest --preset debug -R PhlosionEngineContracts --output-on-failure
```

`PhlosionEngineTests.exe` runs all registered contracts and labels results by
name. The new contract creates only uniquely named directories beneath the
system temporary directory, forces the decoder debug flags off for its own
duration, and needs no graphics device.

The empty-image-bytes reproduction is deliberately outside the contract. To
reproduce it, apply the retained probe patch and rebuild:

```powershell
git apply artifacts/m1-i/empty-byte-probe.patch
cmake --build --preset debug --target PhlosionEngineTests --parallel 4
.\build\Debug\PhlosionEngineTests.exe
git apply -R artifacts/m1-i/empty-byte-probe.patch
cmake --build --preset debug --target PhlosionEngineTests --parallel 4
```

`artifacts/` is ignored, so that path is a local retained artifact. The probe
prints the observed per-role values and `artifacts/m1-i/` keeps the log; the
patch is not part of the checked-in contract.

## Related documentation

- [glTF import verification](GLTF_IMPORT_VERIFICATION.md) for the loader,
  scene/skeleton/clip import and material metadata contracts.
- [Material profiles](MATERIAL_PROFILES.md) for the project material path.
- [Verification](VERIFICATION.md) for the CI scope and dated evidence record.
