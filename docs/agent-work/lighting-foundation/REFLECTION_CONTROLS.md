# Authored reflection response

2026-10-21. This follows the native material study described in
`Cyberpunk2077Research/lighting/reflections/MATERIAL_NATIVE.md` and the engine's
[unit-response correction](REFLECTION_MATERIAL_RESPONSE.md).

## Implementation

`WorldEnvironmentLighting` exposes four per-draw reflection controls: normal
scale, grazing scale, bias and extra grazing response, plus a blend weight and
an optional albedo-derived grazing reflectance. Weight zero is an exact opt-out;
existing callers retain their previous response. Invalid coefficients disable
the adjustment, valid coefficients are bounded to [0,4], and weight to [0,1].

Both Windows backends append two vectors to the view data. The existing nineteen
environment vectors, shadow offsets and D3D12 material/outline offsets remain
unchanged. D3D12 checks that view constants fit before the material block.
Vulkan's cached view state includes the appended controls. Consumers must rebuild
their editor plugins against the changed C++ structure; old binaries are not ABI
compatible. OpenGL retains neutral response and is not qualified for opt-in use.

The shared reflection shader evaluates the licensed GGX moments A and B:

```
q = weight * visibility
a = 1 + (normalScale - 1) * q
b = 1 + (grazingScale - 1 + extraGrazing) * q
response = clamp(a*A*F0 + b*B*F90 + bias*q, 0, 1)
```

With the albedo option enabled, F90 blends from 1 toward
`clamp(50 * sum(linearAlbedo), 0, 1)` with the authored weight. This preserves the
neutral opt-out even for very dark materials. The weight-one ordinary profile
uses the recovered formula. Weight blending is an engine authoring policy, not
a claim that every retail material branch is reproduced.

The adjustment multiplies the already-composed incoming reflection radiance.
It does not change the direct-light BRDF, light intensity, exposure, diffuse
energy split or existing multiple-scattering compensation. The engine supplies
its existing minimum of material AO and contact visibility for `visibility`;
this is an approximation to the captured diffuse-visibility producer.

PokemonCyberpunk opts field/battle static surfaces into `(1.2, 1.3, 0, 1)`, weight
one, with albedo F90. Explicit water draws opt out. Characters, foliage and
neutral model previews do not acquire these controls. The game owns this profile;
the renderer does not encode Cyberpunk material IDs or station coordinates.
`CYBERPUNK_DISABLE_REFLECTION_RESPONSE=1` and the capture tool's matching
`-DisableReflectionResponse` switch restore the previous response while keeping
the reflection sources enabled.

## Independent numerical evidence

The research analyzer uses 893,289 decoded class-0 pixels from the qualified
event 7161. It substitutes the engine's licensed table and the ordinary controls
while retaining native view, radiance, material fields and visibility inputs.
Mean absolute linear-RGB error falls from .0781305 to .000322112; RMSE falls
from .237613 to .00159967. Maximum absolute error falls from 16.8539 to .375488.

The 328 subtype-21 pixels require a different extra-grazing control. Selecting
their recovered controls reduces their mean error from .00417549 to .0000330239.
The generic engine can express these values, but the game does not infer retail
subtype IDs. No captured table or retail shader is shipped.

These supplied-input comparisons isolate the material response. They do not
qualify engine reflection coverage, visibility generation, special material
classes, reflected-character detail, or a complete retail image. They are not
a percentage of whole-lighting parity. The prior native replay's validation and
cross-run repeat failures remain documented in the research study.

## Qualification

Evidence for this implementation is retained under
`artifacts/lighting-foundation/reflection-controls-20261021`, with final engine
and consumer checks in `final/`. The initial D3D12 response run failed shader
compilation because a translated GLSL scalar vector constructor was invalid
HLSL. The explicit three-component constructor fixes that without changing
the formula or numerical limits; the failed logs are retained.

The unit-radiance fixture covers normal/grazing views, dry/wet surfaces,
ordinary/built-in materials, metal/dielectric reflectance, very dark albedo,
partial control weights, partial occlusion, and unchanged direct highlights.
The original <2/255 reflection and <=1/255 direct-light limits are retained.
All 260 selected engine cases pass: 207 GPU and 53 CPU checks, with clean
graphics validation. The 56 response GPU cases include 48 reflection and eight
direct-light controls. Worst reflection error is .824096/255; worst direct
preservation error is 1/255. All 28 response API pairs satisfy the original
1/255 image limit. All 20 neutral response images are pixel-identical to the
previous production results.

The separate strict screen/local reflection API comparison still fails its
existing `handoff6` edge case: maximum 26/255, mean .0000683594/255. The other
65 pairs meet the unchanged 1/255 gate. Camera motion, grazing and thin-source
position checks pass. This pass does not claim to fix the retained edge failure.

## Station integration

Normal launch, all eight wet field/battle host/API views, 28 close/night/daylight/
sunset/orbit images and both station/reference scene-switch sequences pass their
content and API checks. The maximum close-view API mean difference is
.331539/255 against the unchanged 1.5/255 limit. All compared scene, weather,
camera and animation states match. Thirteen of fourteen disabled-response views
are pixel-identical to previous production; the remaining D3D12 noon view has
a maximum 1/255 difference.

The change is localized in the station views, without increasing lamp output or
exposure. Pump/bar surfaces and the ground have a different angle-dependent
response; the wide wet view retains its existing color balance. The offscreen
NPC control clears its original >100-pixel visibility gate on both APIs: 249
D3D12 and 231 Vulkan pixels exceed 3/255, up from 147/133. Maximum change is
16/15 respectively. This remains a faint contribution, not a complete, clear
character reconstruction.

Strict reflected-player isolation passes on Vulkan but **fails on D3D12**:
394 pixels outside the permitted reflection region differ, maximum 2/255.
The same-input ordinary D3D12 repeat also fails exact equality: 419 changed
pixels, 442 channels, maximum 2/255; 394 of those pixels are outside the region.
Scene/animation inputs match and there are no graphics-validation errors.
An additional on/off/repeat control with the authored response disabled is exact
outside the reflection region and repeat-identical in this run. It does not
establish the cause or prove the authored failure independent of this change.
The previously recorded D3D12 repeatability weakness remains open; no threshold
was widened and no passing retry replaces either failed capture.
The retained actor-off image and ordinary repeat agree exactly outside the
reflection region; both differ there from the initial actor-on image. The
spatial diagnostic records this without attributing a cause or accepting the
failed initial capture.

The game retains `actor-isolation-diagnostic.json`, `repeat-diagnostic.json`,
`repeat-spatial-diagnostic.json` and
the original failing verifier result separately from the passing material/API
comparisons. `comparison-pump.png`, `comparison-bar.png`, `comparison-sunset.png`
and `comparison-wet.png` show matched previous/current production views.

## Shared consumers and final record

Autochess's two standalone material groups and seventeen editor cases pass on
both APIs. All 38 images are pixel-identical to the accepted baseline, with clean
graphics validation. Its paired editor/plugin check passes and its tracked
source is unchanged. The first cold-cache preparation took 478.4 seconds on
Vulkan and 1209.8 seconds on D3D12; these are shader-preparation observations,
not frame-performance results. Qualified image deadlines were unchanged.

PokemonCyberpunk's two CPU contracts, Autochess's four CPU contracts and all
twenty reflection-research tests pass. Final checks preserve all 542 engine and
256 game source-input hashes and all 34 binary/generated-input hashes. The
three renderer hosts embed the intended lighting source, the editor/plugin
pair agrees, and all three staged Cyberpunk DLLs match their built counterparts.

`final/qualification-summary.json` consolidates these results and explicitly
sets `qualification_passed` to false for the retained native API edge comparison,
reflected-player isolation and ordinary D3D12 exact-repeat failures. Generating
the report successfully does not turn those failures into passes. This is
Windows D3D12/Vulkan visual evidence; Linux, performance and RT-enabled retail
parity are not newly qualified. Matched station images support a modest visual
improvement, not a claim of reaching 90% overall visual parity.

## Remaining target

The material calculation now has native evidence and an opt-in implementation.
The larger remaining visual work is reflection coverage/detail and the generation
of directional contact visibility. The research repository's
`lighting/reflections/VISIBILITY_PRODUCER.md` maps the existing five captured
dispatches and their constants. Matching their intermediate outputs, including
filtering/history behavior, is the next bounded investigation; a new retail
capture is not required to start it. The D3D12 exact-repeat failure must also
remain visible in qualification rather than being absorbed into image tolerances.
