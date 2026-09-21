# M1-I: existing glTF texture decoding and material-input verification

Date: 2026-09-20. Astra plans/reviews; one astra_flash_builder implements.
Baseline: clean main 4ab3ac7e06e912cc3668fa0f69655f5dc4b24f84.
The user authorized committing M1-H and proceeding; M1-H is committed.

## Objective and architecture

Verify existing texture decoding with real public images before changing material
rendering. M1-F checked material metadata against manually supplied CPUTexture
pixels; its glTF image contained only a PNG signature and was never decoded.
ModelFastGltfTextures.h already exposes five CPU decode functions, requiring no
Model constructor or graphics context. Use these real production entry points
and loader::tryLoad; do not add another decoder, runtime interface or asset format.

This pass is tests/documentation only. It closes an unverified input boundary;
it does not claim rendered PBR parity or fix the material pipeline. Full PBR
wiring/color-space changes need a separately scoped native three-API phase.
Keep production src/assets/shaders, dependencies, existing examples/guards and
runtime behavior unchanged. No native captures are required for this CPU-only
change; do not recycle older captures as new texture-rendering evidence.

## Concrete contracts

Add tests/TestGltfTextureImport.cpp, registered in the existing CPU executable
and TestMain, using uniquely owned FixtureDirectory instances from
tests/gltf_import/GltfImportTestSupport.h. Put any small test-only image payload
helper under tests/gltf_import, not examples or src. Prefer tiny fixed public
PNG/JPEG byte payloads with documented authored pixels; existing stb image writer
may encode fixtures if needed, but expectations must remain independent literal
pixels/values. No downloaded/private assets, large binary fixtures or dependencies.

Exercise the real file-loader-to-decoder chain, not just programmatic assets:

1. A non-symmetric 2x2 RGBA PNG with independently known pixels (including
   distinct alpha values) through external relative URI, image data URI, and
   bufferView in an external buffer with nonzero offset and padding. Include a
   valid GLB containing the image bufferView if the existing helper can support
   this with a small test-only addition. All routes must reproduce dimensions,
   channel order, alpha and row orientation exactly. Use all five decode roles:
   base color, emissive, normal, metallic/roughness, occlusion. Assert selected
   images by giving roles distinguishable payloads or selected-image variants.
2. A tiny real RGB JPEG through at least one loaded glTF. Use an authored flat
   patch with a justified small lossy tolerance (at most 3 per channel), require
   dimensions and opaque decoded alpha, and do not claim exact JPEG bytes.
3. Explicit wrapping (repeat, clamp, mirrored) and nearest/linear plus all six
   minification-filter enums, default sampler behavior, texture coordinate
   indices on each role and optional null outTexCoordIndex. Verify decoder
   metadata only: this does not prove UV binding or GPU sampler behavior.
4. No texture/absent material defaults, and base-color factor-only RGBA8 output
   with literal rounding expectations. With a texture, document/test current
   encoded-byte factor multiplication as a named legacy policy, not glTF color
   fidelity. Emissive/MR/normal/AO decoded bytes must remain raw; their scalar
   factors belong to separate metadata and must not be baked a second time.
5. Decode real opaque/binary/intermediate-alpha images then pass their actual
   CPUTexture to resolveMaterialRenderInfo, proving OPAQUE/MASK/BLEND policy
   integration. Include a base-color alpha factor that changes the result.
   Do not reconstruct the decoder's output in the test.
6. Safe bounded negative cases: bad/missing material or texture/image selection,
   omitted image, corrupt/truncated PNG/JPEG bytes, missing external image.
   Distinguish loader failure from decoder fallback using actual observed
   boundaries. No giant allocation, wrapped offset, invalid iterator or cyclic
   graph fuzzing. Missing/empty bytes have a known fallback inconsistency (below):
   record an explicit probe in artifacts rather than enforcing it as desired.

Use small table-driven cases without copying production selection/decoding math.
Check no aliasing: previously returned CPU textures retain their pixels after a
subsequent decode, including a different role/factor on the same source image.
Use failure messages naming route, role, pixel/field and expected/actual values.
Reuse fixture ownership; no shared-directory deletion or global cwd mutation.
Any environment overrides must be process-local, scoped and restore prior values.

## Evidence-backed gaps to record, not silently fix

Primary specification reference:
[glTF materials](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#materials).
It distinguishes sRGB color textures from linear data channels and linear
material factors. Inspect and report the following current implementation limits:

- BaseColor factor is baked into encoded byte values; do not call this verified
  linear-light factor application. No color-space redesign in this pass.
- Missing emissive texture yields black in the current decoder, while glTF's
  factor-only emissive semantics require a white sampled multiplier. Establish
  the downstream draw consequence from source or label it unverified.
- Empty/unavailable encoded image bytes return white for base and black for all
  other roles, unlike role-specific normal/MR/AO decode-error fallbacks. Retain
  a small safe reproduction and document the inconsistency without normalizing
  the behavior into a desired contract.
- ModelFastGltfLoader currently decodes/uploads only base and emissive textures;
  the other three helpers exist but have no engine runtime call sites in the
  inspected tree. Do not claim five-map Model rendering from five decoder tests.
- One chosen UV set and base-color transform baking do not establish independent
  per-role UV transforms/sets. KTX2/DDS/WebP parser extensions are not evidence
  of supported texture decoding. Preserve the distinction in the guide.

If discovery contradicts the contract or reveals a production change is required
to finish it, return concrete evidence to Astra before editing runtime code.
Known gaps above are intentionally outside this verification-only phase.

## Ownership, verification and handoff

Worker owns tests/TestGltfTextureImport.cpp, narrow tests/gltf_import additions,
CMake test source registration, tests/TestMain.cpp registration,
docs/GLTF_TEXTURE_VERIFICATION.md (new), necessary accuracy updates in
docs/GLTF_IMPORT_VERIFICATION.md, and this phase's REPORT.md.
Root owns PLAN/CHECKPOINT, roadmap, docs/README and docs/VERIFICATION.

You are not alone. Preserve others' edits; no staging, commits, pushes, recursive
agents, other-repository edits or dependency/config changes. Keep the existing
24 CPU contracts and all seven CTest entries passing. Build all enabled Debug
targets in build-presentation with warnings as errors, run the direct CPU suite
and full CTest on final sources. No graphics context may be required.

Retain raw logs, exact commands/exit codes, safe defect probes and any fixture
artifacts under ignored artifacts/m1-i. Verify the patch contains no production
source/shader edits. Prove test sensitivity with two targeted temporary test
fixture mutations (e.g. changed pixel/channel or sampler expectation), require
the named new contract to fail, restore, then run final passing checks. Do not
leave mutations or blanket skips. Native/GPU/Model-cache behavior remains
unverified by this pass and must be stated plainly.

Allow up to 60 minutes for the coherent implement/test/debug loop. No play-by-play
needed. Return ready_for_review with changed paths, evidence, named limitations
and report. Astra reviews the actual new/tracked diff and permits one consolidated
correction by default. M1-I stays uncommitted after acceptance.
