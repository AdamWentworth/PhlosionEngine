# Engine VFX

PhlosionEngine owns reusable effect lifetime, authored particle/mesh batches,
renderer submission and preview helpers. Ordinary engine consumers get VFX through
`Phlosion::Engine`; no separate PhlosionVFX repository is required.

| Target | Responsibility | Dependencies |
| --- | --- | --- |
| `Phlosion::VfxCore` | Authored batches and material/geometry helpers | Engine rendering |
| `Phlosion::Vfx` | EffectHost, batch construction and submission | VfxCore |
| `Phlosion::VfxPreview` | Generic preview application and interfaces | Engine |
| `Phlosion::VfxAuthoring` | Reusable preview renderer and controller helpers | Vfx, VfxPreview |
| `Phlosion::VfxLibraryBridge` | Portable effect sampling, rendering and audio | Vfx, Audio, independent VFXLibrary |

Headers live under `engine/vfx/`. Existing `vfx::runtime` and `vfx::portable`
namespaces are retained. Runtime targets do not depend on preview/editor code.

The library bridge is **off by default**, including when a VFXLibrary checkout
happens to be installed. Enable `PHLOSION_BUILD_VFX_LIBRARY_BRIDGE=ON` and supply
`PHLOSION_VFX_LIBRARY_SOURCE_DIR`, or provide an existing `VFXLibrary::Runtime`
target. An explicitly requested but unavailable library is a configure error.
The local default is `D:/Projects/VFX/VFXLibrary` in the standard workspace layout.
The portable library owns its format, catalog and engine-neutral implementation;
it does not depend on PhlosionEngine.

Authored configuration starts without asset paths or draw passes. Games supply
their presets and manifests. Pass fields `line_pass`, `quarter_texture_bake` and
`preserve_glow_alpha` describe material behavior; shader filenames never select
game-specific rendering behavior. Geometry, placement, blend, depth and time
semantics remain shared across supported rendering APIs.

`PhlosionEffectHost` tests lifetimes, cancellation, anchors and invalid input.
`PhlosionAuthoredVfx` tests empty defaults, explicit material decisions and manifest
loading without a GPU or library checkout. Source-package acceptance tests live
with the consuming project rather than adding game assets to engine tests.

## Migration provenance and retirement

Implementation was imported from PhlosionVFX local commit
`90a036a16822b5bdef637f4f32aeb2df5ca8a643`. Its generic services are now under
`src/engine/vfx`; move-specific presets/controllers/lab are owned by Autochess,
and portable source-package contracts by Cyberpunk. The independent library remains
outside the Phlosion workspace. No extracted assets were added to this repository.

The original checkout and GitHub repository are retained for now, with the local
checkout serving as frozen source/history and a CMake retirement notice. The
eventual goal is to delete both once the migration has proved dependable. Before
that separate retirement step, publish the migration and redirect notice, preserve
the complete history outside the repository, update downstream engine pins,
publish/resolve the model adapter dependency and verify clean consumer builds.
No remote archival or deletion is part of this migration commit.
