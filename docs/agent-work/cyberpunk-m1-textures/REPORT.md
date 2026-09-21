# M1-I worker report: existing glTF texture decoding verification

STATUS: ready_for_review (post-review correction bundle)

Astra disposition: accepted after the consolidated correction; see
[CHECKPOINT.md](CHECKPOINT.md) for integrated decisions and evidence limits.

Task: `/root/cyberpunk_m1_textures`, M1-I.
Workspace: `D:/Projects/Phlosion/PhlosionEngine`, baseline
`4ab3ac7e06e912cc3668fa0f69655f5dc4b24f84` (M1-H committed, clean tree; only
root's `docs/agent-work/cyberpunk-m1-textures/` was untracked at dispatch).
Contract: [`PLAN.md`](PLAN.md). Nothing is staged, committed or pushed.

## Changed paths and behavior

Tests and documentation only. `git status --porcelain` matches no `src/` or
`assets/` path, so no production, shader, dependency, asset or runtime code
changed.

| Path | Change |
| --- | --- |
| `tests/TestGltfTextureImport.cpp` (new) | One new CPU contract, `gltf_texture_import_contract`, over the five existing decode helpers through `loader::tryLoad`: PNG routes (external URI, data URI, bufferView in an external buffer with nonzero offset and padding, GLB bufferView), per-role selection with distinguishable payloads and sizes, per-role texture-coordinate indices and a null out-index, sampler metadata, a real lossy JPEG, defaults/factors, alpha-mode integration, corrupt-payload decode-error fallbacks for all five roles, bounded invalid-selection probes, decode-retention and a scoped decoder-debug environment guard |
| `tests/gltf_import/GltfTextureFixtures.h` (new) | Test-only fixed PNG/JPEG payloads captured once from an independent encoder, literal decoded expectations, a base64 encoder for the data-URI fixture, a GLB container builder, and dimension/pixel/enum expectation helpers with route/role/pixel messages |
| `CMakeLists.txt` | Registers `tests/TestGltfTextureImport.cpp` in the existing `PhlosionEngineTests` executable (one line) |
| `tests/TestMain.cpp` | Declares and registers `gltf_texture_import_contract` (two lines) |
| `docs/GLTF_TEXTURE_VERIFICATION.md` (new) | Verified entry points, case coverage, observed route/boundary behavior, recorded engine behavior, hermeticity guard, precise evidence limits and gaps, and the legacy OpenGL `Model` emissive consequence with the glTF spec reference |
| `docs/GLTF_IMPORT_VERIFICATION.md` | Accuracy updates only: status line, a pointer to the texture contract, one contract-table row, the scoped probe phrasing for the loader/`validate` boundary, and the gap paragraph that used to say texture decoding was unexercised |
| `docs/agent-work/cyberpunk-m1-textures/REPORT.md` (new) | This report |
| `artifacts/m1-i/` (ignored) | Raw logs, the fixture images, `PROBES.md`, the sensitivity and review-correction mutation logs, the environment-proof runs and `empty-byte-probe.patch` |

## Verification

All commands ran natively in PowerShell against the existing
`build-presentation` VS2026 Debug tree (editor, tests and fixture hosts ON,
`PHLOSION_WARNINGS_AS_ERRORS=ON`). No graphics context is created.

| # | Command | Exit | Salient result |
| --- | --- | --- | --- |
| 1 | `cmake --build build-presentation --config Debug --parallel 4` | 0 | Full Debug build of every enabled target, warnings as errors; no warnings (`artifacts/m1-i/final-build.log`) |
| 2 | `pushd build-presentation; .\Debug\PhlosionEngineTests.exe` | 0 | 25 of 25 contracts PASS, including the new `gltf_texture_import_contract` (`artifacts/m1-i/final-tests.log`) |
| 3 | `ctest --test-dir build-presentation -C Debug --output-on-failure` | 0 | 7 of 7 CTest entries pass in 86.7 s (`artifacts/m1-i/final-ctest.log`) |
| 4 | Mutation 1: PNG A expectation R 255 -> 254 in `GltfTextureFixtures.h` | 1 | New contract failed exactly as intended: `route external-relative-uri image[0] source=Array: pixel (0,0) channel R actual 255 0xFF != expected 254 0xFE`, then reverted (`artifacts/m1-i/sensitivity-mutation-01-pixel.log`) |
| 5 | Mutation 2: sampler case "min 9986" magnitude-filter **expectation** changed from `GL_NEAREST` (9728) to `GL_LINEAR` (9729) | 1 | New contract failed exactly as intended: `sampler case min 9986: magF 9728 != expected 9729`, where 9728 is the actual `GL_NEAREST` and 9729 the mutated expectation; then reverted (`artifacts/m1-i/sensitivity-mutation-02-sampler.log`) |
| 6 | Mutation 3 (review correction): removed the truncated-PNG material's `occlusionTexture` binding, the previous false-coverage shape | 1 | New fixture assertion caught it before the decoder: `decode failure truncated-png role occlusion: the role has no authored texture/image reference`; reverted (`artifacts/m1-i/correct-mutation-01-missing-role-binding.log`) |
| 7 | Mutation 4 (review correction): truncated-JPEG entry's intended corrupt image index 9 -> 8 | 1 | `decode failure truncated-jpeg role baseColor: role selects image 9 (texture 9) != intended corrupt image 8`; reverted (`artifacts/m1-i/correct-mutation-02-wrong-corrupt-image.log`) |
| 8 | Ambient-debug hermeticity, positive: final binary run from an isolated directory with `PHLOSION_GLTF_DUMP_TEXTURES=1`, `PHLOSION_GLTF_DEBUG_ALL=1`, `PHLOSION_GLTF_DEBUG_MATCH=test.gltf,textures` | 0 | Contract PASS, isolated directory left empty: 0 `test_mat*` decoder dumps (`artifacts/m1-i/env-proof/guarded-run.log`, `env-proof/clean/`) |
| 9 | Ambient-debug inverse probe: same build with the guard values temporarily flipped to `1`/`1`/`test` | 0 | 32 `test_mat*_basecolor.png` and companion dumps appear under `env-proof/unguarded/debug/gltf_textures/`, showing the guard is what keeps check 8 clean (`env-proof/unguarded-run.log`) |
| 10 | Empty-byte reproduction from the retained artifact patch: `git apply artifacts/m1-i/empty-byte-probe.patch`, build, run in an isolated directory, `git apply -R` | 0 | Probe printed `baseColor (255,255,255,255)` and `emissive/normal/metallicRoughness/occlusion (0,0,0,255)`, all 1x1; patch reverted afterwards (`artifacts/m1-i/correct-probe-empty-bytes-from-patch.log`) |

Checks 1-3 were re-run on the final source after every mutation was reverted
(the `final-*.log` files are that run; earlier `build-0*`/`tests-0*` logs are the
implementation iterations). The 24 pre-existing CPU contracts and all 7 CTest
entries still pass.

## Review corrections applied

1. `caseDecodeErrorFallbacks` now binds the signature-only PNG, the truncated
   PNG and the truncated JPEG materials to all five roles (fixture change) and
   tables through all five decoder roles. Before each decode, the loaded fixture
   is asserted to select the intended corrupt image through the role's own
   texture/image slot (`authoredRoleImage`), so the fallback can no longer come
   from an absent reference. Mutations 3 and 4 above prove that assertion is
   load-bearing. This closes the previous false coverage; metallic-roughness and
   occlusion corrupt decoding are now exercised.
2. `caseDecodedTextureRetention` now decodes normal from
   `kMatAllRolesBufferViewA`, which selects the same bufferView PNG A, and
   re-verifies both earlier `CPUTexture` values after the later factored decode.
3. The empty-byte white/black expectation is no longer asserted by the contract.
   Its document, docstring and expectation moved into the standalone
   `artifacts/m1-i/empty-byte-probe.patch`; the bounded invalid-selection guard
   probes stay. The guide and `PROBES.md` no longer claim the contract pins all
   three fallback families.
4. The contract now sets `PHLOSION_GLTF_DUMP_TEXTURES`,
   `PHLOSION_GLTF_DEBUG_ALL` and `PHLOSION_GLTF_DEBUG_MATCH` to `0` through a
   small scoped RAII override that restores the previous values on every exit
   path, with no global working-directory mutation. Checks 8 and 9 are the
   ambient-environment proof and its inverse.
5. The guide's missing-emissive conclusion now follows the actual legacy OpenGL
   `Model` path (`ModelFastGltfLoader.cpp:452` upload of the black CPU emissive
   texture, `ModelAnimation.cpp:162` bind, `assets/shaders/model/model.frag`
   `srgbToLinear(u_EmissiveTex) * u_EmissiveFactor`), is labelled source-derived
   rather than captured or three-API evidence, keeps the world backends as a
   separate conditional/unverified path, and links the primary glTF material
   specification.
6. Accuracy cleanup: one external PNG file, three payload sizes (2x2/3x2/2x1),
   the corrected texture/image index overview (texture 18 -> image 10), the
   corrected mutation-2 direction, per-role corrupt bindings in the case table,
   and the scoped phrasing plus logged-not-asserted `fastgltf::validate` verdict
   in both documents.

## New proof

- Genuine PNG payloads reproduce dimensions, channel order, alpha and row order
  exactly through the external-relative-URI, image-data-URI,
  bufferView-in-external-buffer and GLB-bufferView routes, and the five roles
  each select their own authored image.
- A genuine baseline JPEG decodes with exact dimensions, exact opaque alpha and
  colour inside a three-per-channel tolerance against a literal flat patch.
- Opaque, binary-cutout and intermediate-alpha images decoded by the production
  decoder drive OPAQUE/MASK/BLEND through `resolveMaterialRenderInfo`, and an
  authored base-color alpha factor is what flips an otherwise opaque result back
  to BLEND.
- Bounded negatives distinguish two contract-pinned fallback families: no usable
  texture reference, and present-but-undecodable bytes (role-specific
  placeholders, now verified for all five roles against a role-slot assertion
  that the corrupt image is the one selected). A missing external image fails at
  `tryLoad`. The empty-byte family is reproduced only by the retained artifact
  patch, not asserted by the contract.
- Decoded textures retain their own pixels after later decodes of the same
  source image with a different role and a different factor, re-checking both
  earlier results.

## Evidence limits, gaps and risks

- No native, GPU, editor-preview or three-API evidence is produced or claimed,
  and none is required because no production or rendering code changed. GPU
  sampler state, UV binding, texture filtering, material appearance and
  cross-API parity remain unverified.
- `Model::loadGLTFFast` still decodes and uploads only base color and emissive
  textures (`src/engine/render/gltf/ModelFastGltfLoader.cpp:440-452`). The
  normal, metallic-roughness and occlusion helpers now have decoder-level
  evidence but no engine runtime call site in this tree. Five passing decoder
  tests are not five-map rendering.
- Base color factor application is the existing encoded-byte multiplication
  (`src/engine/render/gltf/ModelFastGltfTextures.cpp:515`), recorded as a named
  legacy policy. It is not linear-light factor application and no color-space
  redesign is included.
- Missing emissive texture still decodes to opaque black, and on the legacy
  OpenGL `Model` path that black texture is uploaded into
  `Submesh::emissiveTexID` (`src/engine/render/gltf/ModelFastGltfLoader.cpp:452`),
  bound at `src/engine/render/ModelAnimation.cpp:162`, and multiplied by
  `u_EmissiveFactor` in `assets/shaders/model/model.frag`, so a factor-only
  emissive material contributes no emission there. That is a source-derived
  consequence for one path, not a captured GPU result and not three-API
  evidence; glTF requires a white sampled emissive multiplier
  (https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#materials). The
  world-scene backends are separate paths that multiply their own sampled
  emissive texture by the factor, and the OpenGL world draw even supplies a
  white fallback when the emissive texture id is 0, so any claim that the black
  CPU result reaches a world adapter is conditional and unverified.
- Empty or unavailable encoded bytes remain inconsistent with the role-specific
  decode-error placeholders (`src/engine/render/gltf/ModelFastGltfTextures.cpp:453`).
  The contract no longer asserts those values; the standalone
  `artifacts/m1-i/empty-byte-probe.patch` reproduces them (base white, other
  roles black) with its log, so the inconsistency is documented and testable
  without becoming a desired contract. This is a production-fix candidate
  outside this pass.
- The engine loader materializes external-file and data-URI images into the
  asset (`sources::Array`), so the decoder's own URI/file and inline base64
  branches are not covered by this path. In the selected invalid-reference
  probes, deliberately out-of-range texture/image/sampler references are
  accepted by `tryLoad` and reach the decoder guards, while `fastgltf::validate`
  rejects those probe documents as `InvalidGltf`; the contract asserts the
  loader acceptance and decoder result and only logs the validator verdict.
- KTX2, DDS and WebP remain enabled parser extensions only. No compressed
  payload is decoded or claimed.
- The JPEG expectation is one authored flat patch with a justified
  three-per-channel tolerance; it is not a general JPEG fidelity claim.

## Astra integration decisions

- Schedule empty-byte fallback consistency and factor-only emission with the
  next bounded material phase, after establishing the intended runtime path
  and its required native three-API verification. They are not fixed here.
- Keep the small per-probe observation lines as evidence of the loader boundary.
- M1-I is left uncommitted for the next review/commit instruction.

## Next checkpoint

No unfinished work in this bundle. Raw evidence is retained under
`artifacts/m1-i/` (`PROBES.md`, `final-*.log`, mutation and defect-probe logs,
the fixture images); the full logs are not inlined here.
