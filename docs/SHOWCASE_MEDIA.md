# Showcase media

The README uses the existing [Phlosion](https://phlosion.com/) identity.
`docs/assets/readme/phlosion-lockup-blue.png` and `phlosion-lockup-cream.png`
are unchanged copies of the website's light/dark horizontal lockups:

- [Blue lockup](https://phlosion.com/brand/phlosion-lockup-horizontal-blue.png)
- [Cream lockup](https://phlosion.com/brand/phlosion-lockup-horizontal-cream.png)

Keep their colors, transparency and aspect ratio. The README `<picture>` selects
the cream wordmark for dark mode and blue for light mode. The engine name is
typeset separately; these files are the shared brand, not a new engine logo.

The Windows project picker uses an unchanged copy of the site's
`public/brand/phlosion-lockup-horizontal-cream.png` under `tools/branding/`.
Its `phlosion-mark.ico` window and shortcut icon is a 256-pixel format conversion
of the site's `public/brand/phlosion-mark.png`. The picker follows the site's
dark palette (`#050b13`, `#0b1826`, cream `#f7efd9`, orange `#ff5a00`). These
brand assets have the same ownership exception as the README lockups.

The native editor uses resized copies of the same website assets in
`src/engine/editor/branding/`: the cream horizontal lockup is 300 x 115 for the
project browser and menu-bar mark, and the mark is 64 x 64 for the SDL window
icon. The Windows executable embeds `tools/branding/phlosion-mark.ico` for
Explorer and taskbar display. CMake copies the PNGs beside the editor executable
at build time. Its shared ImGui skin uses the site's navy, cream, and orange
palette; the project browser's Open Project action uses `#ff5a00`.

The 2026-09-23 native editor branding check covered the project browser and the
PokemonCyberpunk Outskirt battle workspace at matching frames on all three
renderers:

| Case | OpenGL | Vulkan | Direct3D 12 | Expected content |
| --- | --- | --- | --- | --- |
| Project browser | Pass | Pass | Pass | Site lockup, navy surfaces, orange Open Project button, native backend |
| Outskirt workspace | Pass | Pass | Pass | Menu-bar mark, navy docked UI, battle HUD and VFX adapter |

Local captures and the expected-content/image check are under
`artifacts/editor-brand/`; the game workspace captures and handshakes are under
PokemonCyberpunk's `artifacts/editor-setup/`. Excluding the renderer-name text
and game viewport, the shared UI pixels matched exactly across the three APIs.
The game viewport's OpenGL/Vulkan and OpenGL/D3D12 mean absolute errors were
0.7080 and 0.8449, respectively, unchanged from the pre-branding captures.
UI focus/text and engine contract tests passed. These local captures do not
replace interaction checks in a visible editor window.

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

Badges describe actual dependencies, the engine's own CI workflow and its
[MIT code licence](../LICENSE). They do not imply a stable release or hosted
GPU qualification.

The code licence does not cover the Phlosion brand images or the editor
screenshot. Phlosion branding and third-party artwork/trademarks retain their
respective owners' rights; the screenshot does not grant rights to the depicted
Pokemon assets. See [the licence scope](../README.md#licence).
