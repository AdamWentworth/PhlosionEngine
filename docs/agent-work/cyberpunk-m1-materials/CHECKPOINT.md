# M1-J checkpoint

Status: accepted by Astra; included in the M1-J commit with this checkpoint.

- User authorized committing M1-I and proceeding. Committed
  a037f8833cb3a509b980b825fc5833565eac4784, Verify glTF texture decoding and
  material inputs. All 13 accepted paths committed; staged whitespace passed;
  tree clean after commit; nothing pushed. Prior accepted verification remains
  valid because no code changed before committing.
- [PLAN.md](PLAN.md) defines the next bounded material pass: native five-map
  evidence plus consistent empty-image fallbacks. Existing shared renderer is
  reused; legacy material color/emission corrections are explicitly deferred.
- Root Astra/xhigh; native astra_flash_builder pinned DeepSeek V4.1 Flash/high.
  Static doctor is ready, fingerprint
  237343dd9547ecc87450fada2a20e801718b62afe11628a371d707320f7d1a26 unchanged.
  Optional loopback catalog GET failed with HTTPError; no inference requested,
  no settings changed. Reuse verified M1-D host/router metadata and available
  native role; static/catalog checks alone do not establish fresh inference.
- No engine/ancestor AGENTS.md or project routing override found. The user's
  standing native three-API/editor parity requirement is in PLAN acceptance.
- Worker `/root/cyberpunk_m1_materials` completed the initial bundle, one
  consolidated correction and a narrow second correction; child interrupted
  after each completion. No active writer remains. Root reviewed the actual
  tracked/new implementation, tests, guard, docs, logs and native contact sheets.
- Consolidated correction added owned temporary assets, existing PNG-writer
  reuse, verification-tool oracles, interior pixel checks, targeted lit-region
  comparisons, mandatory bounded evidence and same-size whole-image host parity.
  Root reproduced a missing-editor-metrics false PASS before the correction.
- A second correction used the skill's high-assurance exception: root probes
  proved that oversized hash/width metadata still passed by narrowing to 32 bits.
  This was a material flaw in the new verification boundary, not a style request.
  Exact hash comparison and range checks before conversion now reject those
  values; CPU/harness regressions include expected + 2^32 and UINT64_MAX.

## Accepted result and evidence

One production branch now uses existing role-specific fallbacks for unavailable
encoded images. The permanent five-role CPU regression fails under the old
branch. Existing base/emissive behavior, shaders, renderer backends, Model cache
and plugin ABI are unchanged. Runnable sample content stays in examples,
verification utilities in tools and automated checks in tests.

Final ignored evidence under `artifacts/m1-j/`:

- `overflow-final-build.log`: full Debug build with warnings as errors, no warnings.
- `overflow-final-ctest.log`: 8/8 CTest entries, 223.66 seconds; 26 CPU contracts
  are also confirmed in the final CTest raw log.
- `overflow-harness.log`: adversarial matrix/content/path/identity checks pass.
- `matrix/m1-j-final3/`: 18 native captures at 845x532, 18 cross-API pairs under
  unchanged limits, nine pixel-identical whole-image runtime/editor pairs and
  twelve authored-versus-fallback lit-region comparisons. Diagnostic interior
  deviation is at most 2 with tolerance 3. Astra inspected the final contact sheet.
- `old-branch-tests-01.log`: permanent regression rejects the old normal fallback.
- `astra-final-audit.json`: root independently rehashed all 12 final binaries
  across material, archive and animation manifests; SHA-256 and size all match.
  Retained archive 24 and animation 36 captures therefore remain valid evidence.
- `astra-final-negatives.log`: root independently confirmed rejection of the
  oversized hash and missing editor metrics with the final guard. The harness
  also rejects oversized screenshot dimensions and malformed/missing metadata.
- Earlier material matrices and root probes are retained for history, not accepted
  as final evidence. Old leaked staging directories were not broadly deleted.

Root integrated the readiness roadmap, verification record, docs index and M1-I
commit status. Integration is documentation-only; accepted final binaries and
their verification remain unchanged. Three-count interior tolerance is accepted;
no threshold tightening to a measured device-specific bound is required.

## Limits and next action

M1 remains partial. This is one bounded public five-map layout on Windows/GTX1070,
not a production character adapter or general PBR qualification. The handshake
records decoded factors; the WorldTextureData assembly was code-reviewed, while
GPU scalar application remains unqualified. Shared UVs, identity transforms,
baseColorFactor 1 and default wrap bound the sample. Legacy encoded-byte base
factor application and factor-only emissive loss, per-role UVs/transforms,
compressed textures, private characters and environment systems remain.

The user authorized committing M1-J. The commit is titled
`Verify native material maps and fix empty-image fallbacks`; final code and
accepted verification are unchanged, with only commit-status documentation
updated before staging. Nothing is pushed.

M1-J was committed as `cc600e1f9722b7da7c198102a26763a469e8ee4c`; the tree was
clean after the commit. The user then asked Astra to choose the responsible next
step rather than prioritize lighting simply because it is visually appealing.

The earlier point-light proposal is superseded as the immediate next step.
Astra recommends bounded engine-facing M1 closure: establish and qualify the
supported consumer asset-to-scene path with a public animated character and
small environment, correct required material semantics on that path, and verify
loading/replacement/unload ownership and native runtime/editor parity. See the
[roadmap's next-work checklist](../../CYBERPUNK_ENGINE_READINESS.md#recommended-next-work-after-m1-k).
Then prioritize M2's walkable scene; a lighting feature may follow before M5's
full district qualification. Private game-content qualification stays separate.
That integration objective is now implemented and accepted as
[M1-K](../cyberpunk-m1-integration/ACCEPTANCE.md), included in the commit
`Integrate owned glTF scenes with shared rendering`. It qualifies
the synchronous CPU scene/shared renderer path; asynchronous activation and
GPU lifetime/residency remain. M1 is still partial. See the
[current checkpoint](../cyberpunk-m1-integration/CHECKPOINT.md).
