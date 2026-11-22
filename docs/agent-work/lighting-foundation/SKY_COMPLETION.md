# Sky, sunlight, directional shadows and day/night weather

Status: implementation and pre-integration qualification complete.
**100% estimated coverage of the fixed A1-A8 ordinary Windows technique** is
accepted only with the passing locked-source and publication receipts below.
This is not a retail pixel/FPS identity claim.

This category covers the connected ordinary raster sky/weather pipeline on
Windows D3D12 and Vulkan, runtime and editor, all four normal quality presets.
The following gates are fixed before implementation. Source research is private;
production implementations are independent and public datasets retain licenses.

## Completion gates

- A1: Hosek-Wilkie RGB coefficient preparation, turbidity/ground albedo,
  horizon handling, separate dome/skylight values, celestial presentation and
  scene-linear output; independent reference values.
- A2: Prepared weather/volume noise, volumetric density and detail shaping,
  bounded ray integration, lighting/phase and cirrus composition.
- A3: Camera-snapped multi-height cloud shadow production shared by sky,
  sun receivers and fog; source scale conventions and edge behavior.
- A4: Depth-visible cloud work, tier budgets, depth-aware reconstruction,
  scene temporal integration, cuts, resize and scope lifetime.
- A5: Stabilized camera-relative directional cascades, filter/bias, transitions,
  distant coverage and static/dynamic invalidation; no fixed 64 m shadow box.
- A6: Consistent sky/sun/weather snapshots for opaque, transparent, rain, fog,
  indirect relighting and placed/global reflections, with explicit source adapters.
- A7: Continuous game-owned day/weather clocks and authored presets, paused
  updates, scrubbing/reset, field/battle and editor scene return.
- A8: Native numerical/render evidence, both ordinary hosts/APIs, normal presets,
  optimized GPU/CPU costs, prepared shaders and locked publication.

Existing Hybrid material-specific splashes and real puddle rings stay.
Sandstorms, shared gust mechanics and human hair deformation retain their own
matrix categories. Game location, clocks, colors and weather textures are inputs.
Completion means the above connected source-traced technique, not retail
pixel identity or performance identity on different assets/settings/hardware.

## Reference anchors

Private supplied Mainline:
- engine/shaders/skyColor.fx, include/include_hosekSky.fx;
  dev/src/common/renderer/src/renderSkybox.cpp.
- engine/shaders/volumetricClouds.fx, include/include_volumetricClouds.fx;
  renderVolumetricClouds.cpp and renderNode_VolumetricClouds.cpp.
- shadowCascade.cpp, cascadesStabilizer.cpp, renderShadowManager.cpp,
  renderDistantShadows.cpp, include/include_shadows.fx, globalShadow.fx.
- runtimeSystemWeather.cpp and the environment atmosphere parameter producers.

Public RGB coefficients: Hosek-Wilkie 1.4a, Charles University,
https://cgg.mff.cuni.cz/projects/SkylightModelling/ (BSD-3-Clause).
Worker receipts: WORKER_EVIDENCE_ROOT/phlosion-sky-20261104.

## Connected implementation

| Gate | Production path | Independent/controlled evidence |
| --- | --- | --- |
| A1 | WorldSky prepares the public RGB model; world_sky_model and world_atmosphere consume separate dome/skylight inputs, celestial radiance and the authored atlas. | 320 independent public-model values; native RGB controls, sun excluded from indirect sky, celestial clock changes, filtered cirrus illumination. |
| A2 | WorldSkyAssets prepares weather/volume textures; WorldClouds packs the view; five compute stages prepare skylight, shadows, visible work, integration and reconstruction. | Native constant-density/height-profile controls, finite radiance/transmission, all four budgets and cross-API readback comparisons. |
| A3 | A snapped 4096 m cloud-shadow map carries four height channels; forward, shared-surface and fog consumers share its snapshot. | Independent integrated shadow-profile values and shared native fog/precipitation tests. |
| A4 | Depth-visible tiles append indirect work; half-resolution radiance/depth reconstructs before ordinary scene temporal accumulation. | Opaque roof admits zero cloud tiles; cuts, a second scope and odd resize preserve coverage and remove stale output. |
| A5 | WorldSunShadowPlanner stabilizes near cascades and schedules four distant static levels with a retained blending slot; D3D12/Vulkan own the atlas and sampling. | CPU fit/snap/invalidation controls; native lit, occluded, moved-caster and distant coverage on every tier; paused sun/camera/settings edits invalidate immediately. |
| A6 | Both game presentations prepare the same sky/cloud/sun snapshot before surface, fog, particle, GI and reflection work. Probe sky uses the indirect model and capture-camera rays. | Native precipitation/fog consumers and ordinary field/battle qualification. |
| A7 | Game-owned continuous clocks, independent weather cadence and existing preset/pause/scrub controls feed snapshots; scene resource scopes own history. | WorldEnvironment ownership/rate-independence tests; runtime/editor cycles and scene-return matrix. |
| A8 | Optimized warnings-as-errors builds, prepared programs, normal host/API presets, measured costs and locked package publication. | CPU/native and ordinary-host checks pass; final locked-source/publication receipts are mandatory. |

## Representation choices and limits

The equations, production stages, consumers and invalidation dependencies are
traced against the supplied reference. Resource representation and game inputs
remain explicit adapters:

- The authored weather/noise, moon, star and galaxy images are independent
  procedural inputs packed in one atlas. They are not the retail game's artwork.
  Stars use six 512-pixel faces. Game brightness accounts for the shader's
  1000-times radiance convention.
- Cirrus receives the equivalent mip-3 sky lookup: each of its 16-by-16 values
  averages the corresponding 8-by-8 footprint of the 128-by-128 paraboloid.
  It does not resample the unfiltered analytic model at a single point.
- Cloud visibility reads the opaque depth tile directly instead of consuming a
  separate prebuilt maximum-depth texture. Tile admission, indirect integration,
  depth-aware reconstruction and scene temporal accumulation remain connected.
- Low keeps analytic sky, celestials and filtered cirrus while disabling volume
  cloud integration/shadows. Medium/High/Ultra retain their explicit source-tier
  step and resolution budgets; these are normal quality settings.
- Sun cascades and distant levels occupy one depth atlas. The scheduler renders
  one changed distant level per transition rather than the reference engine's
  finer-grained collection/render job staging. Camera cuts, changed casters,
  paused edits and tier/scope changes invalidate the appropriate records.
- The renderer uses the engine's existing scene temporal reconstruction and
  scoped resource ownership. Cloud ray integration in a probe follows that
  probe's camera; it cannot reuse the main camera's resolved cloud/fog image.
- The existing distant GI heightfield shadow remains its own heightfield march,
  matching that producer's role. It is not replaced by screen-space sun shadows.
- Sun and moon share the active directional shadow producer. Game-owned colors,
  weather textures, scene dimensions and clock speeds are not retail identity.

Sandstorm production, shared world gusts/hair response, ray-traced paths, other
platforms and exact retail pixel/FPS equality are outside these fixed gates.
They are not silently included in this category's completion claim.

## Validation notes

The native public RGB model's maximum relative error is 0.000005365; the native
shader comparison is below 0.001370. Cloud cross-API readbacks differ by at most
0.00390625 absolute (0.000889 relative). Both APIs' native sun control reads
0.950684 lit, 0.0220337 blocked and 0.950684 after removing the blocker.
All comparisons use the precision budgets encoded in the respective tests.

The full game exposed a stale MSBuild Vulkan object whose recorded dependency
list stopped before the internal renderer header. A clean rebuild of the game's
engine_render library removed the startup access violation; the final host
matrix uses that rebuilt binary. This was not worked around by changing
renderer behavior or skipping Vulkan. The previous installed editor stays
unchanged until locked qualification and replacement publication are ready.

Worker evidence is retained under WORKER_EVIDENCE_ROOT/phlosion-sky-20261104:
sky-engine-cpu3.log (52 CPU contracts), complete3-* (connected native consumers),
stars-* (final authored atlas), sun-paused-cpu.log, game-environment-test-final6-direct.log,
and sky-final7-* (latest ordinary host matrix). Earlier failed attempts remain
diagnostic records, not passing acceptance evidence.

The active sun-to-volume controls also pass on both APIs and all tiers:
0.0944824 unblocked, 0.000399351 under the blocker, and 0.0944824 after moving
it away. This exercises the actual atlas binding and volume consumer, including
distant fallback, rather than only checking an empty shadow uniform.

## Ordinary qualification and measured cost

All 22 final7 runtime/editor/API/preset/cycle/scene-return checks pass with API
validation enabled. Four strict7 checks additionally require prepared programs.
The clear-night7 game view checks the final game-owned star brightness.
The 52-test CPU suite, independent native comparisons and strict build/boundary
checks pass; final7-sun-* adds active fog consumers and the paused-update fix.

Worker EVGA GPU1, 1920x1080, Medium, optimized RelWithDebInfo. Heavy results are
means of two medians from before/candidate/candidate/before runs; dry results
are paired medians. Each uses frames 121..239 (119 samples), excluding startup.
No invalid timestamp frames or dropped samples are accepted.

| API | Dry before | Dry current | Heavy before | Heavy current |
| --- | ---: | ---: | ---: | ---: |
| d3d12 | 11.409 ms | 13.679 ms | 14.811 ms | 17.494 ms |
| vulkan | 11.855 ms | 14.263 ms | 15.110 ms | 17.598 ms |

The original dry Vulkan candidate reported a sampled slowdown flag; it and its
paired baseline are excluded. Both dry runs were repeated successfully.
performance.json and timing-rejected.json retain individual results and reasons;
raw phase and GPU telemetry logs remain beside each run.

The more complete sky/cloud/directional-shadow path adds about 2.3-2.7 ms in
these cases. Heavy rain remains above the 16 ms target. This is a technique
completion milestone, not a performance improvement. Further cost work must
profile these producers and preserve the completed calculations, quality tiers
and Hybrid surface-specific splashes/puddle rings.

## Locked release acceptance

Editor ABI60 and model ABI32 require matching engine/game providers.
The prepared publication is scene-shaders-v1/2026-11-04-sky (187 programs,
11,918,269 bytes including its program manifest). Its asset publication hash is
141f609970ff6cf6d218c2902ab36857d8ba889b5058998d35adfca7002f432d.

Final acceptance requires all of:
1. Reviewed source delta integrated into the unchanged controller baseline.
2. Game dependency lock pinned to that engine revision; worker source identical
   to the canonical commits, locked dependencies and warnings-as-errors enabled.
3. Optimized final builds plus locked runtime/editor D3D12/Vulkan checks with
   prepared programs required and no API validation errors.
4. A clean-source publish_editor_build archive and matching shader publication;
   hashes verified when installed on the controller.

The immutable worker records are locked-qualification.json, editor-publication.json
and shader-publication.json under WORKER_EVIDENCE_ROOT/phlosion-sky-20261104.
The controller installation receipt is retained in the matching codex-infra
work/sky-20261104 directory. Do not treat the milestone as accepted if these
release receipts are absent or fail.
