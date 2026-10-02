# VFX integration result

Reusable VFX is engine-owned. `Phlosion::Engine` includes `Phlosion::Vfx`;
`Phlosion::VfxAuthoring` supplies shared preview helpers. The optional
`Phlosion::VfxLibraryBridge` is explicitly enabled by Cyberpunk and remains off
in regular engine and Autochess builds. VFXLibrary stays independent at
`D:/Projects/VFX/VFXLibrary`.

The import covers all 44 source files from PhlosionVFX `90a036a`. Generic code is
under `src/engine/vfx`; the four move presets/controllers and lab belong to
Autochess `src/game/vfx`. Source-package tests moved to Cyberpunk. C++ namespaces
are preserved; includes and CMake targets reflect the new ownership.

Only five imported implementation files needed changes beyond include paths:
the shared configuration/manifest loader, material helpers, procedural batch
bridge and game Growl preset. Asset defaults now come from the game, and material
behavior uses explicit pass fields. Procedural streaks no longer require an
unrelated default mesh. The new generic contract covers that behavior and distinct
alpha-policy texture cache keys. No extracted assets were added to the engine.

## Verification

- Optimized engine editor, Autochess runtime/editor plugin/tests/lab/previewer,
  and Cyberpunk builds passed.
- Engine effect-host, authored-VFX and semantic-boundary contracts: 3/3 passed.
- Autochess affected VFX, layering and boundary contracts: 19/19 passed.
- Cyberpunk full contracts: 14/14 passed; the four lifecycle/portable contracts
  were rerun after final shared changes and passed.
- Autochess CI workflow: 352 checks; dependency-accounting: 41 checks; script
  syntax: 80 PowerShell and 37 Python files; docs hygiene and whitespace passed.
- Fresh engine configuration with the bridge off and an absent library passed.
  Explicitly enabling the absent library produced the required configure error.
- Active engine, both game and independent-library build graphs/caches have no
  retired-repository dependency. Autochess has neither library bridge nor Tile
  Tools targets in its normal graph. Earlier shelving/relocation edits survive.
- Cyberpunk Bite/Confusion Inspector expected-content and renderer parity: six
  captures passed on OpenGL/Vulkan/D3D12. All six were pixel-identical to the
  saved pre-migration captures. Twelve Bite runtime/editor captures at 16:9 and
  21:9 were also pixel-identical to the pre-migration captures.
- The rebuilt engine editor and freshly staged Cyberpunk plugins passed native
  editor probes on all three APIs; the six Inspector cases also passed again.
- Autochess transparency and combat matrices passed expected-content/parity on
  all three APIs. Combat was rerun after the final procedural-streak change.
  The four move-lab previews passed before/after comparison using existing
  thresholds; maximum normalized MAE was 0.000043 and changed-over-8 ratio
  was below 0.00044. The lab host uses OpenGL.

Raw logs, before/after captures, source map, baseline patches and the verified
full-history Git bundle are in `artifacts/vfx-engine-migration`. Autochess native
reports are under its `debug/vfx-engine-migration-native` and
`debug/vfx-engine-migration-final` directories.

## Retirement and remaining gates

The old checkout preserves its source/history but has a retirement CMake stub and
redirect README. The user authorized local commits and chose to retain both the
checkout and GitHub repository for now, with eventual deletion as a separate
future step. No push, remote archival or deletion is part of this commit batch.
At inspection, GitHub's private main was `8d169ff`, four commits behind the
pre-migration local head `90a036a`; there were no open issues or pull requests.
The bundle preserves those four unpublished commits and complete prior history.

The commit sequence records the engine first, then updates Autochess's immutable
engine and package pins to the actual local commits before recording consumers
and retirement documentation. The stale pre-migration engine pin (`4e555f26`,
against the then-current `9f2cdb9`) is addressed by that downstream update.

Before deletion, publish the migration and consumer changes; publish/resolve the
previously extracted PokemonModelSupport dependency; qualify clean consumer
builds; and preserve the complete old VFX history, including the redirect commit,
outside the repository. Hosted clean builds remain unqualified until publication
and dependency distribution are completed.

The pre-existing Cyberpunk full battle visual baseline still fails with frame
hash distance 2 and color difference 13.5 (limit 1.0). Before/after captures are
identical; the golden and its thresholds were not changed. Interactive editor
navigation and the entire gameplay/visual suite were not requalified by this
ownership migration.

Implementation was completed by the root after the previously configured Flash
worker route failed with HTTP 402; no successful delegated implementation or
provider fallback is claimed.
