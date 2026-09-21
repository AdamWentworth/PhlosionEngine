# M1-I checkpoint

Status: accepted by Astra; M1-I committed as a037f8833cb3a509b980b825fc5833565eac4784.

- M1-H committed as 4ab3ac7e06e912cc3668fa0f69655f5dc4b24f84,
  `Preserve and sample glTF cubic animation curves`. Tree clean after commit;
  staged whitespace check passed, nothing pushed. Earlier accepted checks remain
  valid; only commit-status documentation changed before staging.
- User authorized committing accepted work and proceeding to the next pass.
- [PLAN.md](PLAN.md) defines CPU texture-decoding verification and explicit
  limits. Production rendering/decoder changes are excluded from this bundle.
- Root is Astra/xhigh. Doctor is static-ready, pinned native astra_flash_builder
  is DeepSeek V4.1 Flash/high; configuration fingerprint
  237343dd9547ecc87450fada2a20e801718b62afe11628a371d707320f7d1a26.
  Reuse M1-D runtime provider evidence; no fresh paid smoke or settings changes.
  No applicable engine/ancestor AGENTS.md or project config override was found.
- Root owns shared planning/integration docs; worker owns bounded tests/guide.
- Worker: /root/cyberpunk_m1_textures, native astra_flash_builder. Both handoffs
  completed and child interrupted after completion; no active writer remains.
- Astra reviewed the tracked changes and both new test files against 4ab3ac7,
  including fixture bytes, literal expectations, decoder calls and failure paths.
  One consolidated correction was completed and reviewed. Production sources,
  shaders, assets, dependencies and existing rendering guards are unchanged.

## Accepted result and evidence

The existing loader and five CPU texture helpers now have real PNG/JPEG tests:
external/data URI and padded bufferView routes (including GLB), dimensions,
channels/orientation/alpha, per-role selection, sampler metadata, legacy factors,
material-alpha integration, safe invalid selections, corrupt payloads and result
ownership. See [REPORT.md](REPORT.md) and
[the verification guide](../../GLTF_TEXTURE_VERIFICATION.md).

Corrections made the corrupt-image cases reach the intended decoder branch for
all five roles, made retention reuse the same source image and check both earlier
results, moved the empty-byte inconsistency out of the permanent assertions,
and scoped decoder debug settings off. Documentation now follows the actual
legacy OpenGL Model emissive path and separates world-backend evidence.

Final evidence under ignored artifacts/m1-i, inspected by Astra:

- final-build.log: full VS2026 Debug build, warnings as errors, no warnings.
- final-tests.log: 25/25 CPU contracts, including gltf_texture_import_contract.
- final-ctest.log: 7/7 CTest entries, 86.70 seconds.
- sensitivity-mutation-01-pixel.log and sensitivity-mutation-02-sampler.log:
  deliberately wrong expectations fail the new contract; both reverted.
- correct-mutation-01-missing-role-binding.log and
  correct-mutation-02-wrong-corrupt-image.log: missing/wrong authored bindings
  fail before decoder invocation; both reverted.
- env-proof/: final guarded run produces zero texture dumps under ambient debug
  settings; inverse mutation produces 32. Astra independently counted both.
- empty-byte-probe.patch and correct-probe-empty-bytes-from-patch.log: safe
  standalone reproduction of white base / black other roles. Patch is absent
  from final source; Astra's git apply --check passes against the final file.
- PROBES.md: authored image values, six encoded payloads and exact reproductions.
- Final integration: 219 local Markdown links resolve; tracked/new-file
  whitespace checks pass and all 13 changed paths stay within the approved
  test/registration/documentation scope.

Astra integrated the roadmap, documentation index and verification record. The
worker's final suite is sufficient: integration changed documentation only.
No new native captures are required or claimed for this CPU-only pass.

## Limits and next action

M1 is still partial. Decoder evidence is not rendered material qualification.
Base factors are multiplied in encoded space; missing emissive maps lose
factor-only emission on the legacy OpenGL Model path according to source;
empty image bytes use inconsistent placeholders. Runtime Model loading calls
only base/emissive helpers. Independent per-role UVs/transforms, compressed
formats, GPU sampler behavior and private characters remain unqualified.

Keep the small probe observation lines. Schedule the known production gaps in
the next material phase after establishing its shared runtime/consumer path;
do not fix one legacy path and imply three-API coverage. Any affected rendered
behavior needs matching expected-content checks on native OpenGL/Vulkan/D3D12
in runtime and editor. Existing meshes/materials must be reused where suitable.

The user authorized commit/proceed and M1-I was committed as `a037f88`,
`Verify glTF texture decoding and material inputs`. All 13 accepted paths were
committed; staged whitespace passed, the tree was clean and nothing was pushed.
M1-J subsequently corrected the empty-byte fallback and added bounded five-map
native evidence; see its [checkpoint](../cyberpunk-m1-materials/CHECKPOINT.md).
The historical M1-I evidence and limits above remain scoped to that commit.
