# M1-H checkpoint

Status: accepted by Astra; user authorized committing M1-H and proceeding.

- M1-G committed as afaeb1ec470434fe3da10f7c5f3c8238b2fc5068; tree was clean.
- User request: commit accepted work and proceed to the next pass.
- M1-H scope and acceptance: [PLAN.md](PLAN.md).
- Routing: current root Astra/xhigh; installed astra_flash_builder is pinned to
  DeepSeek V4.1 Flash/high. Static doctor passed this turn with configuration
  hash 237343dd9547ecc87450fada2a20e801718b62afe11628a371d707320f7d1a26.
  Reuse M1-D host/router runtime evidence; static doctor is not a new live test.
- Root owns plan/checkpoint and shared integration docs. No staging, commits or
  pushes by worker. M1-H will remain uncommitted after acceptance.
- Worker: /root/cyberpunk_m1_cubic, native astra_flash_builder. Both handoffs
  completed and child interrupted after each; no active writer remains.
- Initial evidence: full build, 7/7 CTest, 24 CPU contracts, 36 native animation
  captures and 24 archive captures pass; original animation captures are pixel
  identical to M1-G. Production helper/cache wiring and cubic math reviewed.
- Corrections: return exact/clamped keys directly before tangent arithmetic;
  cover finite-tangent overflow and missing non-finite input defenses; isolate
  each CLI negative by restoring its own files; correct mathematical comments
  and historical/current coverage wording. Full request is in worker followup.
- Corrections are complete and reviewed against the actual diff, including
  untracked codec/test files. LINEAR/STEP paths retain their behavior; cubic
  Hermite, raw importer triplets, v10 identity gate and real cache codec are
  accepted. Root integrated the roadmap, guide/index and verification record.

## Final evidence and acceptance

All paths below are under ignored artifacts/m1-h:

- build-full-02.log: warning-clean full VS2026 Debug build.
- ctest-03.log: 7/7 CTest entries; tests-03.log: 24/24 CPU contracts.
- harness-02.log: independent linearized-cubic negatives and restored pristine
  complete matrix pass; extreme-tangent and malformed-input contracts pass.
- m1h-run02/: 36 native runtime/editor captures across OpenGL/Vulkan/D3D12,
  zero failures. Worst normalized MAE/RMSE 0.0000074/0.00039; limits unchanged.
- compare24-run02/: original 24 captures pixel-identical to M1-G.
  compare-run02-vs-run01/: all 36 pixel-identical across the review correction.
- archive-regression/m1h-archive-final/: 24 passing final-build archive captures,
  run by Astra during integration; archive-final.log records exit 0.
- final-provenance.json: Astra independently checked eight binary identities
  across both final manifests, zero mismatches. Contact sheet inspected.
- Tracked/new-file whitespace and 198 local Markdown links passed review.

Astra expanded the evidence check for a concrete provenance failure: all four
older archive hashes differed from the final binaries despite the archive guard
returning PASS. Source inspection confirmed that older guard validates digest
format and file size, not content hashes. The older archive evidence was
superseded with fresh captures and explicit SHA-256 checks; no second worker
implementation cycle or unrelated harness refactor was introduced. Future
archive evidence reuse must include an actual hash comparison.

Coverage is one public two-joint world-mesh-textured rig on Windows/GTX 1070
Debug. Native cubic coverage is one two-key Z-rotation curve sampled twice;
multi-segment translation, two-key scale, malformed data and exact-key edge
cases rely on CPU contracts. Full Model cache integration, legacy Model GPU
drawing, private characters, morphs, blending, events, managed attachments,
instanced/indirect skinning and performance remain unqualified or future work.

The user has now authorized the next commit/proceed instruction. Commit accepted
M1-H, then define a bounded M1 material/texture or production-character
qualification pass from current source evidence. Do not recreate capabilities
already present or conflate optional typed formats with required game readiness.
