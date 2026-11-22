# Standard temporal renderer - 2026-10-27

PokemonCyberpunk uses its qualified temporal renderer in normal runtime and
editor primary views. No temporal feature variables are required. Medium is
the default. The game no longer selects the old inline reflection search or
renders another opaque scene for reflection color.

## User decision and behavior

Low/Medium/High/Ultra are quality choices within one renderer. Native camera,
rigid/skinned/wind motion, reconstruction and N-1 color feedback are required
dependencies at every level. Low omits SSR; Medium/High/Ultra use the existing
2x12 / 3x12 / 4x16 base-ray/step variants. The active invalidation rule can add
rays as documented in TEMPORAL_REFLECTIONS.md. Low retains environment/planar
reflections and the same rain, surface splashes and water rings.

This matches the ordinary SSR preset mapping recorded in research
lighting/quality/README.md. It does not claim complete shadow/fog/AO/vegetation
presets or saved graphics settings. Those remain implementation work on this
standard renderer, alongside C1 compaction/coverage, C7/C8 and 16.67 ms acceptance.

Use Launch-Editor.ps1 -GraphicsQuality low|medium|high|ultra, or
CyberpunkRuntime --quality=low|medium|high|ultra. Direct project-picker launches
default to Medium. CYBERPUNK_GRAPHICS_QUALITY selects the level; the old
CYBERPUNK_REFLECTION_QUALITY name remains a compatibility alias with lower
precedence. The game rejects reference as a quality level. Obsolete temporal
enable variables have no effect. Diagnostic readbacks and engine reference
fixtures remain development tools, not player rendering modes.

## Engine boundaries

HDR world alpha supplies reactive coverage without process-wide enable flags;
opaque coverage and transparent/rain coverage keep their established convention.
Vulkan applies that convention to HDR scene variants, preserving direct-display
alpha. Haze keeps destination reactive coverage. Scene presentation is opaque.
Generic engine temporal APIs still execute when a caller submits a temporal view;
this change does not force unrelated games to acquire temporal views.

## Verification

Implementation, optimized RelWithDebInfo builds and all execution ran on gpu-worker
Windows, EVGA RTX 2060 SUPER LUID 000000000000be8e. controller only reviewed and
integrated source. The task baseline includes existing uncommitted work.

- Five engine and six game CPU contracts pass, including default Medium, four
  quality values, invalid/reference rejection, Low SSR-off and alias precedence.
- Both native temporal reconstruction and stochastic-reflection fixtures pass on
  D3D12/Vulkan with their old enable variables removed, covering history, resets,
  coverage, formats, exposure, motion and the four individual SSR variants.
- Eighteen runtime/editor GPU runs pass: dry/wet, moving camera and battle at
  default Medium on both APIs, plus Low/High/Ultra wet runtime on both.
  Twelve Medium images match the prior explicitly enabled qualified path:
  D3D12 exactly; Vulkan within the existing one-byte/channel limit.
- Four first-battle/return-to-field runs pass: previous poses, feedback and
  reflection hits reject incompatible history.
- All 22 game/editor manifests lack temporal opt-in variables. Default Medium
  also lacks quality overrides. Profiles contain no screen-reflection-source
  redraw; Low omits temporal-reflection dispatch, other tiers include it.
- D3D12/Vulkan validation reports no errors. Existing D3D12 warning 1328 remains.
  Launcher PowerShell parsing and invalid runtime-quality rejection pass.
- Visual review of Low retains the station, actor, colored wet-ground response,
  rain and puddle rings; the intended SSR reduction is the quality change.

Evidence: worker WORKER_EVIDENCE_ROOT/phlosion-temporal-defaults-20261027 and controller
PROJECTS_ROOT/Phlosion/PhlosionEngine/artifacts/agent-work/temporal-defaults-20261027. The handoff records baseline and
tested-source hashes, build/product hashes, manifests, comparison metrics,
profiles and logs. Earlier milestone reports retain their dated opt-in wording;
this decision supersedes it.

No fresh performance claim or parity-score increase follows from changing the
default. Reuse the matched consumer timings in TEMPORAL_REFLECTIONS.md.
Native-1080p Medium 16.67 ms, full preset budgets, cold startup, complete C1-C8
acceptance and Linux release qualification remain open.
