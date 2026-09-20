# M1-A checkpoint

Status: M1-A accepted by Astra.
Date: 2026-09-20
Baseline: engine main `c17d2122bd36b3c3bb49c4e703900f40cccfbd76`.
Worker: `/root/cyberpunk_m1_assets`, native `astra_flash_builder`.

Full contract and ownership: [PLAN.md](PLAN.md). Root owns this checkpoint,
the plan, prior readiness documents and final index/roadmap integration. Flash
owns only the listed loader, tests, build registration, API documentation,
verification update and `REPORT.md`. Existing uncommitted roadmap documents are
preserved; assigned pre-existing code files were clean at dispatch.

Key decisions: one background byte reader over the unchanged IAssetStore;
explicit request/result lifetime, no worker callbacks or GPU operations;
retained logical-payload budget (not a hard peak-memory guarantee); cancellation
discards late reads; shutdown joins synchronous in-flight I/O. No production
caller migration, dependency resolver or visual readiness claim in this bundle.

## Result and acceptance

Added the loader, public PHRC scene/prefab CPU fixture, two contracts, build
registration/thread linkage, API docs and updated verification scope. Added
`requestStop()` during review as a deterministic, nonblocking lifecycle boundary;
destruction shares the same stop path and then joins the active reader.

Astra reviewed all changed production code, tests, documentation and build
registration against the captured clean code baseline. One consolidated review
correction removed timing-based shutdown ordering and unbounded fixture waits,
strengthened partial-failure/result-lifetime coverage and added a CTest timeout.
A second narrowly scoped correction was justified by a concrete C++ memory
lifetime defect: a test cleanup guard outlived its tracked thread object. Correct
declaration order now keeps helper threads and synchronization objects alive
through cleanup. Astra inspected the final fix and test logs before accepting.

Verification after final correction:
- Full `build-presentation` Debug build passed with editor/tests enabled and
  engine warnings treated as errors.
- Full CTest: 5/5 entries passed, fourteen C++ contracts, including both additions.
- Four repeat contract runs passed before the final guard-only correction;
  final correction was rebuilt and checked with the full CTest suite once.
- Whitespace and documentation link checks passed. Logs: ignored `artifacts/m1-a/`.

No production scene/game caller is migrated. No renderer code or visual behavior
changed; no GPU, editor-preview or private-content readiness is claimed. Byte
budgets exclude provider temporaries and transferred results, and a blocking
provider can delay destruction. The remainder of M1 is still open.

## Routing and workspace

Root: GPT-6 Astra. Native child role: `astra_flash_builder`; session
`01a0bdeb-757a-71c3-85df-e649c54953c1` reports
`deepseek/deepseek-v4.1-flash`, effort high. Router usage during this task confirms
provider deepseek, the same route and status 200. Finished child was interrupted
for completion bookkeeping. No secret or raw router log was copied.

All edits are local and uncommitted, including the preserved prior roadmap docs.
No staging, pushes, game edits, external publishing or renderer changes occurred.
Root integrated the API doc link and the roadmap progress note after acceptance.

## Next action if continuing

Plan the next M1 bundle: dependency-aware archive loading and/or a first consumer
integration with explicit decode and resource lifetime boundaries. Rendered
synthetic fixtures and the game-owned character roster need separate all-three-API
qualification. Keep M0 hardware/content budgets open until selected; this CPU
bundle does not fill them or complete the whole readiness milestone.
