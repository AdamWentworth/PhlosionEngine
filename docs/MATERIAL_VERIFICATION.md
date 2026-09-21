# Material fixture verification

Status: Implemented native material fixture and empty-image-bytes correction (M1-J)
Last updated: 2026-09-20

## Goal and boundary

M1-J connects the five existing CPU glTF decoders from
[glTF texture decoding verification](GLTF_TEXTURE_VERIFICATION.md) to the
existing shared world renderer in a public runtime/editor sample, qualifies the
result on OpenGL, Vulkan and Direct3D 12 in both hosts, and corrects one bounded
production defect: empty encoded image data now takes the same role-specific
fallback as undecodable image data.

This is not a claim of general material or PBR readiness. The renderer's
shading policies are recorded below rather than changed, and the known
encoded-byte base-colour factor multiplication and the factor-only emissive
behaviour on the legacy `Model` path remain deferred.

## Production change

`src/engine/render/gltf/ModelFastGltfTextures.cpp` returned opaque white for an
empty base-colour role and opaque black for every other role when the encoded
image bytes were empty or unavailable. That branch now returns the decoder's
existing role-specific fallback: opaque white base, opaque black emissive, flat
normal `(128,128,255,255)`, and opaque white metallic-roughness/occlusion, with
the existing fallback sampler metadata (repeat wrap, linear filtering). No
shader, backend, `Model`, cache or ABI change is included. Successful decodes,
absent or invalid references, corrupt-image behaviour, base-factor byte
multiplication and emissive factor handling are unchanged.

The legacy `Model` path decodes only base colour and emissive, and both of those
role fallbacks are unchanged by the correction, so this needs no cache
invalidation: base colour was already white for empty bytes, and emissive is
black both before and after.

## Sample contract

[`examples/material_fixture`](../examples/material_fixture) owns the runnable
sample. It stages an authored glTF document and five tiny PNG payloads (written
by the existing `stb_image_write` dependency) into a run-owned temporary
directory, loads them through `loader::tryLoad` and decodes all five roles with
the existing CPU decode helpers. The decoded `CPUTexture` values own their
pixels and stay alive across every draw.

Ownership of the staging directory is an RAII object: the name is reserved with
`create_directory`, which only reports success for the caller that actually
created the directory, so concurrent hosts and repeated opens cannot share or
remove each other's inputs. Failed staging, exception unwinding and process exit
all release the directory through that owner, the returned texture set keeps it
alive through a shared pointer that copies share, and nothing is ever
glob-deleted. The sample also scopes the decoder's diagnostic environment flags
(`PHLOSION_GLTF_DEBUG_ALL`, `PHLOSION_GLTF_DEBUG_MATCH`,
`PHLOSION_GLTF_DUMP_TEXTURES`) off for the duration of a decode and restores the
caller's values afterwards, so an ambient setting cannot write dumps relative to
the caller's working directory.

One bounded layout serves all three cases: eight equally sized patches in a
single orthographic row, each drawn with one existing per-draw material debug
view plus one composed lit region.

| Patch | Material debug view |
| --- | --- |
| `base` | 1, raw base-colour texture sample |
| `albedo` | 2, authored tint-resolved albedo without lighting |
| `normal` | 3, normal map sample |
| `roughness` | 4, metallic-roughness green channel |
| `metallic` | 5, metallic-roughness blue channel |
| `occlusion` | 6, occlusion red channel |
| `emissive` | 7, emissive sample |
| `lit` | 0, the ordinary composed material path |

The renderer's debug view is submitted immediately before each patch draw and
reset to the ordinary path before the render call returns; the handshake records
that reset and the guard requires it. Base colour and emissive are submitted as
sRGB textures (`textureSrgb = 1`), normal, metallic-roughness and occlusion as
linear textures (`textureSrgb = 0`). All patches share one UV set and identity
model transforms, and `baseColorFactor` is `[1,1,1,1]`. Role cache keys and
geometry keys are content and case dependent.

### Cases

| Case | Authored input | Corrected result |
| --- | --- | --- |
| `authored-maps` | Five distinguishable 8x8 maps; each map differs across its own vertical centre line; metallic-roughness keeps green and blue apart and inverts which is larger between halves; nontrivial scalar factors (`metallicFactor` 0.6, `roughnessFactor` 0.8, `normalScale` 1.25, `occlusionStrength` 0.5, `emissiveFactor` `(0.4,0.2,0.1)`) | Decoded authored pixels in every role slot |
| `empty-image-bytes` | All five roles select a zero-length encoded image bufferView inside one external buffer. A bounded malformed-input fallback case, not a valid glTF asset | Corrected role placeholders: opaque white base, flat normal, opaque white metallic-roughness/occlusion and opaque black emissive, with emission factor zero |
| `absent-maps` | A valid material with no maps and neutral factors | Agrees with the corrected unavailable-bytes placeholders |

Emission factor is zero in both fallback cases, so the known factor-only
emissive limitation is never a required appearance here.

## Color-space treatment

The debug views write their diagnostic value and then take the scene's single
display transfer. For a role submitted as an sRGB texture the authored texel
round-trips back to its authored byte; for a role submitted as a linear texture
the authored byte is sRGB-encoded once. The guard derives both forms from the
authored map bytes and rejects a capture that carries the other form, so the
submitted `textureSrgb` flag cannot silently invert.

Measured interior means on the final Debug binaries at the matched 845x532
viewport: base `(200,60,60)`/`(70,120,200)`,
normal `(219,160,239)`/`(133,224,195)` for authored `(180,90,220)`/`(70,150,210)`,
roughness `137`/`233` and metallic `225`/`99` for metallic-roughness green
`64`/`208` and blue `192`/`32`, occlusion `165`/`241`, emissive
`(240,140,38)`/`(38,140,240)`. The corrected fallback normal patch interior mean is
`(188,188,255)` (the sRGB encoding of the flat `(128,128,255)` placeholder), not
black. Every interior pixel stays within two counts of its literal expected
value, which is inside the guard's three-count interior tolerance.

The composed lit patch is reported the same way, as an interior mean of that
region rather than as a claim about the shading path: `(210,136,113)` for
authored content and `(23,23,23)` for the corrected fallback cases, each measured
on the left half of the patch.

## Capture matrix and guard

`tools/material_fixture/run_capture_matrix.ps1` captures three cases x three
native APIs x two hosts (18 images), records the engine revision/baseline, dirty
state, per-binary SHA-256 and size, requested capture frames and parsed log
provenance, and then runs `MaterialFixtureGuard matrix --require-complete`. It
captures the editor viewport first, derives the validated viewport dimensions
from that capture and then launches the standalone host at exactly those
dimensions, so the two hosts are compared as whole images with the approved
limits. The runner reuses the shared native process launcher, launches every
helper hidden, refuses to reuse an existing run directory and refuses to
continue when the editor viewport is not measurable. An omitted
`-ExpectedBaseline` records the resolved HEAD; an explicitly supplied revision is
resolved to a commit and must match HEAD.

`MaterialFixtureGuard`:

- loads PNGs, detects the fixture's green background band so editor chrome never
  contaminates a comparison, and compares all 18 same-size cross-API pairs and
  all nine runtime/editor pairs with the approved limits (normalized MAE <= 0.01,
  RMSE <= 0.08, changed ratio <= 0.05 at channel tolerance 8) that cannot be
  loosened;
- requires exactly the unique `{runtime, editor} x {opengl, vulkan, d3d12} x
  three cases` Cartesian set, a zero exit code, exactly one matching
  `[Screenshot][<Backend>] WROTE` record at the requested frame, a passing parity
  contract for that API and no OpenGL compatibility fallback;
- re-hashes the runtime, editor, plugin and guard binaries and requires the
  recorded revision to equal the recorded source baseline; the `binaries` object
  and each of its four identities are mandatory, and a stale hash or a size
  mismatch fails the run;
- resolves every entry artifact and metrics path and both output paths against
  the run directory with a canonical, component-aware check before reading or
  writing anything, so a traversal, a sibling name that merely shares a prefix or
  an outside `--out`/`--sheet` is refused without creating a file;
- requires exactly one screenshot `WROTE` record per entry with the requested
  backend, frame, path and size; requires the recorded observed frame, screenshot
  backend and screenshot size to agree with the raw log and the PNG; requires the
  manifest capture frames for both hosts and the editor frame count to be bounded
  integers; and requires the editor metrics file, its `renderer.backend` and its
  bounded `capture.frames` (a missing editor metrics path is a failure, never a
  skipped check);
- requires the handshake host, requested backend, actual backend (no fallback),
  logical frame (bounded integer), surface size (bounded integers), shared sample
  identity, role order, role origin, role color-space flag, exact role
  size/sampler/first-texel metadata, the content hash re-derived from the guard's
  own authored role pixels, the exact `fixture/mat/<case>/<role>/<hash>` key and
  cache key, patch order, patch debug view and patch rectangle to match the
  authored sample; malformed field types, unknown names and fractional or
  oversized integers produce diagnostics instead of exceptions or silent casts.
  Numeric identity is compared exactly: the role content hash must equal the
  expected 32-bit hash promoted to a 64-bit integer, values outside the unsigned
  32-bit range are rejected before any narrowing, the key must be the exact
  `fixture/mat/<case>/<role>/<hash>` string, and every recorded integer field -
  including both `screenshot_size` elements - is read as a 64-bit value and
  range-checked before conversion, so evidence such as the correct value plus
  2^32 or `UINT64_MAX` cannot truncate into an accepted identity. Patch rects are
  normalized doubles and are compared as doubles, not as integers;
- validates each capture against independently authored content: per half and
  channel the interior-region mean must match the authored sample through the
  documented color treatment, and every interior pixel must stay within three
  counts of that literal value, so a corruption that preserves the half mean is
  still rejected; the two halves must differ where the authored map differs,
  roughness and metallic must carry the metallic-roughness green and blue
  channels and invert between halves, the corrected fallback patches must not be
  black, and the composed lit region must be present, different from the measured
  background band, different from the raw base-colour patch and not black;
- compares the composed lit patch region specifically between the authored case
  and each fallback case, so correct diagnostic tiles cannot carry a frozen lit
  region; requires authored content to differ materially from both fallback
  cases; and requires the two fallback cases to agree with each other under the
  same approved limits;
- writes `matrix.json` plus a contact sheet built from the cropped viewports.

The guard's `synth` command builds deterministic captures and handshakes for the
hermetic tests; the synthetic handshakes carry the guard's own literal role
contract, so no hash or first texel is invented. It models a blank frame, a
missing patch, a swapped roughness/metallic routing, a neutralized
normal/AO/emissive map, a missing lit region, a spatially corrupted half that
preserves its mean, the pre-correction black normal/MR/AO fallback, a linear role
view rendered without its sRGB transfer and a lit region frozen at the authored
appearance. The CPU contract requires the validator to accept the pristine
synthetic capture (whose authored and fallback lit regions differ, as the real
ones do) and reject every distinguishable defect, and proves the targeted
lit-region comparison rejects a frozen lit region. The PowerShell harness drives
the same paths through the real CLI, including duplicate and incomplete matrices,
unknown hosts and APIs, loosened thresholds, a stale binary hash, a binary size
mismatch, a missing or partial `binaries` object, an entry/capture frame
mismatch, a handshake host/backend/logical-frame mismatch, a missing requested
backend, a wrong role color space, a wrong role origin, a wrong debug view, a
non run-owned asset directory, a wrong sample identity, a traversal or
sibling-prefix entry path, outside `--out`/`--sheet` targets with preserved
sentinels, a missing editor metrics field, a missing required entry field,
mismatched recorded frame/backend/size evidence, and fractional, oversized,
unknown-name, wrong-type, invented-hash and invented-key handshake input. Each
mutation is restored in its own scope, and the pristine matrix must pass again
before and after.

## Shading observations and their limits

This fixture qualifies decoded inputs and their debug views. It does not
establish the renderer's normal, roughness, metallic, occlusion or emission
mathematics, and no statement below is an independently qualified claim about
those equations.

- Material mode 2 is the world material mode that enables the debug parameter.
  The composed `lit` patch runs the engine's standard material path with the
  review lighting profile, ACES filmic tone mapping and a single display
  transfer; the fixture only requires that this region be present, that it
  differ from the measured background band and the raw base-colour patch, that
  it not be black, and that it differ between the authored and fallback cases.
- Every diagnostic patch (base, albedo, normal, roughness, metallic, occlusion,
  emissive) is a raw or trivially resolved texture sample: the guard checks role
  identity, channel routing, color-space treatment and placeholder values
  against literal authored bytes. Because these views are raw, the fixture does
  not verify that `normalScale`, `metallicFactor`, `roughnessFactor`,
  `occlusionStrength` or `emissiveFactor` are applied on the GPU.
- The handshake records the scalar factors the sample read from the loaded glTF
  through the existing material resolver, and the sample's `WorldTextureData`
  assembly was code-reviewed for consistency with the renderer's documented
  material fields. Neither the handshake nor the captures independently prove
  that submission, and no scalar-application or packed-normal claim is made here.
- Measured interior means, not equations: the authored composed lit region reads
  `(210,136,113)` on the left half and the corrected fallback composed lit region
  reads `(23,23,23)`. The difference is what the targeted lit-region cross-case
  comparison requires; the absolute values are recorded as measurements of this
  bounded fixture, and the fallback darkness is consistent with the corrected
  opaque-white metallic-roughness placeholder reaching the lit path, which is a
  recorded observation rather than a qualified shading derivation.
- The guard's lit check accepts that dark fallback region while still rejecting a
  black region, and the emission factor is zero in both fallback cases so the
  known factor-only emissive limitation is never a required appearance.

## Evidence limits and remaining gaps

- Three APIs, two hosts and one bounded layout are qualified for these three
  cases. This is not a general PBR-conformance, filtering or UV claim: the
  sample uses one shared UV set, identity transforms, `baseColorFactor` 1 and
  default sampler wrap.
- The per-patch interior regions are 8-bit checks (10 counts for an sRGB role
  view mean, 14 for a linear role view mean, 3 for every individual interior
  pixel) with justified insets; they are not full-surface equality tests, and
  the raw debug views do not verify that the scalar factors were applied.
- Known encoded-byte base-colour factor multiplication and the factor-only
  emissive behaviour on the legacy `Model` path remain deferred, as does
  production game adapter work, the legacy `Model` five-map integration,
  independent per-role UV sets, compressed texture formats and
  private-character qualification.
- Full GPU qualification remains a local Windows procedure; there is no
  representative GPU runner in CI, so these captures are dated local evidence.

## Running it

```powershell
cmake --build build-presentation --config Debug --parallel 8
ctest --test-dir build-presentation -C Debug --output-on-failure
powershell -NoProfile -ExecutionPolicy Bypass `
    -File tools/material_fixture/run_capture_matrix.ps1 `
    -EngineRoot . -RunId my-run
```

`MaterialFixtureGuard` also exposes `detect`, `compare`, `expect`, `handshake`,
`synth` and `echo` subcommands for individual images and handshakes.

## Related documentation

- [glTF texture decoding verification](GLTF_TEXTURE_VERIFICATION.md) for the
  decoder boundary and the corrected unavailable-bytes fallback.
- [Animation fixture verification](ANIMATION_VERIFICATION.md) for the shared
  capture-runner and guard conventions this fixture follows.
- [Public asset fixture](PUBLIC_ASSET_FIXTURE.md) for the archive matrix the
  native regression re-runs.
