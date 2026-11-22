# Grazing reflection receivers

2026-10-20. Follow-up to [reflection continuity](REFLECTION_CONTINUITY.md).
All 203 selected native engine cases pass on Windows. Game/shared-consumer
qualification is recorded below, with strict image failures retained.

## Finding and change

The capture already contains Willie. A diagnostic raw RGBA16F readback of the
station's matched NPC-on/off controls finds 509 materially changed color texels
on the relevant cube face, with a maximum linear RGB difference of .51653.
His average captured RGB there is approximately (.1071, .0215, .0455).
The opaque character is present and lit; absence from the capture is not the
explanation for the weak final station reflection.

The receiver visibility check compared its radial distance with the distance of
the nearest capture texel, even though those two points lie on different rays.
At grazing angles that can reject a visible surface in bands. The shader now
intersects the receiver's own radial ray with the captured geometric plane before
checking the gap. The original .05 m radial tolerance remains unchanged. Dividing
by the plane/ray dot product is necessary: using signed normal distance instead
would permit a larger radial gap at grazing angles.

The change does not alter authored materials, light energy, probe resolution,
capture population, tracing step count, filtering or source blend weights.
Temporary D3D12 raw-readback instrumentation was restored byte-for-byte; there is
no new production readback or GPU wait.

## Reproduction and evidence

Local artifacts: `artifacts/lighting-foundation/reflection-character` in the
engine, PokemonCyberpunk and PokemonAutochess.

`PhlosionLocalReflection-<api>-receiver-grazing[-low]` tests a visible mirror at a
shallow angle, illuminated by a broad reflected source. All 400 world-space test
points are well inside that source's analytic footprint. At 64-texel capture
resolution the old calculation drops 100/400 samples; the corrected calculation
drops 0/400 on both APIs. The normal 256-texel control also retains all 400.
`receiver-hidden` offsets the probe behind a blocker while the main camera can
still see the mirror: the corrected path must keep that unavailable receiver's
reflected-source contribution below the existing 3/255 limit.

Early narrow-emitter fixtures conflated source-footprint error with receiver
rejection; their 850/2380 missing-pixel result is not the isolated defect metric.
They remain diagnostic evidence. The final broad-source fixture above isolates
receiver rejection. An intermediate Vulkan run also retained an older SPIR-V
file after copying a shader with its previous timestamp. The shader input was
touched, explicitly configured and rebuilt; `build-analytic-final.log` and the
final generated programs supersede that run. No image gate was relaxed.

The full native suite passes 203/203 cases with clean Vulkan validation. The
separate strict cross-API comparison is not entirely passing: 64/66 image pairs
remain within 1/255. `grazing-view4` retains the previous 25 pixels above that
limit (maximum 4/255). `handoff6` now differs at one pixel, (471,232), by up to
28/255; its full-image RGB mean difference is .00007921/255. Both APIs still
pass the existing analytic placement, footprint and continuity requirements.
`native-comparison.json` and `api-edge-diagnostics.json` retain the outliers;
the strict checker still exits unsuccessfully. These are unresolved edge
differences, not an identical-image claim.

A binary audit caught another timestamp issue: restoring the temporary readback
source left its disabled diagnostic branch in the engine/game executables. The
owned hidden editor probe was stopped, its incomplete log retained, and the
restored C++ source was touched and rebuilt. The final editor, runtime and native
test binaries no longer contain the diagnostic marker. Twenty focused cases pass
again after that clean relink; all 44 resulting images are pixel-identical to the
203-case run, and all 24 generated shader inputs remain unchanged. The editor
and both game runtimes embed the same current generated lighting source. Full
station/shared-consumer qualification uses these clean binaries.

The normal editor launch and all eight wet field/battle host/API captures pass.
The original full-HD actor-isolation gate fails on D3D12: 335 pixels outside the
prescribed reflection bounds change by 1/255, with none above that. Vulkan has
zero outside changes. A same-settings ordinary D3D12 actor-on repeat differs from
the first on capture at 356 pixels by at most 1/255, including exactly the same
335 outside pixels. That repeat and the actor-off control agree exactly outside
the bounds. This establishes a repeatability problem, not an actor-only spill.
It resembles the previously documented timing-sensitive discrepancy but does
not establish its cause. Two subsequent ordinary on captures are pixel-identical.
The original gate remains failed; those later captures do not erase that result.

All 28 close pump/bar, noon/sunset and camera-replay captures satisfy their scene
state and cross-API mean checks; the largest API mean difference is .337048/255,
below the unchanged 1.5 limit. Both five-visit normal scene-switch sequences pass.
The separate comparison against the prior implementation finds all seven D3D12
disabled-local-reflection controls pixel-identical. Its enabled night pump view
changes 46 pixels by more than 3/255, with maximum 7/255: the visible improvement
in that view is small. Vulkan's historical disabled controls are not identical:
9-307 pixels per view differ by more than 3/255, with maximum 59/255. Their scene
state matches. The cause is not established, so those differences must not all
be attributed to receiver correction. The added historical exact-image checker
continues to fail; the existing current-build API mean gate passes.

Autochess's two native material groups and all 17 editor cases pass on both APIs.
All 38 images are pixel-identical to the accepted `cpu-submission` baseline.
Separate shader-cache preparation completed within its existing watchdog; the
normal native/editor capture deadlines were unchanged. This is compatibility
qualification, not a performance measurement. The two Cyberpunk and four
Autochess CPU contracts pass, as do 18 targeted research tests. The final
graphics-log scan is clean. All 33 qualified binaries/generated inputs retain
their recorded hashes, and the paired editor/plugin and staged game DLLs agree.
`qualification-summary.json` consolidates these results alongside the original
strict comparison and NPC-visibility failures; it does not turn those into passes.

The raw station capture analysis reconstructs receiver positions from main-view
depth and compares both gap calculations. It identifies 60,548 pixels rejected
by radial-texel distance but accepted by plane-projected distance. This is a CPU
diagnostic using reconstructed geometric normals, not a count of visibly changed
final pixels. The receiver-mask images and raw data remain in the game artifacts.

## Remaining visual work

The original station NPC visibility gate still fails: the final on/off images
have 16 D3D12 and 2 Vulkan pixels above 3/255, below the unchanged requirement of
more than 100. Maximum differences are 7/255 and 4/255 respectively; scene and
animation state agree and the NPC is behind the camera. This remains an open
visual target. Screen materials have dielectric metallic factor 0 and roughness
.42; the worn housing uses textured roughness and normals. These properties and
the character's low radiance make weak, blurred reflections plausible. They are
not proof that all remaining weakness is physically correct. Do not brighten the
character, flatten the housing or lower roughness solely to force the gate.

The next evidence target is the complete ordinary specular-material response,
not another capture-population change. Retail PSO 20717 registers 2113-2212 apply
material controls and visibility after the already-qualified radiance blend and
DFG lookup. The retained b12/resource-956 readback has row 51.y = .95, but the b6
material controls also matter. Phlosion's existing `.44` specular-IBL scale cannot
be judged against that one constant alone. Qualify the complete response before
changing reflection strength. Existing captures provide the next starting point;
no new gameplay capture is required for that audit.
The research follow-up now qualifies that ordinary arithmetic with supplied
material controls over 262,144 inputs (maximum float32/float64 difference
2.35020e-6). The b6 controls were then recovered from submission-ordered CPU
updates; the corresponding b12 snapshot matches its prior GPU readback exactly.
The subsequent bounded selector follows the captured switches, buffer loads and
phi nodes over 82,240 supplied class/subtype/roughness combinations; its maximum
absolute error against an independent selector is 5.96046e-8. G-buffer decoding,
control-weight production and complete output remain unqualified, so this does
not establish a replacement production gain. See the research repository's
`lighting/reflections/README.md` material-response follow-up.

Start the next native comparison with a known linear emitter and controlled
receiver F0/roughness. Measure material response separately from source capture
and probe coverage, then compare the captured ordinary-material controls. In
particular, establish how the legacy neutral-environment scale interacts with
actual captured scene radiance before changing it. A global brightness increase
would not by itself qualify the recovered response or preserve other consumers.
Include the ordinary material roughness floor in that audit: Vulkan
`world_material.glsl` and the built-in D3D12 PBR path initially clamp it to .16,
where the retained retail composition uses .04. The engine wet-material path can
subsequently alter roughness, so compare dry and wet controls separately. This is
a confirmed code-path difference, not yet a qualified replacement setting.

This still uses one captured geometry layer. Hidden surfaces, thin or edge-on
features, rough-lobe parallax and view-dependent capture lighting remain limits.
This change does not recover Cyberpunk's probe producer or temporal filtering,
and does not establish the 90% visual target. Performance and Linux qualification
remain deferred.
