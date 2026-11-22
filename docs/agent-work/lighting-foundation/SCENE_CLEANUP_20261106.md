# Outskirt appearance and movement cleanup — 2026-11-06

The user now prioritizes playable appearance and smooth movement over literal
Cyberpunk 2077 matching. The earlier matrix records bounded implementation
coverage, not measured equality to retail. Replacement hair remains deferred.

## Changes

- Removed the camera-radius override from indirect-light blending. The near
  transport now fades at its world-space boundary, independent of the player.
  Reflection dimming blends bounded visibility rather than a bright sentinel
  that saturated most of the transition. This removes the following ground disk.
- Moment-shadow receivers now have the same 12 mm normal separation used by the
  default PCF surface path. Coplanar floor receivers no longer self-shadow over
  the square projection footprint. Actual blockers still cast shadows; volume
  sampling is unchanged.
- Game-owned cached views choose geometry around the station, independently of
  the player's visual LOD. Ordinary rendering retains camera-driven LOD.
  Authored hides, pose/content changes and quality still invalidate caches.
  Player movement no longer repacks GI transport and recaptures all probes/shadows.
- Splash placement uses cached physical roof heights, avoiding CPU reconstruction
  of filtered GPU rain-map samples. Wet materials still use the GPU rain maps;
  puddle masks, shoreline checks, splash varieties and rings remain.
  The deliberate difference is a physical roof test for CPU impact eligibility,
  without the GPU rain map's soft filtered shelter edge.
- An unowned D3D12 indirect-light view now uses the fallback descriptor when no
  cloud shadow exists. Native tests exposed this invalid-descriptor path.

These are normal rendering-path fixes across quality presets, not opt-ins.

## Verification and delivery

Worker evidence: WORKER_EVIDENCE_ROOT/phlosion-scene-cleanup-20261106.
Controller evidence: PROJECTS_ROOT/Games/PokemonCyberpunk/.phlosion/agent-work/scene-cleanup-20261106.

The final evidence and controller installation receipts are required before this
checkpoint is called delivered. Earlier failed native attempts are retained;
the first exposed the invalid cloud descriptor, and the next exposed a test
fixture which relied on the removed camera override for full GI weight.
That near-field fixture now explicitly requests its intended narrow fade.
Separate camera-near boundary cases retain the fade and independently verify it.

CPU contracts cover authored invalidation versus camera LOD, roof/water masks,
splash ownership and lifecycle. Native image checks cover both forward and
prepared GI receivers, bounded reflection fading, self-shadow footprints and
real blockers on D3D12/Vulkan. Host checks cover Low/Medium/High/Ultra, gameplay,
editor and reference-scene return, with the Hybrid weather configuration.

Timing runs are diagnostic movement replays, not clean sustained-FPS
benchmarks: telemetry records intermittent software thermal slowdown.
Do not use their GPU medians to claim a final 60-FPS target. The large CPU stalls
are independently localized to scene-wide cache resets and CPU shelter sampling.

## Diagnostic movement replay

1080p Medium, optimized RelWithDebInfo, EVGA RTX 2060 SUPER GPU1.
Frames 180-540 of the same walk. CPU tails diagnose hitches; these runs are
thermally contaminated and are not accepted as clean sustained-GPU benchmarks.

| API/weather | Previous CPU p99/max | Current CPU p99/max | Frames above 33.3 ms, previous/current |
| --- | ---: | ---: | ---: |
| D3D12, day | 134.267/165.269 ms | 14.419/14.976 ms | 46/0 |
| D3D12, heavy rain | 135.041/148.681 ms | 33.168/33.685 ms | 65/3 |
| Vulkan, day | 128.658/133.387 ms | 15.113/17.587 ms | 46/0 |
| Vulkan, heavy rain | 148.430/177.943 ms | 20.318/21.360 ms | 56/0 |

The isolated D3D12 rain-surface preparation maximum fell from 27.959 ms to
2.545 ms (median 5.106 to 1.875 ms). Smaller general frame spikes remain;
this is not a promise of hitch-free rendering or 60 FPS during heavy rain.
