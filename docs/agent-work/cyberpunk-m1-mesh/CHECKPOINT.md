# M1-E checkpoint

Status: Accepted by Astra after one consolidated correction cycle; commit authorized.
Date: 2026-09-20.
Baseline: engine main `68aed76373c169494d9238d267cd0e50cd885ff2`.

The user authorized checking M1-D's semantic placement, committing it and moving
to the next pass. M1-D is now committed as 68aed76 after the example/tools/tests
separation and successful rebuild, six CTest entries and 24 native captures.
Nothing pushed. M1-E implementation and planning changes are uncommitted.

Read PLAN.md for the exact typed static mesh schema/API, consumer migration,
ownership and acceptance. Static .phmesh belongs in engine_core; the example
consumes it, verification tools remain in tools, and automated checks in tests.
No game/private source import or production renderer/editor-host changes.

Root is Astra. Doctor remains static-ready with unchanged configuration hash
237343dd9547ecc87450fada2a20e801718b62afe11628a371d707320f7d1a26 and the pinned
astra_flash_builder role (DeepSeek V4.1 Flash/high). Reuse the real host/router
route verification recorded in the M1-D checkpoint; no new paid smoke test.

Worker: `/root/cyberpunk_m1_mesh`, native astra_flash_builder, fresh initial
context. Initial implementation returned ready_for_review; Astra inspected the
production files, fixture migration, tests and build/CPU/GPU evidence against
the recorded baseline. Initial evidence: six CTest entries, eighteen C++
contracts, Core-only link proof and 24 native captures in artifacts/m1-e/.

Astra requested one consolidated correction cycle: reject invalid UTF-8 in
authored names through the bool/diagnostic contract; hash each mesh once and
enforce exactly one material binding per slot, with a valid multi-slot consumer
regression; preserve tangents in the renderer adapter; anchor the golden payload
test to literal known bytes. Check submesh addition for host-size overflow.
Architecture and semantic folder placement are sound. All findings were fixed
and re-reviewed: strict UTF-8 rejects malformed names without rewriting bytes;
multi-slot validation/activation succeeds and duplicate/missing bindings preserve
active A; the actual fixture adapter forwards tangents; literal golden bytes
anchor the first vertex and complete index payload; range arithmetic is checked
before addition. Both specification and quality review passed.

Final evidence: artifacts/m1-e/build-full-03.log and ctest-03.log (warning-clean
Debug, six CTest entries/eighteen C++ contracts); link-proof-{build,run}-02.log
(Phlosion::Core only); m1e-run03/{manifest,matrix}.json and guard.log (24 native
captures, zero failures, unchanged guards); m1e-run03-vs-m1d/ (all 24 captures
pixel-identical to M1-D reorg-run02). Astra independently matched all four final
capture binary SHA-256 hashes. Earlier runs are retained; run03 supersedes them
after the worker reverted deliberate mutation checks and rebuilt. The worker
was interrupted after each completed handoff for host bookkeeping.

Root integrated the documentation index, asset-architecture implemented subset,
verification record and readiness progress. No source changes after the final
worker evidence. Only PhlosionEngine has task-owned edits; nothing staged or
pushed. M1-D is committed; M1-E remains uncommitted.

Limits: one geometry level, static triangles and label-only material slots;
no skin/animation/LOD/compression/material-payload/importer/cooker/Model migration
claim. Multi-slot and tangent forwarding have CPU evidence; the visual fixture
still has one slot per mesh and no tangent-sensitive material. GPU qualification
is one Windows/GTX 1070 Debug host. Byte/count caps are not total peak-memory caps.

The user clarified that meshes, animation and materials already exist, then
authorized tidying, committing and the next pass. Astra corrected the guides to
distinguish existing runtime capabilities from optional typed storage formats.
No source changed after the accepted build/CPU/GPU evidence.

Next action: commit M1-E, then verify the existing glTF character-import path
with public CPU fixtures. Do not automatically add .phskel/.phanim/.phmat or
treat the complete typed family as a game-readiness prerequisite. Runtime pose
sampling, visual skinning, blending/events and production content qualification
remain distinct verification or implementation steps.
