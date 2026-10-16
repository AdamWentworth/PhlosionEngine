# Rectangular world lights

`WorldEnvironmentLighting::rectLights` supports sixteen one-sided rectangular
emitters alongside the existing four point sources. Position and perpendicular
half-extent vectors are world-space metres. Cross(halfU, halfV) is the emitting
normal. Radiance is scene-linear; range provides a smooth finite cutoff.
Zero range disables a slot. Invalid packets fail closed; disabled environments
retain their existing inspection lighting.

Diffuse and GGX reflection integrate the rectangle using Linearly Transformed
Cosines, with horizon clipping and a stable normal-incidence tangent. The shared
shader serves OpenGL, Vulkan (including indirect materials) and D3D12. The
64x64 reference tables are packed into one backend-owned 128x64 RGBA16F texture,
65,536 bytes before API allocation overhead. See [source attribution](../third_party/ltc/README.md).
No per-fragment random sampling or clusters of point-source highlights are used.

Area sources currently have **no scene-geometry occlusion or local shadow maps**.
The emitting hemisphere and receiver horizon are respected; intervening objects
do not yet cast shadows from these sources. Directional sun/moon shadows remain
independent. This is direct surface lighting, not ray tracing, indirect bounce,
volumetric light scattering or an emissive-mesh GI system.

Visible glTF emission can be controlled separately using `GltfSceneDrawOptions`:
`emissiveFactors` accepts scene-linear RGB through 65504, retaining emissive
textures. Core glTF factors and base-color overrides remain normalized. These
values are tone mapped by the surface shader. Optional `IWorldBloom` adds a halo
around bright display-linear pixels with a normalized Gaussian kernel before display
encoding. It resets each world color pass, defaults off, and does not affect later
UI draws. It adds no illumination or volumetric scattering. Because scene color
is still tone mapped LDR, this is not pre-tonemap HDR bloom. Current bespoke screen
feedback composition uses its own original-color/history path during transitions.

Weather modifies PBR albedo/roughness before light evaluation, retaining mapped
normals. It does not add a uniform smooth coat over rough ground. Dedicated water
meshes use their low roughness, water reflectance and ripple normals in the same
light evaluation. Wetness fades across shelter edges; dry materials and emission
retain their original response. Legacy non-PBR surfaces receive dampening only.
These calculations do not capture reflections or occlusion of world geometry.

The CPU environment/frame expansion requires editor plugin ABI 36 and model
renderer ABI 16. Rebuild both hosts and their plugins; older binaries are rejected.
Auto-instancing keys include rectangular sources to prevent merging differently
lit instances. Uniform layout retains existing point and directional-shadow offsets.

`PhlosionRectLights` tests disabled/invalid sources, the last supported slot,
Vulkan packing and all 4,096 LUT transforms. `PhlosionEngineContracts` covers
model propagation and instancing. The game consumer additionally verifies
fixture geometry and per-fixture condition control, with neutral polished/diffuse
native controls on each API/host. See the consumer's `Verify-Rect-Lights.ps1`.
