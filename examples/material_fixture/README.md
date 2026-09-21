# Material fixture example

This directory is the runnable sample for the public five-map material fixture.
It authors a small public glTF document plus five tiny role maps, loads them
through the production glTF loader and the five existing CPU decode helpers, and
presents the decoded pixels through the shared world renderer's existing
`WorldTextureData` role slots:

| File | Role |
| --- | --- |
| `MaterialFixtureCase.h/.cpp` | The three authored cases, the eight-patch layout, the material debug view per patch and the shared camera/background |
| `MaterialFixtureAssets.h/.cpp` | The authored role maps and their deterministic PNG writer, the per-case glTF documents, staging, decoding and the content-dependent role cache keys |
| `MaterialFixturePresentation.h/.cpp` | `WorldTextureData` assembly, the eight patch draws with their debug views, the debug-state reset and the handshake document |
| `MaterialFixtureRuntimeMain.cpp` | Standalone `MaterialFixtureRuntime` host (hidden native window, no backend fallback) |
| `MaterialFixtureEditorPlugin.cpp` | `MaterialFixtureEditorPlugin` project plugin loaded through the existing editor ABI |

The sample never reimplements decoding. It writes its authored PNG payloads and
glTF document into a run-owned temporary directory beneath the system temporary
directory, calls `loader::tryLoad`, then calls `decodeBaseColorTextureFast`,
`decodeEmissiveTextureFast`, `decodeNormalTextureFast`,
`decodeMetallicRoughnessTextureFast` and `decodeOcclusionTextureFast`. The
decoded `CPUTexture` values own their pixels and stay alive for the whole host
run, so the pointers submitted in `WorldTextureData` are stable across draws.

One bounded layout serves every case: eight equally sized patches in one
orthographic row, drawn with material debug views 1 (raw base colour), 2
(tint-resolved albedo), 3 (normal), 4 (roughness), 5 (metallic), 6 (occlusion),
7 (emissive) and 0 (the composed lit material). The transient debug view is
reset to the ordinary material path before the render call returns.

The three cases are:

| Case | Input |
| --- | --- |
| `authored-maps` | Five distinguishable decoded images; base and emissive are submitted as sRGB textures, normal/metallic-roughness/occlusion as linear textures; nontrivial scalar factors |
| `empty-image-bytes` | Every role selects a zero-length encoded image bufferView. A bounded malformed-input fallback case, not a valid glTF asset |
| `absent-maps` | A valid material with no maps and neutral factors, which must agree with the corrected unavailable-bytes placeholders |

Verification support lives outside this directory and is deliberately not a
dependency of the sample:

- `tools/material_fixture/` owns the image primitives, the authored
  expectations, the guard CLI (`MaterialFixtureGuard`) and the capture runner
  (`run_capture_matrix.ps1`, which reuses the shared native process launcher).
- `tests/material_fixture/` owns the adversarial PowerShell harness; the C++
  contract stays at `tests/TestMaterialFixture.cpp`.

CMake exposes the sample as `material_fixture` and the verification support as
`material_fixture_verification`. The runtime and editor plugin link only the
sample; the guard and tests link both. See
[`docs/MATERIAL_VERIFICATION.md`](../../docs/MATERIAL_VERIFICATION.md) for the
layout contract, capture matrix and provenance requirements.
