# Reflection material response

2026-10-19. Wet planar reflections now reuse the already bound LTC GGX magnitude
and Fresnel moments instead of the previous analytical DFG fit. The source table
is the engine's existing licensed, non-retail data. Only the planar reflection
response changes; the dry fallback, viewer IBL, direct lights, diffuse transport,
artistic .44 reflection scale, source radiance, blur and receiver coverage remain.

The table's coordinates are roughness and sqrt(1-NoV), with 64x64 texel-center
addressing in the right half of the packed texture. Magnitude stores A+B and the
second channel stores B, so reflected radiance is multiplied by F0*(magnitude-B)+B.
The roughness floor is .04, consistent with the studied material path. This adds
one read from an existing texture, without new allocation, descriptors or ABI.

The separate Cyberpunk2077Research study read event 7161's DFG table and compared
131,841 angle/roughness samples. Mean absolute normalized response error drops
97.81% for F0=.02 and 97.51% for F0=.04. The worst remaining F0=.02 absolute error
is .05948 at extreme grazing incidence. This is an approximate DFG component
comparison, not a full reflection/frame or whole-lighting parity result. Retail
material-specific controls, SSR construction, temporal resolve, probe blending
and indirect occlusion remain separate targets.

For the active planar roughness range .04-.55, mean normalized error falls
96.99% at F0=.02 and 96.69% at F0=.04. These figures still exclude the artistic
scale, coverage fade and all image-composition behavior.

The first game comparison exposed a separate integration defect: Vulkan's
composed `surface_lighting.glsl` never called `worldPlanarSpecular`, although the
native/default material did. Its wet station image was unchanged by this pass
while D3D12 changed. The fragment now evaluates the reflection before specular
scale/occlusion, matching the other paths. The composition contract requires that
assignment, and the game scene-reflection check now requires a visible on/off
delta on every enrolled API. Broad API image-similarity checks alone missed it.
Initial captures are retained under the game's `final` artifact directory;
accepted captures go under `qualified`, with hashed provenance for unchanged
D3D12 copies and fresh Vulkan captures after the fix.

Research details: `PROJECTS_ROOT/Research/Cyberpunk2077Research/lighting/reflections/README.md`.
Production evidence: `artifacts/lighting-foundation/reflection-response` in the
engine and game. The initial shader compile caught a declaration-order issue;
the shared lookup now lives in world_lighting with a forward declaration in
world_planar, covering both composed and native material paths.

The initial native off-screen fixture fell below its existing contrast threshold
because the corrected DFG reduces its response at that viewing angle. The fixture
now uses an explicitly luminous red object outside the main view. Its geometry,
projection test and visibility threshold are unchanged; both APIs pass. The
initial failures remain recorded. The other 30 reflection/HDR/material checks
passed before that fixture-only change, including dry/off/reset and emissive
texture/roughness checks. Scene and cost qualification is recorded in CHECKPOINT.md.

The final game receiver check passes on both enrolled APIs, including an explicit
Vulkan on/off pair. The eight scene views, two normal launches and Autochess's
four native/34 editor captures pass. Warm 1080p wet-night GPU medians change by
+0.046 ms on D3D12 and +0.574 ms on Vulkan; the latter also enables the missing
receiver. These full-frame deltas are not isolated lookup timings. Cold shader
compilation remains slow and exceeded the first shared-consumer test timeouts;
warm qualification retains the original startup and image limits.
