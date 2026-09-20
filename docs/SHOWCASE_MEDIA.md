# Showcase media

The README uses the existing [Phlosion](https://phlosion.com/) identity.
`docs/assets/readme/phlosion-lockup-blue.png` and `phlosion-lockup-cream.png`
are unchanged copies of the website's light/dark horizontal lockups:

- [Blue lockup](https://phlosion.com/brand/phlosion-lockup-horizontal-blue.png)
- [Cream lockup](https://phlosion.com/brand/phlosion-lockup-horizontal-cream.png)

Keep their colors, transparency and aspect ratio. The README `<picture>` selects
the cream wordmark for dark mode and blue for light mode. The engine name is
typeset separately; these files are the shared brand, not a new engine logo.

`editor-project-preview.png` is an unchanged 1440 x 900 Direct3D 12 editor
capture from 2026-09-19. It shows the engine hosting Pokemon Autochess's
Flat Dirt Experiment and `Crowded Battle` setup while stopped in Game view.
Character inking and the game's performance diagnostics are disabled.

The source is the game-owned `flat-unit-setup` editor qualification case:
`debug/reviewer-cleanup/editor-hud/flat-unit-setup/d3d12/capture.png`.
Its matrix passed expected-content and image checks on OpenGL, Vulkan and
Direct3D 12. The local game/engine pair used engine revision `4e555f2` and the
game HUD change subsequently published in `665a923d`.

To reproduce with the private game content restored and a compatible Release
editor/plugin pair, run from the PokemonAutochess checkout:

```powershell
$env:PAC_SHOW_PERF_OVERLAY = '0'
$env:PAC_VIDEO_CHARACTER_INKING = '0'
.\tools\housekeeping\check_editor_workflow.ps1 -Configuration Release -Cases flat-unit-setup -OutputDirectory debug/engine-showcase
```

Review the output before promoting the selected native capture. Do not paint
over UI, invent engine features in a mockup or include private asset payloads.
The README explicitly credits the game for the artwork, gameplay and project
adapters. The screenshot is documentation media, not a bundled playable scene.

Badges describe actual dependencies and the engine's own CI workflow. They do
not imply a stable release, selected code licence or hosted GPU qualification.
