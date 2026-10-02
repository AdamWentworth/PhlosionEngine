# Engine VFX integration

Move reusable VFX implementation into the engine. Keep the independent VFXLibrary
at `D:/Projects/VFX/VFXLibrary`; its optional engine bridge must not be required by
ordinary engine or Autochess builds. Cyberpunk explicitly enables the bridge.

The engine owns authored batches, effect lifetime, submission, reusable preview
helpers and the portable adapter. Autochess owns its four move presets and lab;
Cyberpunk owns the source-package integration tests. Rendering choices are explicit
configuration, not recognition of move names or shader filenames. Preserve behavior
and existing public C++ type names; relocate includes to reflect ownership.

1. Preserve dirty baselines, Git history and native captures. Completed; evidence
   is in `artifacts/vfx-engine-migration`.
2. Relocate implementation and tests; expose `Phlosion::VfxCore`, `Phlosion::Vfx`,
   `Phlosion::VfxAuthoring` and optional `Phlosion::VfxLibraryBridge`. Link the
   runtime into the main Engine interface without a dependency cycle.
3. Update consumers, CI dependency accounting and architecture documentation.
4. Verify engine without the bridge, explicit bridge configuration, CPU contracts,
   both games and affected native OpenGL/Vulkan/D3D12 previews and runtime captures.
   Preserve existing visual thresholds and distinguish pre-existing failures.
5. Record provenance and retirement gates. Leave the old Git history intact and
   make the checkout a retirement stub. Do not archive or delete the remote before
   migration commits are published and downstream pins/clean builds are qualified.

Baseline heads: engine `9f2cdb9`, old VFX `90a036a`, Autochess `b41a211`,
Cyberpunk `eb8444d`, independent library `405bb41`. Existing relocation and Tile
Tools shelving edits must survive. GitHub VFX main is `8d169ff`; there are no open
issues or PRs. Its unpublished local history is backed up as a Git bundle.

The configured Flash worker previously failed with HTTP 402 (insufficient balance).
The root is executing this authorized migration directly; no routing changes or
substitute provider are being made.
