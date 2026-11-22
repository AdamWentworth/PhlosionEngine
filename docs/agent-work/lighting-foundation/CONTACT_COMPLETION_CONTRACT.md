# Contact shading completion contract

Ordinary non-RT perspective primary views on Windows D3D12 and Vulkan; reference Mainline ConeAO plus retained retail consumer arithmetic. 100% is estimated mechanism coverage of this fixed scope, not retail pixel/performance identity.

- C1: Trace active caller, quality selection, five passes, formats and consumer ownership before implementation.
- C2: Half-resolution farthest-depth gather with BL/BR/TR/TL ties; associated velocity/normal and OR receiver flags; R32F depth, RG16F velocity, RGBA8 normals/cones.
- C3: All 5x3,10x6,15x10 horizon budgets; Bayer/Weyl variation, foliage stable sampling, depth-derived horizon normals, four-depth gathers at continuous UV, source range and cone moments, no-AO receiver exception.
- C4: 4x4 depth/normal spatial filtering and world-space directional cone; exact UNORM intermediate storage.
- C5: Object/camera/skinning/wind velocity reprojection; previous velocity agreement, depth/world agreement, four bilinear neighborhood cone clipping, 90% history ceiling, first/cut/resize/generation/skip reset.
- C6: Independent bounded view history, retirement, fence-safe resources, per-view depth/velocity/cone ping-pong; no history from another view.
- C7: Once-per-view depth-weighted full-resolution cone reconstruction; raw diffuse visibility drives BRDF coefficients; independently weighted indirect/direct diffuse and specular, colored diffuse multibounce, leaf-translucency dim and grazing response; preserve emission and ray-traced-path exclusions.
- C8: Normal game/editor/battle path, Low/Medium/High/Ultra policy; native independent stage oracle, moving/invalid/edge/material/lifetime fixtures both APIs; CPU contracts; screenshots including wet/night/foliage; optimized timings; exact reviewed integration and commits.

Outside this category: Ray-traced AO; Retail framebuffer identity; Other matrix categories; Orthographic and explicit legacy diagnostic path; must remain functional but not mislabelled as ConeAO parity.

Status: C1-C8 qualified for the fixed supported ordinary Windows path. See [completion evidence](CONTACT_COMPLETION.md) and the exact-integration acceptance records.


C7 audit correction: the original "preserve direct light" assumption was wrong.
Mainline include_lightingHelpers applies direct diffuse/specular controls, and
the retained retail event 7161 CB12 rows 51/52 confirm strengths
(1.2, .95, 1, .5), threshold -1 and leaf dim (.75, .5).
Capture SHA256 408a1ab1bd70e24a13e2734d88b7255a599f09a05018bd92561abbf6e08a5703.
Acceptance expands to those consumers; it does not waive a failed check.
