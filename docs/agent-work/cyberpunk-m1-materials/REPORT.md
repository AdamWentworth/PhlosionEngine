# M1-J worker report: five-map native material verification and empty-image fallbacks

STATUS: accepted by Astra; included in the M1-J commit.

Task: `/root/cyberpunk_m1_materials`, M1-J.
Workspace: `D:/Projects/Phlosion/PhlosionEngine`, baseline
`a037f8833cb3a509b980b825fc5833565eac4784`. Worker handoff staged, committed or pushed nothing;
no sub-agents, no router or sandbox changes, no other repository touched, and no
production renderer, shader, `Model`, cache or ABI change. The production
fallback correction and its old-branch regression are preserved unchanged.

## What this correction changed

| # | Item | Change |
| --- | --- | --- |
| 1 | Staging ownership and hermetic decode | `examples/material_fixture/MaterialFixtureAssets.h/.cpp`: the staging directory is now an RAII owner (`FixtureStagingDirectory`) reserved with atomic `create_directory` and a validated prefix, movable and non-copyable, releasing only its own directory on every exit path. `decodeFixtureTextures` stages through that owner, so a failure or exception removes the partial directory, and the returned `FixtureTextureSet` shares ownership through `std::shared_ptr<const FixtureStagingDirectory>` (`staging`), so copies keep the inputs alive and the last owner performs final cleanup. Nothing is glob-deleted. The decoder diagnostic flags `PHLOSION_GLTF_DEBUG_ALL`, `PHLOSION_GLTF_DEBUG_MATCH` and `PHLOSION_GLTF_DUMP_TEXTURES` are scoped off with a restoring RAII guard for the duration of a decode |
| 2 | PNG writing and oracle placement | The custom CRC/Adler/deflate encoder (~140 lines) is gone; the sample writes its fixed 8x8 role maps with the existing `stb_image_write` dependency and checks the writer's return value. `PatchSample`, `fixturePatchSample`, the role placeholders, `PatchHalf`, the bounded role metadata contract and the expectation-derived content hash moved out of `examples/` into `tools/material_fixture` (`MaterialFixtureGuard.h/.cpp`). The sample now owns only runnable content: authored maps, glTF documents, staging, decoding, factors, origins and the cache identity it submits |
| 3 | Whole-image cross-host parity | `tools/material_fixture/run_capture_matrix.ps1` captures the editor first, derives the validated viewport dimensions from that capture's handshake and launches the standalone host with `--width/--height` set to exactly those dimensions. The guard requires nine runtime/editor pairs and compares them as whole images with the same approved limits as the 18 cross-API pairs (MAE 0.01, RMSE 0.08, changed ratio 0.05, tolerance 8); the mean-only tolerance-12 acceptance is deleted |
| 4 | Expected content and lit coverage | Every interior pixel of every diagnostic patch half must now match its literal authored value within three counts, in addition to the half-mean check, so a mean-preserving corruption is rejected (`same-mean-corruption` synthetic defect). The composed lit patch is compared specifically between the authored case and each fallback case (`compareLitRegions`, minimum changed ratio 0.5), and the synthetic pristine captures now differ per case (authored lit stand-in versus dark fallback stand-in) so a `frozen-lit-across-cases` capture fails even with correct diagnostic tiles |
| 5 | Matrix evidence and path correctness | `tools/material_fixture/MaterialFixtureGuardMain.cpp`: editor entries must carry a metrics path that exists and parses with `renderer.backend` and a bounded `capture.frames`; manifest `capture_frames` for both hosts and `editor_frames` must be bounded integers; exactly one screenshot `WROTE` record per entry is required, and the recorded observed frame, screenshot backend and screenshot size are validated against the raw log and the PNG. Every entry artifact/metrics path and both output files are resolved with a canonical component-aware containment check against the run directory before anything is read or written, so traversal, sibling-prefix and outside `--out`/`--sheet` targets are refused without creating a file |
| 6 | Handshake schema and identity | Integer fields (logical frame, surface, asset count, role size/bytes/color space/samplers, material mode/presence, patch debug view, rect elements) are validated as bounded JSON integers instead of double-to-int casts; factors must be finite; role metadata is validated against a bounded literal contract including the exact size, sampler enums, first texel, the content hash re-derived from the guard's own authored role pixels, the exact `fixture/mat/<case>/<role>/<hash>` key and a matching cache key. Unknown patch names, malformed array elements and wrong types produce diagnostics (the validator is wrapped so a malformed document cannot raise), the asset directory check is component-aware, and existence is no longer required because RAII owns cleanup |
| 7 | Requested logical frame | `MaterialFixtureRuntimeMain.cpp` rejects a `--logical-frame`/`PHLOSION_MATERIAL_FIXTURE_LOGICAL_FRAME` that does not match the authored case frame instead of silently ignoring it |
| 8 | Docs | `docs/GLTF_TEXTURE_VERIFICATION.md` no longer claims three fallback outcomes or that no production code changed, and no longer delegates UV/filter/color semantics generally to the material guide; `docs/MATERIAL_VERIFICATION.md` documents the matched-size parity, per-pixel interior checks, lit-region check, staging ownership, evidence requirements and the tightened limits, with lit values described as measured interior means. Trailing whitespace in `MaterialFixtureGuard.cpp` removed |
| 9 | Second correction: exact numeric identity and doc accuracy | Astra reproduced two false PASSes on the previous bundle. `MaterialFixtureGuard.cpp` compared the role `content_hash` through a `uint32` cast, so expected + 2^32 truncated into an accepted identity; it now compares exactly in `int64` against the expected 32-bit hash promoted, rejecting anything outside the unsigned 32-bit range before any narrowing. `MaterialFixtureGuardMain.cpp` read `screenshot_size` elements with `get<int>()`, which does not reject a value of the correct width plus 2^32; both elements now go through a bounded 64-bit array helper (`jsonBoundedArrayElement`) shared with the other integer paths. Regression cases cover expected + 2^32 and `UINT64_MAX` for both fields, and the audit found no remaining unchecked narrowing of JSON integers in either file (patch rects are normalized doubles and stay doubles). Documentation accuracy: the normal/roughness/occlusion/emission equations were removed from `docs/MATERIAL_VERIFICATION.md` in favour of a bounded, source-observed description; composed lit values are now stated as measured interior means of this fixture, and both the guide and this report say the handshake only records the factors the sample read from the loaded glTF and does not prove `WorldTextureData` submission or GPU scalar application |

## Verification

All commands ran natively in PowerShell against `build-presentation` Debug
(`PHLOSION_WARNINGS_AS_ERRORS=ON`, editor, tests and all fixtures ON).

| # | Command | Exit | Salient result |
| --- | --- | --- | --- |
| 1 | `cmake --build build-presentation --config Debug --parallel 8` (re-run after the last source edit) | 0 | Every enabled Debug target, warnings as errors, no warnings (`artifacts/m1-j/correction-final-build.log`) |
| 2 | `build-presentation\Debug\PhlosionEngineTests.exe` | 0 | 26 of 26 CPU contracts PASS, including the extended `material_fixture_contract` (staging ownership, ambient diagnostic scope, per-pixel interior verdicts, mean-preserving corruption, lit-region cross-case, expanded handshake negatives) |
| 3 | `ctest --test-dir build-presentation -C Debug --output-on-failure` on the final binaries | 0 | 8 of 8 CTest entries in 211.1 s; the material harness runs 118.8 s with the added negatives (`artifacts/m1-j/correction-final-ctest.log`) |
| 4 | `tests/material_fixture/test_fixture_harness.ps1 -GuardExe ...` | 0 | All adversarial checks pass, including the new traversal, sibling-prefix, outside-output-with-sentinel, missing editor metrics, missing required field, recorded frame/backend/size mismatch, fractional, oversized, unknown-name, wrong-type, invented-hash, invented-key, same-mean-corruption and frozen-lit negatives; the pristine matrix passes before and after every mutation (`artifacts/m1-j/correction-harness-02.log`) |
| 5 | `tools/material_fixture/run_capture_matrix.ps1 -RunId m1-j-final3 -ExpectedBaseline a037f883...` on the final binaries (after the numeric hardening) | 0 | Fresh 18 native captures; all 18 at the matched 845x532 viewport. Guard: 18 entries, 18 cross-API pairs, 9 runtime/editor whole-image pairs, 12 lit-region cross-case comparisons, 18 cross-case comparisons, no failures (`artifacts/m1-j/overflow-matrix.log`, `matrix/m1-j-final3/{manifest.json,matrix.json,guard.log,contact-sheet.png}`). The earlier `m1-j-final2` and `m1-j-corrected` runs remain in place, untouched, as superseded history |
| 6 | Interior per-pixel deviation measured over all 18 captures | 0 | Maximum deviation from the literal expected value is 2 of 255, inside the three-count interior tolerance and far below the 30-count same-mean corruption the harness injects |
| 7 | Ambient diagnostic proof: `PHLOSION_GLTF_DEBUG_ALL=1`, `PHLOSION_GLTF_DUMP_TEXTURES=1`, `PHLOSION_GLTF_DEBUG_MATCH=material_fixture` around a runtime capture | 0 | Capture succeeded, no `debug/gltf_textures` output anywhere under the engine root, and the staging-directory count under `%TEMP%` was unchanged before and after (68 before, 68 after; the 68 are pre-correction leftovers retained untouched) |
| 8 | `MaterialFixtureRuntime.exe --case=absent-maps --logical-frame=99` | 1 | `material fixture logical frame 99 does not match the authored frame 120 for case absent-maps` |
| 9 | Recorded-binary revalidation for the retained archive (24 capture) and animation (36 capture) matrices: SHA-256 and size of every recorded binary against the final files, re-run after the final rebuild | 0 | All eight recorded identities still match byte-for-byte, so those runs are retained with explicit proof rather than reused blindly (`artifacts/m1-j/correction-retained-hash-check.log` plus the final-build re-check) |
| 10 | Old-branch negative (from the accepted bundle, unchanged by this correction) | 1 | Restoring `return (kind == TextureKind::BaseColor) ? white : black;` still fails the checked-in contract on the normal role; not re-run because neither the production branch nor that test changed (`artifacts/m1-j/old-branch-tests-01.log`) |
| 11 | Astra's two retained false-PASS reproductions re-run against the corrected guard | 1 | `astra-overflow-handshake.json`: was `handshake ok=true failures=0` in Astra's log, now `ok=false` with `handshake role 'baseColor' content hash is missing or does not match the independently authored role pixels`. `astra-overflow-size.json`: was a complete 18-entry `ok=true`, now `entry screenshot_size is not a bounded integer pair` (`artifacts/m1-j/overflow-rejection.log`) |
| 12 | Full Debug build, 26 CPU contracts, CTest and the material harness after the numeric hardening | 0 | `overflow-final-build.log`, 26/26 contracts including the new expected + 2^32 and `UINT64_MAX` handshake negatives, 8/8 CTest entries in 223.7 s with the extended harness (131.4 s), and the archive/animation retained-run hash recheck still reports zero mismatches (`overflow-final-ctest.log`) |

## Retained but not accepted

- `artifacts/m1-j/matrix/m1-j-final/` and `matrix/m1-j-native/` are earlier
  bundle's runs. Their manifests are now stale against the final guard binary
  (the guard correctly refuses them) and their runtime/editor pairs were not
  size-matched, so they are retained only as history.
- `artifacts/m1-j/matrix/m1-j-final2/` is the accepted-in-principle run that
  still admitted forged numeric evidence. Its manifest, matrix and Astra's
  `astra-overflow-*` probes are left exactly as they were and now serve as
  stale-hash and overflow negatives; only new `astra-overflow-*-result.json`
  files were added beside them. Nothing was rewritten, and the accepted matrix
  for this correction is `matrix/m1-j-final3`.
- `artifacts/m1-j/matrix/m1-j-final/astra-review-missing-editor-metrics*` is
  Astra's probe showing the old guard accepted a manifest with every editor
  metrics path removed. The corrected guard rejects that shape (harness negative
  `missing-metrics-field`).
- The 68 pre-correction `phlosion-material-fixture-*` staging directories under
  `%TEMP%` are the leak this correction removes; they are left in place rather
  than glob-deleted.

## Decisions requiring Astra

1. The runtime/editor parity method is now whole-image comparison at matched
   viewport dimensions, driven by the editor capture. It measures pixel-identical
   results for all nine pairs, so no further tolerance or resampling policy is
   needed; if the editor viewport ever changes shape between runs the runner now
   fails instead of comparing mismatched sizes.
2. Authored tolerances: 10 counts for an sRGB role-view mean, 14 for a linear
   role-view mean, 3 for every individual interior pixel. The measured worst case
   is 2, so the interior tolerance could be tightened to 2 if Astra prefers the
   margin to sit at the measured bound.
3. Synthetic stand-ins remain class-level, not measured goldens: the pristine
   synthetic fallback lit region is a dark stand-in and the authored one a
   light stand-in, chosen so the required cross-case distinction is exercised
   without teaching the guard a captured value.
4. The numeric hardening is deliberately exact: identity values are compared as
   integers and never narrowed, while patch rectangles stay normalized doubles.
   If Astra wants a stricter policy (for example rejecting any JSON number that
   is not an integer everywhere, including rects), that is a contract change, not
   a bug fix.
5. `docs/TEST_PLAN.md` does not exist in this repository; `docs/README.md`,
   `docs/VERIFICATION.md` and other shared indexes remain root-owned and were not
   touched.

## Outstanding risks and limits

- Bounded material qualification only: three cases, one eight-patch layout, one
  shared UV set, identity transforms, `baseColorFactor` 1 and default wrap. The
  raw debug views do not verify that the scalar factors were applied. The
  handshake records the scalar factors the sample read from the loaded glTF, and
  the `WorldTextureData` assembly was code-reviewed; neither the handshake nor
  the captures independently prove submission or GPU scalar application, and the
  composed lit patch is checked for presence, difference from the background and
  the raw base-colour patch, and cross-case difference, never against a PBR
  oracle. Normal, roughness, metallic, occlusion and emission equations are not
  qualified here.
- Dated local Windows evidence on one GTX 1070 in Debug; not a CI GPU
  qualification.
- Known deferred items are unchanged: encoded-byte base-colour factor
  multiplication and the factor-only emissive behaviour on the legacy `Model`
  path. Emission factor is zero in both fallback cases.
- The staging guard scopes the decoder diagnostics for the sample and the tests
  assert restoration; other engine paths that decode glTF are untouched.

## Next checkpoint

Astra accepted the corrected patch and final contact sheet. Final evidence is
under `artifacts/m1-j/`: `overflow-final-build.log`, `overflow-final-ctest.log`,
`overflow-harness.log`, `overflow-matrix.log`, `matrix/m1-j-final3/*`,
`astra-final-audit.json` and `astra-final-negatives.log`. Earlier corrected runs
are superseded. The root audit confirms all 12 final binary hashes/sizes across
the material matrix and retained archive/animation runs. Three-count interior
tolerance and normalized rectangle coordinates are accepted as designed.
Root integrated shared documentation only; see [CHECKPOINT.md](CHECKPOINT.md)
for the exact next action and limits. No unfinished implementation remains
within this bounded pass; the larger M1 roadmap remains partial.
