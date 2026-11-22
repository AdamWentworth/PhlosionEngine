# Vulkan CPU work and presentation overlap

2026-10-19. Follow-up to [SUBMISSION.md](SUBMISSION.md) and the indirect-light
composition checkpoint. Cloud-only implementation. No game lighting, shader,
asset, weather, resolution or animation-quality settings changed in this pass.

## Measured result

RTX 2060 SUPER, Windows, 1920x1080 heavy wet nighttime station, Hybrid rain,
45 seconds accumulated wetness, fixed front camera. Sequential hidden runtime
runs with vsync disabled;
200 warm samples (frames 30-229), no simultaneous capture or build. Final timing
disables temporary profiling, thread CPU diagnostics and Vulkan validation.
The original executable/PDB are retained for interleaved before/after runs.

| Run | GPU median ms | Host median ms | Host p99 ms | Host maximum ms |
| --- | ---: | ---: | ---: | ---: |
| Vulkan original repeat | 20.187 | 49.136 | 72.362 | 79.300 |
| Vulkan final 1 | 20.291 | 22.415 | 33.881 | 42.203 |
| Vulkan original repeat 2 | 20.304 | 50.289 | 66.912 | 70.402 |
| Vulkan final 2 | 20.419 | 23.262 | 32.568 | 34.716 |
| Vulkan final, presentation overlap disabled | 20.309 | 42.187 | 54.696 | 57.315 |
| D3D12 original repeat | 19.554 | 19.917 | 29.718 | 32.600 |
| D3D12 current | 20.042 | 20.858 | 31.484 | 34.906 |

Host median falls about 54%, corresponding to approximately 20 -> 43-45 FPS
capacity in this measured scene. GPU time is essentially unchanged. This is
throughput evidence, not a general FPS guarantee or an input-latency measurement.
Both APIs still miss the 16.67 ms / 60 FPS target. Vulkan needs another roughly
6 ms host reduction; both need roughly 3.5-4 ms less GPU time. Prioritize measuring
GPU pass costs next, without lowering the established lighting quality.

All final timing images are pixel-identical to the pre-pass baseline; environment,
precipitation, surfaces, imports, exploration and animation state are exact.
Percentiles use NumPy's percentile calculation. Raw samples, process snapshots
and before/after GPU clocks/utilization are retained; the latter are not continuous
clock telemetry. D3D12 receives no production implementation change here; its
repeat differs by +.941 ms host / +.488 ms GPU, and no improvement is claimed.

## What changed

1. Immutable geometry cache hits no longer rescan every index. Admission still
   validates all new geometry and rejects invalid dimensions/indices. An existing
   key still means immutable contents, as required by the geometry cache API.
2. Reused skin palettes compare against a byte-exact CPU memory mirror, instead
   of reading GPU upload memory which need not be CPU cached. Content hash, size
   and byte equality remain required. Mutating the same input address does not
   reuse stale skinning data. The mirror resets each frame and reuses capacity.
3. On swapchains with a spare image, CPU command recording for the next frame
   overlaps rendering of the preceding one. The previous presentation is issued
   before the next submission. Only one presentation is pending; the existing
   two frames in flight remain. No presentation thread or new queue is added.
4. Completed image-to-fence ownership is cleared before that fence is reused.
   Otherwise stale aliases make acquisition wait for an unrelated new submission.
5. Presentation-complete semaphores are owned by swapchain images rather than
   frame slots. Reacquisition supplies the correct reuse lifetime. See the
   [Khronos semaphore-reuse guidance](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html).
6. Clear/load render-pass variants now have matching dependencies. Depth that is
   resumed with LOAD is stored, and the barriers cover depth writes/reuse. Actual
   Khronos validation found these preexisting errors, including in the retained
   original executable; they are corrected rather than suppressed.

`PHLOSION_VULKAN_DISABLE_PRESENT_OVERLAP=1` retains the immediate presentation
control. It changes scheduling, not quality. The normal default checks actual
swapchain image count against the surface minimum; no spare image means no
overlap. First and screenshot frames present immediately. Resize, vsync change
and shutdown drain pending presentation before destroying its resources.

The deliberate presentation delay can add up to a CPU recording interval to
display latency. End-to-end latency has not been measured, nor have Linux or
other GPU vendors been qualified. Do not claim a latency improvement or universal
driver speedup. The Vulkan spec permits host blocking in
[vkQueuePresentKHR](https://docs.vulkan.org/refpages/latest/refpages/source/vkQueuePresentKHR.html);
the cost and benefit here are measured on this Windows/NVIDIA setup.

## Diagnosis and retained failures

Initial diagnostic Vulkan CPU consumption averaged 29.531 ms/frame versus
51.136 ms host median. Temporary native scopes found about 2.65 ms repeated
geometry validation and 6.4 ms draw-state work; the palette change reduced the
latter to about 3.13 ms. Scopes nest and must not be added together. The first
CPU-only revision measured 21.563 ms average thread CPU and 43.333 ms host median.
Windows thread accounting is coarse; compare averages, not tiny-scope medians.

An immediate present-mode trial did not remove the blocking. The first deferred
presentation trial merely moved about 20 ms from presentation into the stale
image-fence wait, leaving host time at 42.377 ms. Correcting fence ownership is
essential to the measured overlap improvement. Temporary instrumentation and
trial present-mode switches are absent from production source.

The instrumented `deferred-fences` trial failed strict image equality: 31 pixels
differed by one 8-bit channel step (MAE .000004983/255). Its image, delta and report
remain retained. The uninstrumented repeat and all final captures are exactly
equal to the baseline. No image guard was weakened to pass the trial.

WPR CPU capture was denied by system policy; no recording was left active. Native
scope profiling was used instead. The Vulkan core/synchronization layer is a
portable, process-local diagnostic download in ignored artifacts, not a global
installation. Its Khronos build commit is
`aea761f4d54b86682be4099c428dba27b6609940`, workflow 39081605124, artifact
12119359820; archive SHA-256 is
`61c20124a1434caff1258fd8e9ba1eb3aab1c4802e9ea41df231ce1c1db50c99`.

## Verification and reproduction

The 32 focused engine checks pass: Vulkan submission/lifetime contract, CPU
engine contracts, both APIs' indirect-composition cases and planar reflection
cases. Core and synchronization validation are enabled for Vulkan checks; the
native logs have no validation errors. The full heavy station capture also
passes both validation layers after reproducing the original failures separately.
The native submission test covers mutable skin palettes, immutable mesh admission,
fence reuse, resizing, vsync changes, the no-overlap fallback and repeated shutdown.
Its target/CTest registration requires `PHLOSION_BUILD_GPU_TESTS=ON`; a default
source build does not opt into this new native GPU check.

Both PokemonCyberpunk normal editor launches and five-visit station/reference
switch sequences pass, with zero pending resources at every recorded visit.
All eight station/battle API/host views retain exact state and pixels against
the preceding accepted `indirect-composition/verified/scene` baseline. Existing
fixture, content, allocation and parity guards pass, along with the two affected
game CPU contracts. Staged game/model DLLs match their build hashes.

Autochess's runtime/editor builds and four affected CPU contracts pass. Its two
native character cohorts on both APIs and 17 editor cases on both APIs pass the
existing expected-content, API and baseline guards: all 38 images are exact.
These are warm functional checks, with SHA-recorded cache seeding and unchanged
180-second native / 240-second editor deadlines; cold startup was not requalified.

Final test-registration review checked OFF -> zero new native tests and ON -> one,
then rebuilt/reran the native Vulkan check under both validation layers. The
configuration step rewrote Autochess's generated renderer-policy header, relinking
its plugin from unchanged sources. The initial same-binary assertion in
`finish-checks.ps1` therefore failed for the plugin; the renderer and probe stayed
identical. `editor-pair-final` records the refreshed proof. Eight additional
fire/eyes/wing/flight captures qualify that final plugin on both APIs with exact
pixels. `compare.py --final-plugin` verifies all 46 retained Autochess images.
The temporary final-plugin wrapper also initially misread PowerShell's unset
native exit status/array cardinality as a failure; the native image reports were
already passing. Its corrected report checks revalidated those existing captures,
without changing guards or taking replacement images.

Evidence lives under `artifacts/lighting-foundation/cpu-submission` in the engine,
PokemonCyberpunk and PokemonAutochess. The game's `measure.ps1` reproduces timing;
`analyze.py` produces `comparison.json` using existing frame-cost parsing and
strict image/state checks. `qualify-performance.ps1` runs the validation capture,
interleaved executable comparisons and the overlap-disabled control. The separate
`qualify-game.ps1` stages the normal launch and exercises scene switching and
station/battle content. Autochess's `qualify.ps1` uses its established native and
editor material guards. Final shared-consumer results are recorded in the
[checkpoint](CHECKPOINT.md).

Do not change thresholds, hide baseline validation failures or compare instrumented
and uninstrumented timing as a controlled speedup. Cold loading and compilation
are excluded from these frame measurements and remain separate open costs.
