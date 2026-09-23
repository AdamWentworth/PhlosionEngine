# M3 animation blend foundation

The engine's first asset-independent M3 pass adds CPU pose blending and timed
events. `AnimationBlend.h` accepts a base clip or bind pose, an optional
crossfade target with a weight from 0 to 1, and ordered override layers. Each
layer can affect every node or an explicit node mask. The shared sampler still
defines clip loop/endpoint behavior and horizontal root-motion suppression;
`composePose` rebuilds global transforms with the existing hierarchy and
segment-scale-compensation rule after local blending.

`sampleGltfSceneBlendedPose` exposes the result on the public glTF scene asset.
It builds mesh-relative skin palettes in the same space as `sampleGltfScenePose`,
records the asset generation, and leaves the previous output unchanged when a
request fails. The renderer's existing `drawGltfSceneAsset` path consumes this
pose without a backend-specific blend implementation.

Translation and scale use linear interpolation; rotation uses normalized
quaternion slerp. Layers are overrides in request order, not additive. A
matrix-authored node passes through when both inputs have the identical matrix.
An in-between blend of differing matrix-authored transforms is rejected with
an error rather than decomposing a matrix that could contain shear. Zero and
full weights preserve their respective endpoint exactly. Node masks list node
indices explicitly; they do not imply descendants.

`collectAnimationEvents` returns markers in `(previous, current]` on a
nonnegative, unwrapped clip timeline, including loop crossings. `includeStart`
fires a marker at the activation boundary, and `looping=false` limits a
one-shot attack to its first cycle. It orders
same-time markers by their authoring order and caps catch-up at a caller-set
limit (256 by default). Consumers reset their own cursor on seek/cancel and
attach battle/VFX/audio semantics to marker names. A failed request preserves
the caller's previous event list.

## Verification

`PhlosionEngineContracts` checks a two-node synthetic crossfade and masked
layer with independent transform expectations, endpoint weights, invalid masks
and transactional errors; it also checks event ordering, loop boundaries and
bounded catch-up. Its public glTF rig checks the blended skin palette against
an analytic half-turn and verifies pose generation identity.

The animation fixture adds `crossfade-quarter`: bind to a cubic clip at 1 s,
weighted 0.25, with an analytic -30 degree tip angle. The native matrix runner
captures seven cases in the standalone runtime and real editor on OpenGL,
Vulkan and Direct3D 12. The guard checks expected pixels and geometry,
provenance, pairwise parity and cross-case change. The hermetic guard suite
also covers malformed evidence and recovery. See `artifacts/m3-a/` for local
captures and logs from this pass.

This foundation does not yet include a parameter-driven state machine,
additive layers, authored event metadata/cooking, battle choreography or
roster-asset qualification. Those remain M3 work; the final battle exit gate
also requires camera, UI, audio, VFX and game integration.
