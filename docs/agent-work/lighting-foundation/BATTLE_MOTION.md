# Battle motion producers — 2026-10-27

The optional native motion path now includes battle Pokemon, trainers, field
geometry, boundary props and GPU-wind foliage on Windows D3D12/Vulkan.
PHLOSION_NATIVE_MOTION=1 replaces the contact prepass with shared surface/motion
capture. Normal launches still leave this qualification path disabled.

## Ownership and actual poses

ModelRenderFrame ABI 20 appends optional scene-owned motion history and object
identity. The generic engine model provider enrolls the actual prepared rigid or
skinned batches, after pose, grounding and material resolution. Each primitive
uses an authored geometry-template ordinal assigned before visibility filtering
and sorting. Successful resource replacement supplies a new revision. History
owns previous matrices/palettes; shadow/probe/reflection sampling may reuse the
provider's pose slots without changing it.

Opaque/masked model motion uses the existing native GPU writers. Temporary batch
auto-instancing is disabled only for enrolled model submissions, preserving
authored primitive identity. Normal submission keeps its previous batching.
Explicitly instanced model batches, CPU-deformed model fallback and animated
billboards remain unenrolled; GPU-wind vegetation retains its existing per-
instance identity path. Translucent trails, particles, rain and held/flight balls
do not acquire fabricated motion. These still need their appropriate temporal
invalidation/coverage treatment in C3.

Old model-provider ABI 19 and historical ABI 6 calls remain supported without
reading the new frame suffix. PokemonModelSupport needs no source changes.
Consumers requesting ABI 20 must use a rebuilt model-runtime plugin; the candidate
package includes it. No previously deployed editor/runtime package was replaced.

## Views, cuts and transitions

Battle owns separate scene-editor and gameplay view histories. It commits only
after successful primary drawing; prewarm and auxiliary views never promote
history. Encounter reset/cancel clear it. Field history is cleared on encounter
entry and cannot bleed into battle or the returning field view.

Authored arrival camera cuts come from the existing cut list after its scheduler
time remapping. The camera director supplies a shot-selection key covering phase,
subject, motion, idle-shot replacement and move/group action identities. Normal
sampling/remainder and aspect changes do not manufacture new shots. Entry/recall/
free-camera mode changes and a conservative large-jump fallback also invalidate.
Resize, missing visibility and geometry/palette-layout changes retain the generic
history rejection rules.

Diagnostic handshakes expose each actor's image identities; optional GPU readback
can associate visible pixels with those actors. This diagnostic stalls and must
remain disabled for performance measurements.

## Qualification

Worker: gpu-worker Windows, optimized RelWithDebInfo, EVGA RTX 2060 SUPER
GPU_1_UUID, LUID 000000000000be8e.
Task: WORKER_EVIDENCE_ROOT/phlosion-battle-motion-20261027.
Controller evidence: PROJECTS_ROOT/Phlosion/PhlosionEngine/artifacts/agent-work/battle-motion-20261027.

The provider CPU test covers owned previous animation/model transforms, two
scene instances sharing an asset, reordered submissions, auxiliary pose-slot
overwrites, first visibility, visibility gaps, cuts, prewarm/trail exclusion,
cleared enrollment, inactive-history rejection and ABI 19/6 compatibility.
Camera tests check repeated sampling versus shot changes and every authored
arrival cut. Native GPU numerical tests cover camera/rigid/skinned/wind motion
on both APIs. Full scene qualification verifies actual actor identities and
default-off image controls. Ten runtime/editor checks passed across both APIs;
both field and battle default-off controls are pixel-identical to the previous
qualified images. All four Pokemon and both trainers have visible valid GPU
motion identities. The D3D12 battle frame records 732 parts, all with valid
previous state, and 534 changing deformation states. These counts establish
coverage, not image-quality or performance gains.

Four engine CPU suites and five initial game suites passed. The arrival suite
passed after the asset restore and test-origin correction described below.
Native numerical GPU cases passed on both APIs. The Windows editor and runtime
are qualified here; Linux and the Autochess application are not newly qualified
by this pass. See game-checks.json, native-final.json, cpu.json and
arrival-test-restored.log; retain the earlier failed arrival log.

The initial arrival test lacked the ignored SV ball GLB on the worker. It was
restored from the controller's prepared asset after matching the manifest SHA256
384f17404daaeb0c51b613cf8bc09df31235cef1c44075b48b4284ca790a9f8b.
Its cut test also uses the existing runtime scheduler origin, not the original
capture's first-draw index. Retain the original failure and successful rerun.

## Remaining connected technique

This completes another motion-producer milestone, not temporal reconstruction
or reflection parity. C3-C6 still need invalidation/dilation, scene-colour
reconstruction, post-AA composed-colour feedback, stochastic reflection tracing
and composition. The redundant reflection-source redraw remains. The shared
surface layout remains a qualification layout, not C1's compact shipping layout.
No frame-time improvement or newly measured retail-parity percentage is claimed.


Final transition qualification: the first battle frame (45) and the first
returning field frame (71) have zero valid previous pixels on both APIs. The
initial return fixture sampled frame 70, which is the cancellation frame still
rendered in battle; its failure is retained. These four accepted transition
checks are in transitions-final.json. All ten scene checks and native numerical
tests pass; graphics validation reports no errors. The known D3D12 warning 1328
about upload-buffer initial state remains. Tests do not establish a speedup.
