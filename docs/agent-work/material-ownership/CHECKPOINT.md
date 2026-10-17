# Material ownership cleanup checkpoint

Date: 2026-10-15. Implementation and verification complete.

## Result

General texture/normal sampling, PBR surface shading, weather response, review
lighting and final color output moved from the optional model adapter to
`assets/shaders/materials`. Seven explicit hooks preserve recovered eye/coat and
texture interpretation in PokemonModelSupport. Neutral defaults remain in the
engine. The source-neutral CMake composer now lives in the engine; Cyberpunk and
Autochess both call it. No shader expressions, material IDs, bindings or ABIs
changed. Source-specific dispatch and early-return effects remain in the adapter.

The engine has no Pokemon dependency. The adapter boundary rejects moving general
surface implementations or the composer back into that package. Existing source
provenance and licensing exclusions were retained. Scene assets and settings were
not edited. The next lighting study is separate work.

## Verification completed

- Both games' RelWithDebInfo runtime and editor targets built. Cyberpunk's model
  providers and project DLL were staged into its normal editor directory.
- Autochess's normal engine `build` editor was refreshed, and the existing paired
  build tool verified ABI 36, contract size, layout and compiler compatibility
  against the staged project plugin. Its source/artifact proof is saved locally.
- All 34 composed text shaders have identical token streams to the pre-refactor
  baseline; 32 are byte-identical and two differ only in comments. All 14 Vulkan
  shader binaries match freshly compiled baseline programs byte for byte.
- Seven engine tests passed: RectLights, EngineContracts, EngineSemanticBoundary,
  MaterialComposition, PackedNormalContract, MaterialDebugViewContract and
  ReviewLightingProfileContract. The new composition test covers defaults,
  overrides, native include preservation, missing paths, traversal and cycles.
- Eight adapter shader contracts passed: CharacterStageLight,
  LayeredCharacterShading, LayeredEyeSpecular, PackedSpecularProbe, RefractiveEye,
  StageReflectionProbe, SubsurfaceEnvironment and ViewAngleLayer.
- NativeIr, Runtime and Boundary adapter tests, plus Autochess's
  ProjectOwnershipBoundary and EngineSemanticBoundary passed. Cyberpunk's
  OutdoorProfile and WorldEnvironment tests also passed (22 selected CTests total).
- Cyberpunk's six night/rain station captures passed the existing fixture,
  allocation, API and host parity checks. Maximum API MAE was 1.230/255, below the
  existing 1.5 limit; runtime/editor MAE was below 0.000006/255.
- All six rainy battle captures passed the existing content and API/host parity
  checks. All twelve station/battle viewports are pixel-identical to the accepted
  pre-refactor `artifacts/wet-materials` captures, with matching camera and asset
  state. Every before/after MAE and maximum pixel difference is zero.
- Autochess's two native character cohorts passed on all three APIs (six
  captures, ten character variants). All 36 expected-content guards passed.
  Maximum normalized API MAE was 0.001786 against the existing 0.01 limit;
  maximum changed-pixel ratio was 1.40% against the existing 5% limit.
- Autochess's body/eye and authored-stage editor cases passed on all three APIs
  (six captures, all 12 expected-content guards). Maximum normalized API MAE was
  0.000083; maximum changed-pixel ratio was 0.074%, below the same existing limits.
- Final review covered all four repository diffs and new files; whitespace and
  conflict-marker checks passed. All 14 tested SPIR-V hashes still match their
  recorded baseline values. No verification runtime/editor processes remain.

## Local evidence

Ignored artifacts, not source assets:

- Engine `artifacts/material-ownership`: original sources and composed programs,
  equivalence reports, freshly compiled baseline SPIR-V, configure log and
  `compare_cyberpunk.py` for fixed-viewport before/after comparison.
- Cyberpunk `artifacts/material-ownership`: build log, native captures and reports.
- Autochess `artifacts/material-ownership`: build and paired-editor proof/logs.

Reproduction commands use each game's root as the working directory:

```powershell
# Cyberpunk, sequential GPU capture stages
./tools/Verify-Canopy-Neon.ps1 -Stage matrix -OutputRoot artifacts/material-ownership/native
./tools/Verify-Canopy-Neon.ps1 -Stage battle -OutputRoot artifacts/material-ownership/native
# Run the two Python validators with the bundled numpy/Pillow interpreter.
python tools/verify_station_rect_lights.py artifacts/material-ownership/native
python tools/verify_outskirt_station.py artifacts/material-ownership/native .phlosion/outskirt-station/v1 --groups rain-battle

# Autochess: use Windows PowerShell for the existing System.Drawing checker.
powershell.exe -NoProfile -File tools/check_native_character_materials.ps1 -Configuration RelWithDebInfo -OutputDirectory artifacts/material-ownership/runtime
powershell.exe -NoProfile -Command "& ./tools/check_character_materials.ps1 -Configuration RelWithDebInfo -OutputDirectory artifacts/material-ownership/editor -Cases 'character-body-eyes','character-stage'"
```

PowerShell 7.6.5 could capture but failed to load the existing image-analysis
modules because its Add-Type compiler did not resolve System.Drawing.Common.
Windows PowerShell successfully loads both modules; reuse completed captures with
`-SkipCapture` when validating that initial run. Keep its initial failure log.
Both native D3D12 cohorts also exceeded the existing 180-second process startup
timeout on the initial run. The ignored Autochess `finish_native_checks.ps1` reuses
the repository capture function for missing images with a 450-second timeout, then
runs the unchanged validators under Windows PowerShell. It preserves original
failure logs. Image limits, frame/timeline, content guards and scene settings are
unchanged; this is a startup allowance, not performance qualification.

No commit or push was requested for this reorganization. Dependency lock revisions
remain at the preceding committed checkpoint while these local changes are
uncommitted; this is not strict-lock or release qualification. Capture timing is
not a performance claim. No visual thresholds or baselines were relaxed.

## Completion

All planned checks are complete: 22 selected CTests and 24 native runtime/editor
captures passed. Cyberpunk's 12 before/after viewports are pixel-identical.
The changes remain uncommitted in the engine, model adapter and two consumers.
