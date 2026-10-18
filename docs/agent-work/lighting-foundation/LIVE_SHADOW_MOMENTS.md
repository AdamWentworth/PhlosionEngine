# Live distance-moment shadows

The recovered filter in [SHADOW_MOMENTS.md](SHADOW_MOMENTS.md) now has a native
OpenGL, Vulkan and D3D12 integration, functionally accepted on 2026-10-18 with
the performance/startup limits below. Evidence is retained under
`artifacts/lighting-foundation/live-moments`; see CHECKPOINT.md for current status.

## Adapter and ownership

`WorldRectLight::shadowFilter` selects PCF (the engine default) or distance
moments. `rectLightShadowView` retains the existing 150-degree perspective
projection, near plane at the luminous face and current caster geometry. It
supplies `(far/near, 1-far/near, tan(fov/2), 1/far)`. This projection/source-size
policy is Phlosion's adapter, not recovered Cyberpunk authoring behavior.

After each local depth tile, two ordered full-screen GPU draws run the unchanged
qualified `world_shadow_moments.glsl` core with five taps. Both stores use
RG16_UNORM, including quantization between passes. Texture fetches use integer
coordinates, and rays are evaluated at unclamped tap coordinates. This stable
addressing intentionally differs from the retail normalized-coordinate
roundtrip described in SHADOW_MOMENTS.md; live images are not asserted bit-exact
to the retail capture.

The existing D32 atlas, cutout/skinned/wind-deformed caster shaders, sun PCF and
submission/culling remain in use. All scene casters populate the depth input
each frame; the cached-depth operand is one. There is no separate static cache,
and no claim of matching retail cache construction or invalidation.

Each prepared local-shadow renderer owns a 1024-square intermediate and a
2048-by-4096 final atlas, 36 MiB total. Vulkan owns this pair for each in-flight
frame (72 MiB with its two frames). Preparation allocates targets and compiles pipelines outside frames;
filtering allocates nothing. Each source writes one isolated 1024-square tile.
Vulkan dependencies and D3D12 transitions order depth -> horizontal -> vertical
-> receiver reads. OpenGL uses distinct framebuffer attachments for each draw.

The existing six-vec4 per-light receiver packet remains fixed-size. Negative
parameter w selects moments, with y=`1/far`, z=`tan(fov/2)` and w=`-1/resolution`.
Surface and haze receivers use the same radial distance and four nearest fetches
for bilinear moment interpolation, clamped within the light's tile, followed by
the recovered `finiteLightMomentVisibility`. PCF retains its original bias and
receiver-plane correction. Reflection views use the same world-space maps.

Public C++ layout changes require model ABI 19 and editor project ABI 39; hosts
and plugins must be rebuilt/staged together. PokemonModelSupport owns no new
graphics implementation.

PokemonCyberpunk selects distance moments for its six canopy sources in both
exploration and battle. `PHLOSION_USE_PCF_LOCAL_SHADOWS=1` restores the previous
filter without changing source response, radiance, scene, weather or camera.
`Verify-SkyWeather.ps1` and `Verify-Canopy-Neon.ps1` expose `-UsePcfLocalShadows`;
handshakes record all sixteen `local_shadow_filters` values. The existing
`PHLOSION_USE_LTC_RECT_LIGHTS=1` response override remains independent.

## Verification

The new `PhlosionLiveMoments-*` tests render eight sparse-indexed light tiles,
alternating blockers and empty maps, a partial blocker, repeated map updates,
clearing stale occlusion, the PCF control and a receiving plane casting onto
itself. `PhlosionLocalHaze-*-moments-*` applies the existing independent volume
oracle to full/reduced-resolution haze, blockers and overlapping colors.
CPU tests independently reconstruct radial distances and validate receiver
packing. Existing native finite-response and moment math fixtures remain intact.

The final stable build passed 169 affected engine tests and six game CPU
contracts. Native controlled images differ across APIs by at most
0.000046875/255 mean absolute error; self-casting flat ground is pixel-identical
to the empty-map control on each API. The unchanged qualified filter-core SHA-256
is `0f00aaf8c604fdb57f2e6b88c2815074d73deec9a41acdbe714a381d8bb26bac`.

Normal D3D12 editor launch and all six station runtime/editor captures pass,
including actual moment selection for the six canopy sources. Maximum station
API mean absolute difference is 1.078028/255, below the existing 1.5 limit;
maximum runtime/editor difference is 0.000013698/255. Resource-allocation
failures are zero. No image threshold was relaxed.

An initial default-selection failure exposed an object built while its source
was being edited. The first profile assertion and capture correctly retained
PCF. A subsequent link also found the owned capture process still holding its
executable. Both failures remain in artifacts. Stable rebuilds of all affected
hosts, plugins and tests, then fresh captures and handshakes, supply the final
evidence. Code was unchanged during those rebuilds and checks.

All six rainy battle captures pass their unchanged geometry, fixture, weather,
shelter and API/host image checks; their handshakes confirm moment selection.
Seven day/dry/off/oblique controls and seven scene-reflection controls pass the
existing predicted-footprint, puddle, bloom, reflected-actor, sky and dry/legacy
guards. The lights-off image is pixel-identical to the preceding finite-source
checkpoint. Four isolated source captures show 1,270 of 2,408 reviewed character
pixels responding independently to both colors, with all 2,408 responding to
blue. These display-image checks do not measure linear radiance addition.

The new and preceding PCF front images differ by 0.247192/255 mean display RGB,
with 13,254 pixels changing by more than three levels. This measures the extent
of the deliberate filter change, not a quality score or retail parity. Normal
launch and five station/reference visits in each API's editor scene-switch probe
pass, including resource stabilization. Front, oblique and reflected views were
also visually reviewed.

Autochess's two runtime character sets pass all six API captures, parity pairs
and 36 expected-content guards. Its eight shared character-material contracts
also pass. The paired editor/project plugin proof verifies ABI compatibility
and exact source fingerprints. The first artifact wrapper incorrectly checked
PowerShell's stale `$LASTEXITCODE` after a script that reports failure by throwing;
both matrix scenes and every content check had passed. That wrapper failure is
retained, its check now reads the matrix report, and editor verification resumed
separately without rerunning or discarding successful runtime captures.
All six editor body/eye and stage-lighting views then pass their existing
content, API-parity and baseline checks; all six model crops are pixel-identical
to their accepted per-API baseline. Windows PowerShell runs the existing
image verifier; the retained artifact helper uses the previously qualified
600-second cold-start allowance, with unchanged image thresholds. Autochess
and PokemonModelSupport source remain unchanged.

## Matched cost

On an RTX 2060 SUPER, driver 617.42, sequential 1920x1080 wet-night captures use
200 warm samples (frames 30 through 229). Recorded imports, exploration state
and environment match, except for the shadow-filter tags. All GPU samples are
valid and allocation failures are zero. No builds or other test renders ran
during timing. PCF and moments both use the new prewarmed resource layout.

| API | PCF GPU median | Moments GPU median | PCF host median | Moments host median |
| --- | ---: | ---: | ---: | ---: |
| GL | 40.315 ms | 42.983 ms | 42.652 ms | 45.188 ms |
| Vulkan | 21.740 ms | 21.158 ms | 53.423 ms | 52.748 ms |
| D3D12 | 21.180 ms | 20.827 ms | 21.687 ms | 21.843 ms |

These are full-scene observations, not isolated filter timers or 60 FPS
qualification. GL's +2.668 ms GPU and +2.536 ms host changes exceed the existing
+1.5/+2 ms environment review thresholds. A reverse-order GL repeat, also 200
valid warm samples per mode with matched state, measures PCF/moments GPU
39.934/40.397 ms and host 42.600/42.286 ms: +0.463 ms GPU and -0.314 ms host.
Both observations are retained. The initial threshold exceedance is not erased
by the smaller repeat; GL cost is variable and remains a performance-review
item, rather than a demonstrated fixed overhead or speedup. Vulkan remains
host-limited. The station default is functionally accepted with these declared
cost and startup limitations; the PCF comparison remains available on every API.

## Evidence and reproduction

Engine and game evidence is retained under each repository's
`artifacts/lighting-foundation/live-moments`. Engine `stable-acceptance-fixtures.log`
contains the final 169-test run; `native-image-parity.json` records controlled
image comparisons. In the game, `final` holds station/battle/control/reflection
captures and their verification JSON, `overlap` holds isolated colors,
`scene-switch-*` holds five-visit probes, and `normal-launch-final` holds the
normal launch. `controls-verification.json` compares the preceding checkpoint
and character regions. `cost/summary.json` and `cost-repeat/summary.json` retain
both timing observations and all source logs/handshakes.

Build the engine native fixtures and all affected hosts/plugins before running
`ctest --test-dir build-presentation -C RelWithDebInfo --output-on-failure` with
the affected test selection. In the game, run `Verify-Canopy-Neon.ps1` stages
`matrix`, `controls`, `battle`, `reflections`; `Verify-Canopy-Overlap.ps1`; and
`Launch-Editor.ps1 -SkipBuild -Probe -ProbeSceneSwitch -Renderer <api>` on each
API. Apply the existing station (`--shadow-filter moments`), neon, battle and
planar-reflection validators. The retained `qualify-rest.ps1`, `measure.ps1` and
analysis scripts record the exact paths and options used for this checkpoint.

Cold first-use captures after the shared shader change took several minutes
(about five minutes for the game Vulkan runtime/editor and six minutes for the
first Autochess Vulkan editor view). These are end-to-end launch
durations, not isolated compiler timings. Warm frame measurements above do not
resolve cold startup. No private retail capture, extracted assets, shader
bytecode or readbacks are committed with the implementation.

## Remaining parity targets

The live recovered filter and visibility calculation close this integration
step. Source-to-shadow projection/normalization still follows Phlosion's
150-degree adapter, and static caching is absent. A populated cache in the
retained capture cannot establish its construction or invalidation rules.
Material decoding, other emitter branches, indirect transport, reflections and
display response still need their own matched-input evidence. The retained
retail mode has RT disabled; this work does not establish ray/path-tracing parity.
