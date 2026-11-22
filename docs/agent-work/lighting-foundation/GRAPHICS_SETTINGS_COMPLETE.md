# Graphics settings completion contract (Q1–Q8)

Scope fixed before implementation, 2026-11-05. Complete means all eight gates
pass for the supported Windows D3D12/Vulkan raster path, not pixel or performance
identity with retail. Research/source trace belongs to Cyberpunk2077Research/
graphics_settings. Game owns preferences, UI and preset policy; engine owns
generic resource controls and renderer budgets. No experimental opt-in renderer.

- Q1: source-traced Low/Medium/High/Ultra and independent supported effect budgets.
- Q2: one validated effective game snapshot consumed in field/battle/editor;
  reflection quality no longer controls unrelated effects.
- Q3: versioned saved preferences, launch precedence, atomic save, missing/corrupt
  input recovery, meaningful errors, reset and Custom derived from values.
- Q4: accessible runtime/editor graphics panel with Apply/Cancel/Defaults,
  keyboard/controller/mouse navigation and gameplay input isolation.
- Q5: live effect changes invalidate dependent histories/caches, respect disabled
  effects, survive scene/Play transitions and release resources safely.
- Q6: texture detail and filtering affect real material resources, exclude data
  textures, and use an explicit restart policy; no ineffective controls.
- Q7: all four tiers and mixed Custom on both enrolled APIs in normal hosts;
  saved preferences survive restart, explicit launch overrides stay temporary.
- Q8: optimized CPU/native/UI correctness and bounded-resource evidence,
  matched GPU observations, locked dependencies and qualified editor handoff.

## Completion evidence

The implemented Q1-Q8 Windows scope is qualified by the following evidence.
Final acceptance additionally requires the exact committed build to pass the
locked-qualification receipt and the controller installation receipt; neither
source builds nor preliminary timings replace those gates.

- Q1/Q2: independent preset/effect policies and one game snapshot reach the
  field, battle and editor. Low keeps temporal reconstruction, sun, lighting,
  probes and falling rain; it switches off the traced optional effects.
- Q3/Q4: four isolated CPU processes cover default/legacy/environment/CLI
  precedence, versioned recovery, atomic replacement/save failure, Apply/Cancel/
  Defaults and mouse/keyboard/controller focus isolation. The same panel works
  in both hosts; battle placement leaves both teams' status panels readable.
- Q5: ten live revisions through repeated Medium/Low/Ultra cycles return to
  identical Medium payload bytes on each API. Reference scene return has exact
  payload equality, zero pending releases and zero allocation failures.
  Saved Custom choices survive editing, paused/resumed Play and scene return.
  Fog Off suppresses scene fog and analytic fallback without zeroing the
  independent light volume used by falling rain; both hosts/APIs recheck Low.
- Q6: native image and exact-upload checks cover 2/1/0 top-mip drops with 1x/4x/
  16x filtering on both APIs. Independent CPU material references preserve packed
  masks/layout maps. Normal uploaded image samplers also accept 8x. Native 512px
  fresh/cached/scheduled shadows agree; slot addresses stay stable.
- Q7: 28 initial normal runtime/editor cases cover four presets, live Custom,
  saved restart and battle on both APIs. Five final UI checks cover the battle
  button correction and live D3D12 runtime changes; CPU input tests pass again.
- Q8: optimized builds, native API validation, image review, repeated resource
  cycles, independent timed runs and the locked-release receipt qualify delivery.
  ABI63/model ABI35 replace 62/34. Prepared shader programs and environment
  packages are unchanged; no launch reads private research inputs.

Private reproducible evidence is in Cyberpunk2077Research/graphics_settings,
with raw runs at WORKER_EVIDENCE_ROOT/phlosion-graphics-settings-20261105 on gpu-worker.
The initial runner argument errors and the corrected battle-button overlap are
retained in the task evidence; they are not counted as successful release tests.

## Policy and boundaries

The game owns presets, user preferences, UI and launch precedence. The engine
owns material mip selection/anisotropic samplers and local shadow allocation.
Texture settings are selected before scene resources; effect settings change
live with view/history invalidation. Low/Medium/High/Ultra are ordinary settings,
not feature opt-ins. Hybrid impact types and actual-puddle rings remain.

Source settings expose independent controls, inherited quick presets and Custom,
2/1/0 mip drops, 1/4/8/16 filtering, and local shadows Off/512/512/1024 with
0/2/3/5 update limits. Fog grids and probe-refresh allocation remain explicit
qualified Phlosion adapters. Packed lookup/mask data is not reduced. Host window/
display controls remain existing host responsibilities. RT, upscalers, cinematic
options, unsupported lens/material systems, Linux and retail pixel/FPS identity
are outside this fixed scope.

## Matched cost

| API | Weather | Previous Medium | New Medium | Delta |
| --- | --- | ---: | ---: | ---: |
| d3d12 | dry | 13.517 ms | 12.275 ms | -1.242 ms |
| d3d12 | wet | 18.861 ms | 17.564 ms | -1.298 ms |
| vulkan | dry | 15.256 ms | 13.394 ms | -1.862 ms |
| vulkan | wet | 20.301 ms | 18.488 ms | -1.814 ms |

1080p Medium on worker GPU1 (RTX 2060 SUPER), unchanged 175 W, RelWithDebInfo.
Two reversed-order pairs per API/weather; frames 121-239, whole-frame GPU cost.
Validation and cold startup are separate. Thermal-flagged attempts are retained
and excluded. Differences include the intentional source-traced Medium policy
(smaller local shadow maps/update budget, lower fog budget and 4x filtering).
These are not same-image optimizations or a claim of retail frame-rate parity.
The final Fog Off correction only changes disabled-fog behavior; the measured
Medium inputs and shader programs are unchanged.
