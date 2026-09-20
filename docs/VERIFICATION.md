# Engine verification

## Automated source and CPU checks

[Engine CI](../.github/workflows/ci.yml) runs on pushes to `main`, pull requests
and manual dispatch. It uses the Windows Visual Studio 2026 runner, restores
the manifest-pinned vcpkg dependencies, builds all enabled Debug targets with
warnings treated as errors, and runs CTest.

The build includes the native editor, plugin probe and Vulkan shader programs.
It does not fetch a game, private assets or optional package repositories.
No test in the current engine CTest suite needs a graphics context.

| CTest entry | Coverage |
| --- | --- |
| `PhlosionEngineContracts` | Fourteen C++ contracts: material profiles, containers, scenes, environment patches, asynchronous asset loading, the public PHRC scene/prefab fixture, descriptors, renderer preference, camera pan, plugin ABI, preview routing, reload and performance accounting |
| `PhlosionEngineSemanticBoundary` | Removed game systems, identifiers, materials and former source paths cannot return to engine code/shaders |
| `PhlosionPackedNormalContract` | Shared packed-normal source contract |
| `PhlosionMaterialDebugViewContract` | Material debug-view source contract |
| `PhlosionReviewLightingProfileContract` | Generic review-lighting source contract |

Local equivalent:

```powershell
cmake --preset vs2026 -DPHLOSION_BUILD_EDITOR=ON -DPHLOSION_BUILD_TESTS=ON -DPHLOSION_WARNINGS_AS_ERRORS=ON
cmake --build --preset debug --parallel 4
ctest --preset debug
```

Tests enforce particular contracts, not a blanket claim of correctness. Review
the changed implementation and relevant consumer behavior as well.

The asynchronous loading contract and its retained-byte-budget limits are
described in [ASYNC_ASSET_LOADING.md](ASYNC_ASSET_LOADING.md). The loader tests
are CPU only: they coordinate a synthetic provider with condition variables and
never create a graphics context. `PhlosionEngineContracts` carries a 120 second
CTest timeout as a regression backstop so a blocked loader cannot hang the suite
unboundedly.

## Native rendering and editor changes

OpenGL, Vulkan and Direct3D 12 are equal supported targets. Renderer or visual
editor work requires all affected surfaces on all three native APIs, including
expected-content checks at matching scene/simulation frames. Startup logs and
source contracts alone cannot prove visual correctness.

Use the consuming project's scene and material fixtures when the change affects
its rendering. Keep game content and capture harnesses in that project. The
current Autochess consumer documents its [parity contract](https://github.com/AdamWentworth/PokemonAutochess/blob/master/docs/RENDERER_PARITY_CONTRACT.md)
and [test plan](https://github.com/AdamWentworth/PokemonAutochess/blob/master/docs/TEST_PLAN.md).
Generic engine defaults also need independent coverage when changed.

- Keep resolution, simulation time, camera, content and presentation settings comparable.
- Rebuild compatible editor/project-plugin pairs when public interfaces change.
- Check models, materials, animation, scene visibility and UI, not only pixel similarity.
- Record backend, source revision and coverage limits. Do not loosen thresholds
  or silently fall back to another API to turn a failed check green.

Hosted CI is a Windows build/CPU gate. Full GPU qualification and performance
measurement remain local; this workflow does not certify other operating systems,
every GPU/driver, all project content or every editor scenario.

## Recorded evidence

On 2026-09-20, the M1-A asynchronous asset iteration added
`AsyncAssetLoader` with its explicit `requestStop()` lifecycle boundary, the two
CPU contracts above and their documentation. The existing `build-presentation`
Debug tree (VS2026, editor/tests enabled, engine warnings treated as errors) rebuilt cleanly
with the new `engine_core` source and standard thread linkage, and the full CTest
set passed (5 of 5 entries, including all fourteen C++ contracts). Logs are local
under `artifacts/m1-a/`. This iteration is pure CPU and produces no rendering or
visual-parity evidence.

On 2026-09-19, the presentation review built a separate `build-presentation`
Debug tree with MSVC, editor/tests enabled and warnings treated as errors.
All five CTest entries passed, including all twelve C++ contracts and the
semantic boundary. Logs are local under `artifacts/presentation-review/`.
This review changes documentation, media and CI rather than runtime behavior.

The material extraction at engine commit `4e555f2` has separate
[consumer verification](https://github.com/AdamWentworth/PokemonAutochess/blob/master/docs/CHARACTER_MATERIALS.md#boundary-verification-2026-09-19):
listed editor and native game cases across three APIs, default-engine checks,
CPU contracts and scoped performance measurements. Consult that dated record
for its exact limits. Subsequent consumer-specific qualification failures belong
in the [game's issue register](https://github.com/AdamWentworth/PokemonAutochess/blob/master/docs/OUTSTANDING_ISSUES.md).
