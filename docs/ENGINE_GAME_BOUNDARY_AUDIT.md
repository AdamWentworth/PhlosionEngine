# Engine/game boundary audit (2026-09-26)

The engine source and build graph now contain reusable services and source-neutral
fixtures only. PokemonCyberpunk owns the Outskirt overworld/battle preview,
runtime/editor plugin, HUD, evidence guard, and their tests and capture tools.

## Inspected surface

- `src/engine`, public render/editor/input/collision/camera/audio APIs and shaders;
- `examples`, `tests`, `tools`, and the CMake target/test graph;
- PokemonCyberpunk's launcher, project descriptor, preview, battle/VFX adapters,
  native evidence scripts and tests;
- PokemonAutochess's model-render provider, and the research runners that launch
  the private Outskirt preview.

## Boundary corrections

| Former engine ownership | Current owner |
| --- | --- |
| `examples/exploration_fixture` with Outskirt, Wes, Willie, battle rules/presentation/HUD | `PokemonCyberpunk/src/game/preview` |
| Exploration runtime/editor plugin, guard and integration tests | PokemonCyberpunk build and test graph |
| `BattlePokemonPlugin.h` model rendering ABI | Source-neutral `ModelRenderPlugin.h` in Engine; implementation in PokemonAutochess |
| Private Scarlet/Violet and LGPE test data literals | Scarlet/Violet package check moved to PokemonCyberpunk; Engine test fixtures use neutral data |
| Outskirt-specific D3D12 renderer comment | Source-neutral renderer wording |

`PhlosionEngineSemanticBoundary` scans engine source, shaders, examples, C++
tests, C++ tools, CMake, former ownership paths, and game-specific identifiers.
The game test plan owns the three-API runtime/editor qualification. Historical
milestone reports retain their dated observations, with a migration note in the
readiness audit; current build instructions point to the game repository.

The engine still owns generic movement, camera, collision, input, renderer,
editor host, model-render ABI, and verification support. An untracked local
build may retain old binaries after CMake removes their targets; launchers and
project descriptors use only the game-owned plugin and runtime.
