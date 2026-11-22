# Rendering quality policy

Recorded 2026-10-26. Whole-game presets remain a design contract. Independent
screen-reflection tiers now exist in PokemonCyberpunk; see SSR_QUALITY_PERFORMANCE.md.

PokemonCyberpunk's first performance qualification target is Medium raster
quality at native 1920 x 1080 and 60 FPS on one explicitly identified GPU.
High is the next target; Low is a supported fallback and Ultra an optional
higher-cost tier. The current accepted custom renderer remains the reference
during implementation. It is not established as equivalent to another game's
Ultra preset. The 16.67 ms goal is still unmet.

## Ownership and controls

PhlosionEngine owns reusable effect settings, resource budgets, supported
capabilities and safe application. Games own named bundles, defaults and UI.
An engine-wide Low/Medium/High label must not silently choose every game's
art policy. Other games, including Autochess, retain their existing behavior.

Expose independent settings as each affected pipeline becomes real:

| Control | Engine responsibility |
| --- | --- |
| Screen reflections | Enabled state, bounded sampling tier, roughness fallback, temporal dependencies |
| Shadows | Map resolution, refresh work budget, caster LOD, sun cascades, optional contact detail |
| Fog / atmospheric effects | Grid dimensions, samples, reconstruction and resource bounds |
| AO | Enabled state and sampling/reconstruction quality |
| Texture residency / filtering | Mip policy, anisotropy and selected-device memory budget |
| Output / internal resolution | Explicit dimensions, optional scaling mode and quality |
| Vegetation / particles | Rendering/LOD/overdraw budgets independent of gameplay simulation and weather time |

Preserve the same material response, emitter geometry, authored light colors,
exposure conventions and continuity rules across tiers. Reduce sampling,
resolution, update frequency or optional detail deliberately. Temporal modes
require valid reconstruction, history reset and probe fallback. The authorized
single-frame bounded mode below is a separate approximation with explicit detail loss.

## Apply and verify

Resolve each preset plus overrides into explicit effective settings. Persist
them and log them with a version; unsupported requests must have a visible
fallback. Apply resource/format changes at a safe rendering boundary. Rebuild
affected targets/pipelines and invalidate incompatible histories, including
view changes, resizing and field/battle transitions. Prewarm required variants.

Keep the old custom reference selectable until replacements pass. Introduce
new effect controls with the pipeline that consumes them; avoid an apparently
working settings menu whose controls do nothing. Check preset resolution,
Custom overrides, persistence and capability fallbacks when implemented.

Optimization comparisons require identical resolved settings. Tier comparisons
require reviewed images/sequences and an explicit account of lost detail.
Record native/scaled resolution, texture tier, effect budgets, API, single GPU
identity, build mode, source/payload hashes, frame-time distribution, memory and
thermal conditions. Use optimized builds; distinguish release-like
RelWithDebInfo with assertions from pure Release. GPU time alone is not FPS.

Qualify Medium/High first across dry daylight, wet night, movement, battle and
return, including editor and runtime on each enrolled API. Keep Low/Ultra
correctness checks and representative captures without requiring Ultra/60 on
the same hardware. Native-resolution results precede separately labeled scaled
results. Windows D3D12/Vulkan and later Linux Vulkan remain the game gates.

## Source study and next dependency

Cyberpunk2077Research/lighting/quality/README.md records the inspected source
preset -> menu -> renderer/shader mapping and the private evidence receipt.
The source snapshot is not authenticated as current retail. It demonstrates
independent work budgets and settings roundtrip nuances; exact preset values
are research findings, not imported code or runtime dependencies.

Continue the shared surface/motion/history work in
[GPU_PHASE_PROFILE.md](GPU_PHASE_PROFILE.md). Implement bounded Medium/High SSR
with valid temporal feedback for detail recovery. The authorized single-frame
bounded mode below is now the game's default, with measured results in
SSR_QUALITY_PERFORMANCE.md. Follow with independently budgeted shadows/fog/AO.
The broader whole-game preset design remains unimplemented.


## Interim checkpoint and reaffirmed target - 2026-10-26

The prior authorization allowed the measured bounded single-frame mode.
That checkpoint is retained with its detail loss and mixed daylight result.
The user has now reaffirmed the complete connected technique as the target.
Follow [COMPLETE_TECHNIQUE.md](COMPLETE_TECHNIQUE.md).

Do not treat the interim exception as authorization to keep replacing missing
motion/history, temporal reconstruction and frame-colour reuse with further caps
or visual shortcuts. Implement source quality variants as part of the complete
path. Current defaults stay an interim baseline until that replacement qualifies;
this decision does not change runtime settings.
