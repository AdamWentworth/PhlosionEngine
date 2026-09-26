# Cyberpunk engine readiness audit

Status: Engine-facing M1 complete; M2-A through F accepted; provisional game-owned Outskirt collision and 108-capture native scene replay qualified; M2 exit gate remains open
Date: 2026-09-26 (original audit 2026-09-20)
Owner: Astra (scope, architecture, acceptance)
Audited engine baseline: `c17d2122bd36b3c3bb49c4e703900f40cccfbd76` (`main`)

The original documentation-only audit records Phlosion Engine against the
recovered Pokemon Cyberpunk direction: missing capabilities, a dependency-ordered
roadmap and a migration boundary. The dated progress record below supersedes
the original baseline where implementation and verification have since landed.

## Implementation progress after the audit

On 2026-09-20, Astra accepted **M1-A: asynchronous asset bytes and a public CPU
archive fixture**, implemented by `astra_flash_builder` and committed with the
roadmap as `e2ec926` before beginning M1-B.
[AsyncAssetLoader](ASYNC_ASSET_LOADING.md) now provides one background
reader over IAssetStore, bounded request slots, cancellation and explicit stop,
safe result ownership, and a retained-payload byte budget. Synthetic scene and
prefab archives exercise the existing PHRC encoders/readers through the loader.
The engine/editor Debug build passed and all five CTest entries passed,
including fourteen C++ contracts. See the
[accepted checkpoint](agent-work/cyberpunk-m1-assets/CHECKPOINT.md).

On the same date, Astra accepted **M1-B: dependency-aware asset loading**,
implemented by DeepSeek V4.1 Flash through `astra_flash_builder`.
[PhlosionAssetGraphLoader](ASSET_DEPENDENCY_LOADING.md) resolves opaque asset IDs
through a caller mapping, loads required transitive dependencies, deduplicates
shared assets, validates PHRC/raw dependency hashes, rejects required cycles and
reports skipped optional edges. Asset, edge and retained-byte limits apply to
each session. Cancellation/reset suppress late results and allow reuse. A public
dependency-only prefab -> scene -> raw fixture verifies the consumer boundary.
The final engine/editor Debug build and all five CTest entries passed, including
fifteen C++ contracts. See the
[M1-B accepted checkpoint](agent-work/cyberpunk-m1-dependencies/CHECKPOINT.md).
M1-B was committed as `b2e03c4` before beginning M1-C.

Also accepted on 2026-09-20: **M1-C: staged archive activation**, implemented by
DeepSeek V4.1 Flash through `astra_flash_builder` and reviewed by Astra.
[PhlosionArchiveSession](ARCHIVE_ACTIVATION.md) consumes the dependency graph
and the existing typed scene/prefab decoders. A replacement becomes staged only
after both validations pass; explicit activation switches the resource snapshot.
Failed, cancelled or discarded candidates preserve the active archive, while
caller-held snapshots remain readable across replacement, unload and provider
destruction. Tests cover real scene/prefab content, a dependency-only prefab,
invalid typed roots, missing/corrupt dependencies and gated replacement reads.
The engine/editor Debug build and five CTest entries passed with sixteen C++
contracts. See the [M1-C accepted checkpoint](agent-work/cyberpunk-m1-activation/CHECKPOINT.md).
M1-C was committed as `83e02ff` before beginning M1-D. This is an engine resource
session; no production game application call site has been migrated.

Also accepted on 2026-09-20: **M1-D: public archive-to-render fixture**, built by
DeepSeek V4.1 Flash through `astra_flash_builder` and reviewed by Astra. The
[public fixture](PUBLIC_ASSET_FIXTURE.md) loads synthetic geometry/textures
through the archive session in a standalone host and a real editor scene plugin.
Initial A at logical frames 0/30, failed replacement preserving A30 and valid B30
replacement passed on native OpenGL, Vulkan and Direct3D 12: 24 captures with
expected checker, depth, blending, rigid-marker and cache-replacement checks.
The full Debug build and six CTest entries passed, including seventeen C++
contracts and an adversarial capture-harness suite. Exact matrix coverage,
requested/observed capture frames and binary provenance are checked. See the
[accepted checkpoint](agent-work/cyberpunk-m1-fixture/CHECKPOINT.md).
M1-D was committed as `68aed76` after the user's requested semantic folder review:
the sample stays in examples, verification utilities are in tools, and automated
checks are in tests. Build, six CTest entries and all 24 captures passed again
after relocation. GPU evidence is one Windows/GTX 1070 host;
no production renderer/editor API changed.

Also accepted on 2026-09-20: **M1-E: typed static mesh assets**, implemented by
DeepSeek V4.1 Flash through `astra_flash_builder`, reviewed and corrected under
Astra's acceptance gate. [Static `.phmesh` v1](MESH_ASSETS.md) now lives in
engine_core, with indexed geometry, submeshes, material slot labels, bounds,
portable component encoding and strict validation. The public fixture consumes
six required PHMS resources through its staged dependency graph; it retains
example-specific textures, materials and motion. Invalid mesh or slot-binding
candidates preserve the active scene. CPU checks include a two-submesh/two-slot
consumer, malformed UTF-8, fixed golden bytes and attribute preservation.

The final warning-clean Debug build, six CTest entries (eighteen C++ contracts)
and a Core-only link proof passed. All 24 native runtime/editor captures passed
with unchanged guards and matched the accepted M1-D images pixel for pixel.
See the [M1-E accepted checkpoint](agent-work/cyberpunk-m1-mesh/CHECKPOINT.md).
M1-E was committed as `9d094c9` before beginning M1-F.
It adds a typed storage/validation format, not the engine's
first mesh-rendering capability. Existing Model/glTF/cache, skeletal sampling,
skinning and material-profile paths remain available and were not migrated.

Also accepted on 2026-09-20: **M1-F: existing glTF character-import verification**,
implemented by DeepSeek V4.1 Flash through `astra_flash_builder` and reviewed by
Astra. The [CPU contracts](GLTF_IMPORT_VERIFICATION.md) exercise the existing
file loader, scene hierarchy, skeleton/inverse binds, LINEAR/STEP clip import
and material metadata resolver using tiny authored public fixtures. A separate
test verifies temporary-fixture ownership. The warning-clean Debug build, six
CTest entries and all twenty-one C++ contracts passed. Production sources are
unchanged; this adds verification, not a replacement mesh/animation/material
system. See the [M1-F accepted checkpoint](agent-work/cyberpunk-m1-gltf/CHECKPOINT.md).
M1-F was committed as `9c0d812` before beginning M1-G's pose-sampling and native
skeletal-deformation verification.

Import is distinct from playback: M1-F's original tests did not sample poses,
deform vertices or draw pixels. Its audit found discarded CUBICSPLINE tangents
and unsupported morph-weight channels. M1-H below closes the cubic gap and adds
import-to-sampling checks; morph weights remain unsupported.

Also accepted on 2026-09-20: **M1-G: shared pose sampling and native skeletal
verification**, implemented by DeepSeek V4.1 Flash through `astra_flash_builder`
and reviewed by Astra after one consolidated correction. The existing evaluator
is now available through a CPU interface, with Model delegating to the same
implementation. [Animation verification](ANIMATION_VERIFICATION.md) covers
LINEAR/STEP sampling, loop/root-motion policies, hierarchy/scale compensation,
and a public weighted two-joint rig. Four poses passed on all three native APIs
in the standalone runtime and actual editor plugin (24 captures), with expected
deformation and stable palette-storage transition checks. The warning-clean
build, seven CTest entries and 23 CPU contracts passed. The existing 24-case
archive matrix also passed. See the
[M1-G accepted checkpoint](agent-work/cyberpunk-m1-animation/CHECKPOINT.md).
M1-G was committed as `afaeb1e` before beginning M1-H.
This qualifies a bounded shared sampler/world-mesh
path on one GTX 1070 Debug host; private characters, legacy Model drawing,
instanced/indirect skinning and performance remain outside that evidence.

Also accepted on 2026-09-20: **M1-H: glTF cubic animation fidelity**, implemented
by DeepSeek V4.1 Flash through `astra_flash_builder` and reviewed by Astra.
The importer preserves cubic tangent triplets; the shared sampler evaluates
translation, rotation and scale curves with authored interval timing and
normalized quaternion results. Exact/clamped keys bypass interpolation, malformed
cubic channels preserve their current component, and existing LINEAR/STEP
policies remain unchanged. Cache v10 rejects older tangent-stripped caches;
CPU contracts exercise the production sampler codec and identity gate.

The warning-clean Debug build, seven CTest entries and 24 CPU contracts passed.
Six poses passed across native runtime/editor and all three APIs (36 captures),
including two cubic rotation poses with guards that reject linearized playback.
The original 24 animation images remain pixel-identical to M1-G. The archive
fixture also passed 24 final-build captures. See the
[M1-H accepted checkpoint](agent-work/cyberpunk-m1-cubic/CHECKPOINT.md) and
[animation verification](ANIMATION_VERIFICATION.md). M1-H was committed as
`4ab3ac7` before beginning M1-I. This is bounded public-rig evidence, not
private-character or full Model cache/draw qualification.

Also accepted on 2026-09-20: **M1-I: existing glTF texture decoding
verification**, implemented by DeepSeek V4.1 Flash through `astra_flash_builder`
and reviewed by Astra after one consolidated correction. The
[CPU contract](GLTF_TEXTURE_VERIFICATION.md) loads real public PNG/JPEG payloads
through the engine's file loader and existing five texture decode helpers.
It checks external/data URI and bufferView/GLB routes, literal pixels and alpha,
role selection, sampler metadata, material-alpha integration, corrupt payloads
bound to every role and retained results after later decodes. Tests and docs
changed; production behavior is unchanged. The warning-clean Debug build,
seven CTest entries and 25 CPU contracts passed. See the
[M1-I accepted checkpoint](agent-work/cyberpunk-m1-textures/CHECKPOINT.md).
M1-I was committed as `a037f88` before beginning M1-J.

Also accepted on 2026-09-20: **M1-J: five-map native material verification and
empty-image fallbacks**, implemented by DeepSeek V4.1 Flash through
`astra_flash_builder` and reviewed by Astra. The [material sample and
guards](MATERIAL_VERIFICATION.md) connect the existing five glTF decode helpers
to the existing world renderer. Three cases (authored maps, empty image bytes,
absent maps) passed on native OpenGL, Vulkan and Direct3D 12 in both the runtime
and real editor preview: 18 captures at matching viewport sizes, independent
diagnostic-patch checks, 18 cross-API pairs and nine pixel-identical host pairs.
The composed lit region is present and differs between authored and fallback
inputs. Empty encoded images now use the existing role-specific placeholders;
a permanent regression fails under the old black normal/MR/AO branch.

The warning-clean full Debug build, 26 CPU contracts and all eight CTest entries
passed. The previous 24 archive and 36 animation captures still match their
final binaries by SHA-256 and size. Runnable content lives in examples,
verification tools in tools and automated checks in tests. No renderer, shader,
Model, cache or plugin ABI changed. See the
[M1-J accepted checkpoint](agent-work/cyberpunk-m1-materials/CHECKPOINT.md).
M1-J is included in the commit `Verify native material maps and fix empty-image fallbacks`.

Remaining material work is explicit: base factors are still baked into encoded
bytes; source inspection still identifies factor-only emission loss on the
legacy OpenGL Model path. Model's runtime loader calls only base-color and
emissive helpers; the public sample is not a production character adapter.
Independent per-role UVs/transforms, compressed texture decoding, GPU scalar
factor application and general PBR fidelity remain unqualified. M1-J uses one
shared UV set, identity transforms, baseColorFactor 1 and default wrap on one
local Windows/GTX 1070 host.

At M1-J this was partial M1 completion. The next gaps were the actual production
character/content paths and further material/texture/environment qualification.
Blending, events and managed attachments remain M3 work. Asset contracts can document
and test an existing representation; they do not require a new file format.
The full typed family remains optional, as the audit below states. Production game adapters, private roster
qualification, richer GPU resource lifetimes and residency/load-time measurements
remain. The public fixture's schema is deliberately example-specific.
The budgets are not hard peak-memory limits and cancellation cannot interrupt
an active synchronous provider read. Decoding still runs on the caller thread
without a frame-time guarantee. The first three passes were CPU-only; M1-D/E add
the bounded visual evidence above. Static mesh v1 does not include skinning,
LODs, compression, material payloads or an importer/cook CLI. Active, staged and externally retained snapshots can
coexist; their combined residency is not bounded by the per-candidate byte cap.
The capability matrix below preserves the original
audited commit's state; this progress record supersedes its byte-loading and
dependency-resolution/public-fixture/typed-storage/import/pose/texture/material-verification gaps and twelve-contract count
for the local implementation.

Accepted on 2026-09-20: **M1-K: integrated CPU glTF scene and shared renderer
adapter**, based on `cc600e1` plus the accepted changes included in the commit
`Integrate owned glTF scenes with shared rendering`.
The reusable engine path loads owned geometry, decoded textures, nodes, skins
and clips; samples poses; and presents the selected scene through the existing
world renderer. A public character/environment example exercises these together.
Loading and draw binding live under `src/engine/render/gltf`, while authored
sample content and test transitions belong in `examples/scene_fixture`.

The selected path preserves raw base-color pixels and applies the linear factor
once on the GPU. It supports factor-only emission without changing the legacy
Model behavior described above. Candidate loading and pose rejection preserve
previous output; retained snapshots remain usable after replacement/source
removal. Unsupported material/sampler options and invalid references are
rejected before unsafe helper access or any partial draw submission.

Verification passed 29 CPU contracts, nine CTest entries, and 36 integrated
captures across native OpenGL/Vulkan/D3D12 in both runtime and real editor
preview. The archive, animation and material regression matrices passed another
78 captures. Tests cover bind-to-pose, failed replacement, replacement,
emission-off and unload, with independent expected-content and color-factor
checks. Evidence is scoped to Windows/GTX 1070 Debug and the documented subset.
See [the supported scene path](GLTF_SCENE_ASSETS.md),
[the fixture guide](SCENE_FIXTURE.md), and
[Astra's acceptance](agent-work/cyberpunk-m1-integration/ACCEPTANCE.md).

M1-K itself was synchronous and CPU snapshot release did not evict renderer GPU
caches. The closure pass below supplies asynchronous archive integration and
explicit GPU reclamation. Retained byte counts remain distinct from peak memory.

### M1-L/M closure and next milestone

Accepted on 2026-09-20 from baseline `56cdf24`: **M1-L/M, archive-backed glTF
sessions and native GPU ownership**. Flash returned HTTP 402 before editing;
the user authorized Astra fallback. Root Astra implemented the session/consumer
integration, an explicitly selected Astra worker implemented the independent
GPU foundation, and root reviewed and qualified the combined change.

[Scene sessions](GLTF_SCENE_SESSIONS.md) load Scene/Prefab archives, decode glTF
on a worker and publish a matching archive/scene pair. Failed/cancelled candidates
preserve active; late results cannot replace a newer request; retained snapshots
survive unload. The active plus accepted candidate logical geometry/pixel budget
is explicit. Existing graph/PHRC interpretation can still run in `pump()`.

[World resource scopes](WORLD_RESOURCE_SCOPES.md) now own cached geometry,
textures and material descriptors on OpenGL, Vulkan and D3D12. Release closes
admission and collects after GPU completion at the next frame boundary. Vulkan
uses dedicated managed geometry; D3D12 descriptor capacity is recycled; live
independent scopes are preserved. Shared fallbacks/environment and transient
resources remain outside the managed accounting. Maintenance may block.

The [native lifecycle matrix](SCENE_FIXTURE.md) runs 12 unique reload, pending
replacement, cancellation, retained-draw and release cycles before each of six
final content cases. All 36 runtime/editor captures passed on the three APIs,
including live-host unload to zero managed resources. Another 114 regression
captures passed. All 32 CPU contracts, nine CTest entries and the warning-clean
Debug build passed. The 36 lifecycle images exactly match the legacy scene
captures, which exactly match accepted M1-K images. See
[Astra's acceptance and measurements](agent-work/cyberpunk-m1-closure/ACCEPTANCE.md).

Engine-facing M1 exit checklist:

- [x] Document the supported path and asset subset, then demonstrate geometry,
  animation and materials together through interfaces available to a consumer.
- [x] Correct and qualify the selected path's required material behavior, including
  tint/color-factor and emission semantics; verify GPU scalar behavior where used.
- [x] Demonstrate load, replacement, failed replacement and unload with explicit
  resource ownership, and record loading/residency measurements for the scene.
  M1-L/M qualify asynchronous activation, cancellation/retention, scoped GPU
  release and repeated native resource stability with measured bytes/timings.
- [x] Pass expected-content and parity checks for the integrated scene in native
  OpenGL, Vulkan and Direct3D 12, including the real editor preview.

This does not require every optional format, an entire typed asset family or
general PBR conformance. Private roster/district qualification remains the
separate game-facing M1 track. **The agreed engine-facing checklist is complete
(4/4 gates); the whole game's M1 is not declared complete.** Full streaming,
performance budgets and district scale remain later gates. Evidence is limited
to the documented glTF subset on one Windows/GTX 1070 Debug host; the byte caps
do not bound total process, driver or peak decode memory.

After this engine-facing gate, prioritize M2's small walkable scene: collision,
movement, camera and input. That gives subsequent lighting a useful scene and
interactive context. A bounded lighting feature can then precede M5's full
environment qualification. M2 needs its own bounded implementation brief; this
closure pass does not start it.

### M2-A: collision foundation (accepted 2026-09-20)

The first M2 pass establishes a reusable CPU static collision world through
`Phlosion::Collision`: owned boxes/triangle meshes, layer and sensor filtering,
raycasts, upright-capsule sweeps and precise overlaps. Astra selected Jolt behind
a Phlosion-owned public API; the unchanged vcpkg baseline selects Jolt 5.4.0#0.
The target is separate from Core, rendering and the existing Engine umbrella.
See the [collision contract](COLLISION_WORLD.md) and
[acceptance record](agent-work/cyberpunk-m2-collision/ACCEPTANCE.md).

Astra implemented through the user-authorized fallback after Flash returned
HTTP 402 before editing. Root Astra reviewed the complete source and evidence,
then requested transformed-mesh and thread-ownership verification. The full
enabled Debug build with warnings as errors and all ten CTest entries passed;
the final affected rebuild and all eight collision groups passed after review.
The standalone executable links no engine graphics/platform/editor targets.

This is the foundation for subsequent controller, camera and trigger behavior.
Character movement, gameplay trigger events, input/controller handling and the
walkable runtime/editor scene are later M2 passes. M2 remains in progress;
this accepts only the bounded CPU collision foundation, with no native visual
or playable-scene qualification claim. The next pass's fixed-step movement,
grounding, sliding, slopes, steps and recovery work is recorded below.

### M2-B: character movement (accepted 2026-09-22)

The continuation from M2-A commit `b570f4e` adds
[world-owned upright characters](CHARACTER_CONTROLLER.md) using the pinned Jolt
virtual-character solver: fixed-tick analog walk/run, gravity/terminal fall speed,
grounding, wall/corner sliding, slope/step policy, floor following, bounded
penetration recovery and precise safe teleport. Public state reports actual
movement and copied support metadata; character IDs, ownership and rollback
remain behind the Phlosion API. Sensors never block or support characters.

The full enabled Debug build with `/W4 /WX` and all eleven CTest entries passed.
Ten standalone character groups exercise analytic geometry, fixed-tick batching,
removed support, thread ownership, capacity and failed-operation preservation.
Final test-only filter additions passed another build and the affected CTest.
Root reviewed the actual source and evidence, including velocity-reporting and
rollback-support defects found and fixed during implementation. Flash returned
HTTP 402 before editing; the user-authorized Astra fallback implemented the pass.
See the [acceptance record](agent-work/cyberpunk-m2-character/ACCEPTANCE.md) and
[checkpoint](agent-work/cyberpunk-m2-character/CHECKPOINT.md).

M2-B did not add trigger event state; the next pass below supplies discrete
sampling. Exploration camera/actions, runtime/editor host integration and the
matching-frame three-API walkable-scene gate remain subsequent M2 work. This
CPU pass does not qualify a playable scene or complete M2.

### M2-C: character trigger events (accepted 2026-09-22)

From M2-B commit `21672db`, the [trigger sampler](CHARACTER_TRIGGERS.md) adds
explicit character sensor Enter/Stay/Exit state, independent filters, copied
metadata and deterministic ordering. Precise capsule overlaps compare each
character's current membership with its previous successful sample. Removed
sensors retain copied exit metadata; filter changes and teleports reconcile on
the next sample. Sampling neither moves characters nor dispatches gameplay.

The full enabled `/W4 /WX` Debug build and all twelve CTest entries passed
(297.60 seconds). Seven standalone trigger groups verify precise geometry,
full event metadata/order, independent history/filter ownership, failed calls,
sensor removal/slot reuse, lifetime/thread rules and repeated sequences. Final
test-only cases passed the affected rebuild/CTest. Root reviewed the actual
patch and retained evidence with no outstanding findings. This pass reused the
disclosed Astra fallback after the recent Flash balance failure; no routing
settings changed. See the
[acceptance record](agent-work/cyberpunk-m2-triggers/ACCEPTANCE.md) and
[checkpoint](agent-work/cyberpunk-m2-triggers/CHECKPOINT.md).

Samples record discrete endpoint membership: a complete sensor crossing between
samples can produce no event, as explicitly tested. Continuous path events,
camera/actions and native runtime/editor host qualification remain outside this
pass. M2 is still incomplete.

### M2-D: named actions and controller input (accepted 2026-09-22)

From M2-C commit `6a72ded`, [named input actions](INPUT_ACTIONS.md) now provide
SDL-free button/scalar/vector actions, transactional rebinding, explicit frame
edges, rescaled deadzones, controller selection and focus/enable recovery.
The opt-in Platform adapter discovers mapped SDL controllers, preserves queued
taps, polls complete snapshots and handles hotplug without owning the host's
event queue. Existing InputEvent and runtime/editor event paths remain unchanged.

The full enabled Debug build and all fourteen CTest entries passed. A second
build qualified all 37 compiled targets with `/WX` and passed all fourteen
entries again (355.57 seconds). The two new suites use `/W4 /WX`; the pre-existing
aggregate test target retains `/W1` and ordinarily disables `/WX`, as documented
in [verification](VERIFICATION.md). Temporary qualification flags were restored.
Final test-only cleanup passed another full build and both input executables.

Six pure groups cover frame semantics, combinations, validation, identities,
gates and ownership. Three SDL groups use real virtual mapped devices, including
all 15 buttons and six axes, queued taps, event/poll duplicate observations,
selection, removal, window filtering and balanced subsystem/handle teardown.
Import inspection confirms the pure executable needs only Windows/C++ runtimes;
the adapter test adds SDL without graphics backend imports. Root reviewed the
implementation and evidence with no outstanding findings. This reused the
approved Astra fallback after the Flash balance failure; routing was unchanged.
See the [acceptance](agent-work/cyberpunk-m2-input/ACCEPTANCE.md) and
[checkpoint](agent-work/cyberpunk-m2-input/CHECKPOINT.md).

Exploration camera, physical controller qualification and native host integration
remain subsequent work. Virtual devices do not qualify the full M2 scene/device
gate, input contexts, binding persistence or mouse-look controls.

### M2-E: exploration camera rig (accepted 2026-09-22)

From M2-D commit `bd76391`, the opt-in [exploration camera](EXPLORATION_CAMERA.md)
now provides separate authored settings and runtime follow/orbit state, explicit
delta/rate controls, camera-relative movement and a sphere-probed collision boom.
It pulls in immediately at obstructions, recovers distance with analytic damping
and resets explicitly for teleports/cuts. Failures preserve the previous state;
origin penetration is reported with a finite view direction at zero boom length.
The module links Collision and GLM without requiring SDL or a renderer.

The full enabled Debug build and all fifteen CTest entries passed (333.88 seconds).
Ten camera groups include analytic 16:9/21:9 framing and damping, real thin-wall,
rotated-box and transformed-mesh probes, filters/copied metadata, validation,
ownership and a repeated InputActions -> kinematic character -> camera sequence.
The composition exercises walk/run, blocking, orbit-relative movement, focus
release/rearm and recovery/reset. Both new targets compile with `/W4 /WX`, and
the executable imports only Windows/C++ runtimes. Root reviewed the source,
tests and retained evidence with no outstanding findings. Implementation reused
the approved Astra fallback with unchanged routing. See the
[acceptance](agent-work/cyberpunk-m2-camera/ACCEPTANCE.md) and
[checkpoint](agent-work/cyberpunk-m2-camera/CHECKPOINT.md).

Existing Camera3D, host event paths and rendering remain unchanged. This is CPU
API composition, not a native playable scene. The probe covers the final radial
sphere, not the camera's swept trajectory or whole-frustum clearance. Runtime/
editor scene integration, native aspect-ratio captures, authored camera zones
and physical-device qualification remain; M2 is still incomplete.

### M2-F: asset-independent native exploration (automated qualification accepted 2026-09-22)

From M2-E `36b6986`, the [public exploration course](EXPLORATION_FIXTURE.md)
combines the accepted systems in the standalone runtime and actual editor Game
preview. It adds an optional SDL input extension preserving ABI 34, focus/text/
pause gates, fixed-tick edge/delta delivery, exact viewport aspects, authored
camera settings, safe recovery and NPC blocked-route/retry behavior. Public
placeholder geometry drives both rendering and collision.

All 20 CTest entries passed. Final native qualification passed 96 exploration
captures at 16:9/21:9, twelve supplemental steep/ledge captures and the existing
36-case scene regression, across both hosts and all three APIs. Exploration
viewport comparisons are pixel-identical; independent state/content/provenance
guards also pass. All 27 negative evidence cases reject, followed by pristine
recovery. Root reviewed the patch and one correction cycle, native images and
936 file identities. See [acceptance](agent-work/cyberpunk-m2-exploration/ACCEPTANCE.md)
and the [checkpoint](agent-work/cyberpunk-m2-exploration/CHECKPOINT.md).

The user exercised an SDL `Xbox 360 Controller` in visible D3D12 runtime and
editor sessions. Telemetry confirms basic controls and runtime recovery/focus;
manual editor toolbar transitions remain pending at the user's request. The
reported physical unplug/reconnect was not corroborated by an SDL inventory
change. Actual character fitting, environment collision authoring,
Outskirt Stand camera/routes/triggers and final scene qualification await the
game assets. Public placeholder evidence cannot close those asset-specific gates.

On 2026-09-22 the private GameCube research depot supplied provisional recovered
Outskirt Stand and Wes GLBs. The existing exploration runtime/editor now has an
opt-in `outskirt` case that draws both through the shared glTF scene path and
uses a separate provisional collision proxy. CPU import
accepted the outdoor and battle-field environment derivatives plus Wes. The
first outdoor idle/walk/run preview passed 18 native captures.

A subsequent asset-specific pass derives Jolt triangle collision from four
named recovered outdoor visual meshes: the ground, paved forecourt, raised
train ramp and train body. It filters ten degenerate train-body triangles.
The approach trigger and train-side blockers remain authored because the
visual mesh leaves a passable gap. The Blender source audit and five-phase
native matrix cover idle, walk, run, ramp ascent and a blocked train-side route:
60 captures across both hosts, all three APIs and both target aspect ratios at
matching simulation ticks.
Expected Wes/stand/forecourt pixels, grounded movement, ramp support and the
blocked wall route passed; all 30 runtime/editor viewport pairs are pixel
identical. The 40 cross-API pairs have a maximum mean channel error of
0.82/255, and all 30 corresponding 16:9/21:9 gameplay-state pairs match.
The two affected exploration CTest entries pass. See the
[fixture guide](EXPLORATION_FIXTURE.md) and private recovery record. This is
collision built from recovered render geometry plus authored blockers, not
extracted game collision. The battle-field variant still has only CPU import
and Blender preview evidence. Material fidelity, full routes and physical
controls on this provisional scene, final replacement content and manual editor
toolbar checks still leave the M2 gate open. M3 battle integration has not yet
been claimed.

On 2026-09-26, PokemonCyberpunk moved the Outskirt encounter's four collision
mesh choices, three authored boxes (including the approach sensor), and their
validation into its v2 scene data. The shared engine fixture loads these values
for that game scene while retaining its historical `outskirt` research case.
The game's nine-phase M2 replay passed 108 matching-frame captures across
runtime/editor, 16:9/21:9, and OpenGL/Vulkan/D3D12. Checks cover visible Wes,
train and forecourt content, walk/run, recovered ramp support, train-side wall
blocking, reset, synthetic focus loss/resume and virtual-controller reconnect.
All 54 runtime/editor image pairs are identical; 72 cross-API comparisons have
maximum MAE 0.785/255 and 1.89% pixels changing by more than eight. The public
course still owns the generic stair/step contract: no visible Outskirt step
case has been qualified. Physical focus/device behavior in this scene and the
deferred editor toolbar checks remain open, as do game-owned camera settings
and replacement-content fitting. This is M2 progress, not the M2 exit gate.

### M3-A: asset-independent animation blend foundation (qualified 2026-09-22)

The [blend foundation](ANIMATION_BLEND.md) adds clip crossfades, masked override
layers, ordered timed-event collection and public glTF pose/palette integration.
The synthetic two-joint rig's new crossfade case passed 42 native captures in
the runtime and real editor across OpenGL, Vulkan and Direct3D 12, with zero
expected-content, pairwise parity or cross-case failures. CPU pose, palette and
event contracts, all 20 configured CTest entries and the negative-evidence
harness passed. The native matrix is
retained under `artifacts/m3-a/native-20260922/`. Parameter-driven state,
event authoring, battle camera/UI/audio/VFX and roster-specific qualification
are still M3 work.

### M3-B: parameter-driven animation state (qualified 2026-09-22)

The [state-machine pass](ANIMATION_STATE_MACHINE.md) adds typed parameters,
authored transition priority, one-shot event timing and cancellation/reset
behavior on top of the M3-A blend path. Its public rig has a state-driven
crossfade case. All 20 CTest entries and the eight-case native matrix passed:
48 runtime/editor captures, 48 pairwise renderer comparisons and 60 cross-case
checks across OpenGL, Vulkan and Direct3D 12, with zero failures. Evidence is
under `artifacts/m3-b/native-20260922/`. Roster, authoring and battle-system
exit gates remain open.

### M3-C: asset-independent battle camera (CPU qualified 2026-09-22)

The [battle camera foundation](BATTLE_CAMERA.md) adds multi-target perspective
framing, authored cuts, eased blends, deterministic shake and reduced-motion
cleanup in the renderer-neutral camera target. This pass changes no native
presentation path. The full 21-entry Debug CTest suite passed, including the
new camera contract. Synthetic runtime/editor and final-asset camera captures
remain necessary for the M3 exit gate.

### M3-D: runtime UI focus and action routing (CPU qualified 2026-09-22)

The [UI focus foundation](UI_FOCUS_NAVIGATION.md) adds directional focus,
keyboard/controller action routing, repeat timing, modal gameplay capture and
neutral rearm after focus loss. All 22 Debug CTest entries passed; the affected
UI contract passed again after the final rearm change. Text layout, cross-API
drawing, battle HUD/dialogue integration and native image qualification remain
open.

### M3-E: runtime UI text foundation (CPU qualified 2026-09-22)

The [text layout foundation](UI_TEXT_LAYOUT.md) adds validated UTF-8 string
tables, measured wrapping/scaling and fallback-font selection in the CPU UI
target. It does not change the current OpenGL-only text renderer. Actual fonts,
native three-API text/HUD presentation and dialogue/subtitle integration remain
open. The full 23-entry Debug CTest suite passed.

### M3-F: runtime audio mixer (CPU qualified 2026-09-22)

The [audio mixer foundation](AUDIO_MIXER.md) adds owned decoded PCM clips,
four buses, looping, sample-time fades, mute, and spatial mono mixing. The
standalone CPU contract passes. Device output, decoding/streaming, game cue
bindings, and physical playback qualification remain open.

### M3-G: SDL audio device adapter (dummy-device qualified 2026-09-22)

The [opt-in SDL output](AUDIO_MIXER.md) queues bounded stereo PCM, advances the
mixer through a null sink if no device opens, supports retry, and clears queued
audio on stop/shutdown. Its dummy-driver contract passes. Physical speakers,
game cues and end-to-end battle audio remain to be qualified.

### M3-H: reusable effect host (CPU qualified 2026-09-22)

PhlosionVFX's [effect host](../../PhlosionVFX/docs/EFFECT_HOST.md) adds bounded
spawn IDs, moving-anchor attachment, expiry and stop-all cleanup. Its standalone
contracts pass. The game must still bind battle/animation events and actor
sockets to real effects; native attack-effect presentation remains open.

### M3-I: provisional Wes locomotion integration (qualified 2026-09-22)

The opt-in Outskirt Stand preview now drives Wes's accepted idle/walk/run clips
through M3-B's numeric-speed state graph and 0.15-second crossfades. The shared
blended glTF pose and skin palette feed all three renderers. Animation state in
the capture handshake records the chosen clip, target and blend weight. The
private six-phase native matrix covers idle, a 55.6% idle-to-run blend, walk,
run, recovered ramp ascent and a blocked train-side route at 16:9 and 21:9:
72 captures across runtime/editor and OpenGL/Vulkan/D3D12. All 36 matching-tick
host viewport pairs are pixel-identical, 48 cross-API image pairs have maximum
mean channel error 0.82/255, and all 36 corresponding aspect-ratio gameplay and
animation-state pairs match. The exploration, engine animation and animation
fixture contracts pass. A separate OpenGL runtime/editor reset probe restores
Wes to idle and the player to spawn. See the [fixture guide](EXPLORATION_FIXTURE.md)
and private recovery record.

This qualifies provisional field locomotion, not the M3 double battle.

### M3-J: provisional battle-field camera preview (native parity qualified 2026-09-22)

The opt-in `outskirt-battle` fixture draws the recovered Outskirt battle-field
GLB with four labelled actor slots occupied by temporary Wes copies. M3-C's
camera rig supplies a four-actor wide shot, a target blend, a counter cut,
impact shake and settle at fixed ticks. The same preview runs in the standalone
runtime and editor Game view at 16:9 and 21:9. This establishes a tangible
battle-field framing path; it does not implement battle rules or qualify the
final roster. See the [fixture guide](EXPLORATION_FIXTURE.md) and private
Outskirt recovery record for the repeatable native runner.

The D3D12 world sampler now uses default mip bias rather than -0.35. On the
previously failing 21:9 blend frame, this reduced OpenGL/D3D12 MAE from
1.0231/255 to 0.8885/255 without changing the 1.0 limit. The expanded M3-K
matrix below qualifies the temporary camera path in runtime and editor on all
three APIs. Final roster and authored-scene framing remain open.
As a regression check for the shared sampler change, the consumer's Release
matrix passes four existing scenes (combat, static PBR, transparent fire and
the wide starter battle) on all three APIs with content guards. Its complete
31-scene matrix was not rerun for this pass.

### M3-K: provisional encounter cue sequence (native qualified 2026-09-22)

An opt-in `BattleSequence` advances the four temporary actor slots through
fixed-tick camera blend/cut, a Wes gesture crossfade, a 0.5-second animation
impact marker, scripted health change and cleanup. The marker starts a
generated spatial SFX voice in `AudioMixer` and a short visual flash. Simple
health bars and the flash use backend-independent debug quads. Escape/B cancels,
Space/A skips, and R resets; cancel, skip, natural exit and preview teardown
clear the camera rig, active sound and effect state. The CPU sequence,
exploration, mixer and camera contracts pass.

The private eight-phase matrix covers 96 captures across runtime/editor,
16:9/21:9 and OpenGL/Vulkan/D3D12. All pass expected field, actor, HUD,
impact and cleanup checks. All 48 runtime/editor viewport pairs are
pixel-identical; 64 cross-API image comparisons pass with maximum MAE
0.892/255 and maximum over-eight changed-channel ratio 1.43%. All 48
corresponding aspect-ratio state pairs agree. This is a provisional encounter
demonstration, not the M3 double battle: it has no Pokémon rules, final actors,
production text HUD, physical audio output or PhlosionVFX effect-host binding,
and it does not return to the overworld.

### M3-L: provisional encounter audio output (2026-09-22)

The opt-in battle preview now uses `SdlAudioOutput` to pump its generated,
spatialized impact cue in the interactive standalone runtime and editor Game
preview. Hidden captures keep the deterministic null sink by default. The SDL
adapter reports the mixed peak and avoids filling its device queue with silence;
cancel, skip, reset and natural exit clear queued sound as well as mixer voices.
An unavailable or lost device falls back to the null sink with a diagnostic.
This connects the existing M3-F/G audio capability to the provisional M3-K
encounter; it does not supply battle music, ambient content or final SFX.
The dummy-device and missing-device branches are covered automatically; physical
audibility remains a manual check. The six affected CPU contract suites pass.
The private six-case audio probe passes impact in both hosts, cancel, skip and
automatic exit with the dummy device, and missing-device fallback. The unchanged
eight-phase native matrix passes all 96 runtime/editor captures on OpenGL,
Vulkan and D3D12 at 16:9 and 21:9 (112 parity comparisons).

### M3-M: recovered battle HUD and provisional VFX binding (2026-09-22)

The opt-in battle preview can draw cropped status-panel and command art from
Colosseum's `fight_common.fsys/menu_048.gtx` sheet, decoded to a private PNG by
the GameCube research recipes. Scripted health fills remain code-driven. The
fixture loads the game-owned `PokemonCyberpunk` module through a
versioned battle-preview hook. That module binds the impact marker to
PhlosionVFX's `EffectHost` and shared all-backend submission path, rendering a
temporary world-space glow and clearing it on stop, cancel, skip and exit.
The unconfigured public fixture retains its debug bars and flash. The game
module contains no extracted content. This pass does not add battle names,
numerical HP, move selection, authored move effects or final characters.
The private native matrix passes 96 runtime/editor captures and 112 parity
comparisons across OpenGL, Vulkan and D3D12 at 16:9 and 21:9. All 48 matching
runtime/editor viewports are pixel-identical; maximum cross-API MAE is
0.9675/255 against the unchanged 1.0 limit. The verifier checks recovered HUD
content, projected impact glow and cancel/skip/exit cleanup. The old no-asset
OpenGL impact capture remains pixel-identical. The research guide records
source provenance and the machine-readable report.

## Read this first

**Design provenance.** The product direction below comes from a user-supplied
shared ChatGPT conversation, "Engine Gap Analysis":
<https://chatgpt.com/share/6aaee44c-a1a0-83e8-8b4c-e827f3140144>. Astra
recovered it by parsing the shared HTML on 2026-09-20; the local extraction read
for this audit is recorded in
[EVIDENCE.md](agent-work/cyberpunk-readiness/EVIDENCE.md). Some of that
conversation is assistant restatement of the user's intent, and the user has
said some gaps it lists were already worked on. Treat it as recovered context,
not an approved specification document. Astra owns PLAN.md; this audit is an
evidence supplement, not implementation authorization.

**Code truth stays with code.** Implementation findings below are derived from the
repository at the audited baseline. Where a recovered-conversation claim does
not match the present code, the reconciliation section states what changed and
what is still outstanding. A header, interface, stub, TODO, planned
architecture section or source-contract test is never treated as runtime
qualification.

Evidence classes used below:

- **Implemented**: source exists in the engine and is reachable from the build.
  Implementation and verification are separate: an engine test is named only
  where one was identified, and its absence is recorded rather than implied.
- **Partial**: source exists and is exercised by the current consumer, but the
  behaviour is unproven for the work described here.
- **Planned-only**: described in documentation or a versioned contract but not
  implemented.
- **Not found in inspected scope**: no matching source was found in the search
  scope recorded in [EVIDENCE.md](agent-work/cyberpunk-readiness/EVIDENCE.md).
  This is a scoped negative, not proof of global absence.
- **Unverified**: cannot be judged from source; needs a runtime, content or GPU
  check that was not performed for this audit.

**Test coverage is narrow.** `tests/TestMain.cpp` runs twelve named contracts:
world material profile, resource container, authored scene, environment patch,
scene archive, project descriptor, editor renderer preference, camera view-plane
pan, editor project plugin, editor game preview routing, editor gameplay
reload, and editor performance. `CMakeLists.txt` adds four source-scanning
checks: semantic boundary, packed normal, material debug view, and review
lighting profile. Nothing in that list independently tests the ECS world and its
deferred structural changes, input translation, window/platform behaviour,
animation sampling or blending, audio, physics/collision, resource streaming,
save data, or gameplay at scene scale. Rows below describe implementation; only
rows that name a test are covered by one.

## Recovered product context

The recovered discussion describes a Pokemon Colosseum-style remake set in a
Cyberpunk setting, built above a version-pinned, reusable engine plus packages.
Its qualification target is an **Outskirt Stand vertical slice**:

1. Boot the game and load the Outskirt Stand scene.
2. Spawn the player and walk/run with a controller through a world with
   collision and a Colosseum-style exploration camera.
3. Interact with an NPC (Rider Willie) and read dialogue in the UI.
4. Enter a double battle with Espeon and Umbreon against enemy Pokemon, with
   battle camera, animation blending, VFX, audio and HUD.
5. Return to the overworld, save, quit and reload into the correct state.
6. Repeat the same scene under day, night and rain, at 16:9 and 21:9, with
   keyboard/mouse and with a controller.

Game-owned systems listed in the recovered material: battle sequencing, damage,
types, abilities, status, accuracy, critical hits, Block/Dodge, battle AI;
species, stats, moves, leveling, evolution, party and storage; Shadow state,
heart gauge equivalent, shadow move slot, purification, re-shadowing and
legendary rules; encounters, interactables, NPC behaviour, trainers, doors and
transitions; dialogue, quests, cutscene definitions, flags and story
progression; player inventory, money, party and progression; and save data.

Environment scope in the recovered material: day/night and rain, wet and
cyberpunk materials, with Agate vegetation and water qualifying separately.

Explicitly outside the baseline (do not design around them): a seamless
megacity, vehicles, multiplayer or other networking, destructive physics,
procedural worlds, a generalized quest engine and visual scripting. The
consumer's own graphics roadmap additionally keeps ray tracing, path tracing,
DLSS and frame generation out of the near term
([Autochess graphics roadmap](../../../Games/PokemonAutochess/docs/DISPLAY_GRAPHICS_ROADMAP.md)).

Two recovered emphases shape the whole audit:

- **Owner-requested workflow**: a local AI agent should be able to inspect,
  command, validate and capture the project through structured interfaces
  instead of manually navigating an editor UI. See the editor section.
- **Character content roster.** In the recovered discussion the user explicitly
  added Quilava and Typhlosion to the proof set; the rest (Umbreon baseline,
  Charmander or Charizard tail flame, Ponyta or Rapidash skinned flame surfaces,
  optional Gastly translucency) is the assistant's earlier proposal rather than
  a user decision. Material, animation and attachment/socket fidelity matter,
  and automatic conversion of fire effects from glTF must not be assumed. The
  current Autochess evidence already covers some adjacent cases, including
  Charmander-family fire material and shared Growl coverage per its test plan.

## Evidence provenance and limitations

Inspected (read-only):

- Engine source, tests, shaders, `CMakeLists.txt` and `vcpkg.json` at the
  baseline commit.
- Engine documentation: [VERIFICATION.md](VERIFICATION.md),
  [EXTRACTION_STATUS.md](EXTRACTION_STATUS.md),
  [ENGINE_BOUNDARIES.md](ENGINE_BOUNDARIES.md),
  [EDITOR_ARCHITECTURE.md](EDITOR_ARCHITECTURE.md),
  [MATERIAL_PROFILES.md](MATERIAL_PROFILES.md),
  [PHLOSION_ASSET_ARCHITECTURE.md](PHLOSION_ASSET_ARCHITECTURE.md).
- Consumer evidence: Pokemon Autochess docs
  ([graphics roadmap](../../../Games/PokemonAutochess/docs/DISPLAY_GRAPHICS_ROADMAP.md),
  [outstanding issues](../../../Games/PokemonAutochess/docs/OUTSTANDING_ISSUES.md),
  [parity contract](../../../Games/PokemonAutochess/docs/RENDERER_PARITY_CONTRACT.md),
  [tech debt](../../../Games/PokemonAutochess/docs/TECH_DEBT.md),
  [test plan](../../../Games/PokemonAutochess/docs/TEST_PLAN.md)) plus targeted
  source searches for audio, gamepad, save and scripting usage.
- Sibling ownership: [package architecture](../../PhlosionPackages/docs/PACKAGE_ARCHITECTURE.md),
  [PhlosionVFX README](../../PhlosionVFX/README.md).
- Unreal project structure only: `PokemonCyberpunk.uproject` plus one native
  module
  ([PokemonCyberpunk.Build.cs](../../../Games/PokemonCyberpunkUnreal/Source/PokemonCyberpunk/PokemonCyberpunk.Build.cs)).
- The recovered shared conversation, read as context only.

Not inspected: the Unreal project's binary `Content/` payload, private asset
depots and extraction results, generated/build/cache trees, and any other
machine state. The Unreal content may still hold art or design evidence this
audit does not see, so "the Unreal project is empty" is explicitly not claimed.

No build, test run, editor launch or GPU capture was performed for this audit.
The dated records in [VERIFICATION.md](VERIFICATION.md) and the Autochess
material verification describe earlier Autochess work, not this design.

## Reconciliation: previous gap, present evidence, remaining qualification

The recovered conversation contained an earlier engine roadmap. This is the
current status of its claims, re-checked against the baseline.

| Previous claim | Present evidence at baseline | Remaining qualification |
| --- | --- | --- |
| The LGPE material evaluator was compiled into the renderer and should become a project-loadable material profile | Completed separation: [ENGINE_BOUNDARIES.md](ENGINE_BOUNDARIES.md) and [EXTRACTION_STATUS.md](EXTRACTION_STATUS.md) record that recovered field, character, eye and layered-fire materials moved to the game; [MATERIAL_PROFILES.md](MATERIAL_PROFILES.md) plus [WorldMaterialProfile.h](../src/engine/render/WorldMaterialProfile.h) define the project profile API, and the semantic boundary test rejects the removed identifiers | A future Cyberpunk project supplies its own profiles and materials; engine defaults are unqualified for wet, cyberpunk or stylised city looks (Unverified) |
| `BoardRenderer`, `BattleFeed` and `HealthBarRenderer` lived in the engine | Completed: they now live in the game; the engine keeps generic mesh, text, sprite and UI primitives, and the shared `healthbar` shader filenames map to a generic solid-color rectangle used by boot loading ([ENGINE_BOUNDARIES.md](ENGINE_BOUNDARIES.md)) | None engine-side; game presentation semantics remain game-owned |
| The engine carried Autochess-specific profiling and diagnostics such as Growl/Scratch fields and shop/round/combat timings | Completed: [EngineServices.h](../src/engine/core/EngineServices.h) now holds generic host, presentation and frame-performance fields only; the game owns its runtime services | None; keep game diagnostics out when Cyberpunk code is added |
| An installed CMake package and standalone engine CI were future cleanup and freeze blockers | Separate CI exists ([VERIFICATION.md](VERIFICATION.md)). Version pinning is already practiced at the consumer boundary: Pokemon Autochess `CMakeLists.txt` lines 193-266 set `PHLOSION_ENGINE_GIT_REPOSITORY` and an exact `PHLOSION_ENGINE_GIT_TAG` and consume it through `add_subdirectory` or `FetchContent`, with matching exact pins for PhlosionPackages (line 281) and PhlosionVFX (line 341). An installed CMake package and API freeze remain future work in [EXTRACTION_STATUS.md](EXTRACTION_STATUS.md). Audit position: an installed SDK is not a required blocker | Status is Partial: source pinning works today; release/API compatibility checking and clean-machine package qualification are outstanding. Choose and record the mechanism at the release boundary |
| The full typed cooked family `.phmesh/.phskel/.phanim/.phmat/.phcol/.phv` had to be finished before freezing | Present: [PhlosionResourceContainer.h](../src/engine/assets/phlosion/PhlosionResourceContainer.h) and [PhlosionSceneArchive.h](../src/engine/assets/phlosion/PhlosionSceneArchive.h) provide the `.phrc` container plus `.phlo`/`.phscene` runtime APIs with integrity and round-trip tests; typed formats remain a long-term target in [PHLOSION_ASSET_ARCHITECTURE.md](PHLOSION_ASSET_ARCHITECTURE.md). Audit position: the typed family is not a blocker | Treat typed formats as an optional, evidence-driven follow-up; freeze against the versioned PHLO/PHSCENE contract |
| Lua/sol2 were linked without a mature generic scripting layer | Still true: `lua` and `sol2` are declared in [vcpkg.json](../vcpkg.json) and linked in [CMakeLists.txt](../CMakeLists.txt), while engine source contains no Lua usage (word-bounded search) | Optional for the qualification slice; if scripting is wanted, decide between a thin engine facade and game-owned bindings |
| Scene hierarchy was partly read-only with editable components as a near-term milestone | Completed for the current adapter workflow: [EDITOR_ARCHITECTURE.md](EDITOR_ARCHITECTURE.md) M1 is active with typed commands, transactions, undo/redo, transform gizmos and saveable project-owned overrides; [EDITOR_PROJECT_EXTENSIONS.md](EDITOR_PROJECT_EXTENSIONS.md) declares generic transform capabilities, project commands and commit/cancel/undo routing; source spot checks show the layout-object and command types in [EditorProjectPlugin.h](../src/engine/editor/EditorProjectPlugin.h) (lines 180, 244-290, 523-526, 678-689) and undo/redo plus "release to save; Esc cancels" in [EditorShell.cpp](../src/engine/editor/EditorShell.cpp) (lines 1483-1494, 1686) | Generic component coverage beyond project layout objects, and a general external query/edit transport for agents, remain outstanding. Canonical cooked scenes stay read-only by design |
| Physics/collision, character controller, animation runtime, camera rigs, action input, runtime UI and audio were missing | Absent or partial at the original audit; M2-A through F subsequently added and qualified the bounded exploration stack | Actual game-scene qualification, expanded animation/battle cameras, runtime UI and audio remain blockers |

## Capability matrix

Each row states what the engine has now, whether any test was identified, and
the gap for the Outskirt Stand slice. Rows that name a test are covered by it;
rows marked "no engine test identified" are source-inspected only, and "where
it is proven" means consumer usage, not engine verification.

### Platform, input and camera

| Capability | Status | Evidence | Gap for this work |
| --- | --- | --- | --- |
| Window, drawable size, VSync, swap | Implemented | [Window.h](../src/engine/platform/Window.h); no engine test identified | Fullscreen/borderless policy and display-mode enumeration are consumer-side |
| Keyboard and mouse input | Actions and bounded native exploration routing qualified through M2-F | [Input contracts](INPUT_ACTIONS.md) plus the [public course](EXPLORATION_FIXTURE.md): keys, RMB orbit, wheel zoom, focus/text/pause gates and fixed-tick delivery | General text/IME input and final game integration remain; existing plugins opt in explicitly |
| Gamepad / controller | Mapped adapter and synthetic runtime/editor integration qualified through M2-F; basic physical D3D12 controls exercised | [SDL action adapter](INPUT_ACTIONS.md), public-course virtual-device select/hotplug/rearm, and recorded Xbox 360 Controller manual sessions | Manual editor toolbar checks deferred; physical SDL detach not corroborated; prompts, haptics and other hardware/driver coverage remain |
| Named action mapping, rebinding, context layers | Actions, in-memory rebinding and sample host integration implemented; context layers remain absent | [InputActions](INPUT_ACTIONS.md) and [course scheduling](EXPLORATION_FIXTURE.md) preserve taps/deltas through zero-tick frames and consume them once across catch-up ticks | Context priority/consumption, binding capture UI and persistence remain |
| Camera | Exploration CPU/native path qualified through M2-F; M3-C battle rig CPU foundation and M3-J/K provisional native preview | [Exploration camera](EXPLORATION_CAMERA.md), [battle camera](BATTLE_CAMERA.md) and [Outskirt fixture](EXPLORATION_FIXTURE.md): four-slot framing, blend, cut, shake, settle and cleanup on all three APIs in runtime/editor | Final-asset framing and authored scene zones remain unqualified |
| Cursor capture and pointer policy | Bounded viewport-local exploration policy qualified in M2-F | RMB starts inside the active image; gates/releases clear drag and deltas; wheel respects image hover without global relative mouse mode | Broader cursor modes and battle pointer policy remain |

### Runtime, ECS, jobs and time

| Capability | Status | Evidence | Gap for this work |
| --- | --- | --- | --- |
| Application host and frame loop | Implemented | [Application.cpp](../src/engine/runtime/Application.cpp) clamps `frameDt` to 0.25 s (line 339), which bounds each frame to at most about 15 fixed updates; Escape is hardcoded to quit (line 303); and a window resize rebuilds the camera with fixed 45-degree, 0.1-100 parameters (line 317), discarding the current pose. No engine test identified. | Needs verification for pause/menu flows (the hardcoded Escape quit) and camera continuity across resize; there is no frame-pacing policy beyond fixed step |
| Fixed-step simulation | Implemented | [FixedStep.h](../src/engine/runtime/FixedStep.h) (60 Hz constant); accumulator loop in [Application.cpp](../src/engine/runtime/Application.cpp) lines 336-343 | Catch-up is bounded by the 0.25 s clamp, not unbounded; the policy for long frames is implicit and has no contract test |
| ECS world and component storage | Implemented | [World.h](../src/engine/core/ecs/World.h), [ComponentStorage.h](../src/engine/core/ecs/ComponentStorage.h), [View.h](../src/engine/core/ecs/View.h); no engine test identified | Deferred structural changes exist in source. Archetype storage, query caching and entity serialisation are scale-driven choices, not blockers for the slice |
| System scheduling | Implemented | [Scheduler.h](../src/engine/core/ecs/Scheduler.h), [SystemRegistry.h](../src/engine/core/SystemRegistry.h); no engine test identified | Ordered single-thread execution may satisfy the slice; verify against its budgets. Parallel scheduling is a scale-driven choice, not an automatic blocker |
| Job system / worker pool | Not found in inspected scope | `std::thread`, `std::async`, `taskflow`, `worker` search: only `std::async` texture decode in [D3D12RenderBackendTextures.cpp](../src/engine/render/d3d12/D3D12RenderBackendTextures.cpp) line 401 | Async asset work needs an explicit owner (see M1/M4); a general job system is a scale-driven choice |
| Events | Implemented | [EventBus.h](../src/engine/core/EventBus.h), [events/EventBus.h](../src/engine/events/EventBus.h); no engine test identified | Mutex-guarded, documented for single-threaded gameplay use |

### Resources, cooking, import and streaming

| Capability | Status | Evidence | Gap for this work |
| --- | --- | --- | --- |
| Cooked container and runtime archives | Implemented | [PhlosionResourceContainer.h](../src/engine/assets/phlosion/PhlosionResourceContainer.h), [PhlosionSceneArchive.h](../src/engine/assets/phlosion/PhlosionSceneArchive.h), tests for both | Archives load whole into memory; no partial or ranged reads |
| Asset store abstraction | Implemented | [IAssetStore.h](../src/engine/core/IAssetStore.h); no engine test identified | Three read methods only: no async handle, dependency resolution, cancellation or residency policy |
| Model loading and on-disk cache | Partial | [Model.h](../src/engine/render/Model.h), [ModelCache.cpp](../src/engine/render/ModelCache.cpp) (cache version 10), [ModelCacheAnimation.h](../src/engine/render/ModelCacheAnimation.h), [ResourceManager.h](../src/engine/utils/ResourceManager.h); M1-H verifies the production sampler codec and rejects old lossy cache versions | Model loads remain synchronous and path-keyed; cache keyed by source mtime/size; no Model memory budget or eviction. Async byte/graph loading exists separately (M1-A/B/C); full Model cache and loading integration remain unqualified |
| glTF import and fire-case fidelity | Partial | [gltf/](../src/engine/render/gltf) loader family, [FastGltfValidator.h](../src/engine/render/gltf/FastGltfValidator.h) | The user's own experience is that glTF was problematic for fire-heavy characters; see the roster section, and treat manual authoring or a project importer as expected |
| Source-format import and cooking | Game-owned by contract | [ENGINE_BOUNDARIES.md](ENGINE_BOUNDARIES.md) | The game supplies its own importers and cook policy |
| Scene and world streaming | Not found in inspected scope | One startup scene per descriptor in [ProjectDescriptor.h](../src/engine/editor/ProjectDescriptor.h); editor scene switching is frame-scheduled via `--open-scene-at`; no cell, region, streaming source, HLOD or data-layer concept in engine source | Outskirt Stand is one scene. Loaded map transitions plus async loading may satisfy a Colosseum-style structure; cell/HLOD streaming is conditional on a measured region-size decision, not assumed |
| Vegetation and water features | Not found in inspected scope | No vegetation, wind, LOD or water code found | Agate vegetation and water are separate qualification targets in the recovered material |

### Physics, collision and traversal

| Capability | Status | Evidence | Gap for this work |
| --- | --- | --- | --- |
| Physics/collision layer | M2-A CPU foundation and M2-F public-course integration accepted | [Static collision queries](COLLISION_WORLD.md) plus shared authored render/collision boxes in both native hosts | Actual environment proxies, representative content/scale and scene fitting remain; general dynamic physics is outside the baseline |
| Character controller | M2-B CPU foundation and M2-F action/camera/host composition accepted | [Kinematic character contract](CHARACTER_CONTROLLER.md): walk/run, gravity, grounding, sliding, slopes/steps and recovery, exercised in the native public course | Final character/environment fitting and game-scale movement remain; moving bodies and character-character response are outside this pass |
| Character sensor events | M2-C discrete foundation and M2-F visible sample dispatch accepted | [Trigger samples](CHARACTER_TRIGGERS.md): Enter/Stay/Exit with independent filters and copied snapshots; public player/zone membership response in both native hosts | Complete crossings between samples are not guaranteed; games still author volumes and interaction rules |
| Traversal (jumping, ledges, ramps) | Bounded CPU and native public-course subset qualified | M2-B contracts and M2-F ramp/stair, steep-slope rejection, ledge falling/landing and recovery captures | Jumping, climbing, traversal animation and final game-scene qualification remain |

The recovered material calls for kinematic character/world collision.
The original audit deferred vendor selection; M2-A selected manifest-pinned
Jolt 5.4.0 behind a Phlosion-owned API and accepted the static query foundation.
M2-B now supplies the bounded static-world controller policy. Destructible and
vehicle physics stay excluded.

### Animation

| Capability | Status | Evidence | Gap for this work |
| --- | --- | --- | --- |
| Skeletal animation sampling | Implemented; bounded verification | [ModelPoseSampler.cpp](../src/engine/render/ModelPoseSampler.cpp), [animation verification](ANIMATION_VERIFICATION.md); M1-G/H CPU contracts cover LINEAR/STEP/CUBICSPLINE T/R/S, hierarchy and loop/root-motion policies; native public-rig runtime/editor captures cover all three APIs | Per-instance CPU sampling; private rigs and the legacy Model GPU draw path remain unqualified |
| Blending, crossfade, layers, parameters | Partial M3-A/B foundation | [Animation blend](ANIMATION_BLEND.md) and [state machine](ANIMATION_STATE_MACHINE.md): CPU crossfade, masked override layers and typed parameter transitions feed public glTF skin palettes and synthetic native fixture | Additive layers, private roster and production transition authoring remain |
| Animation events and notifies | Partial M3-A/B foundation | Bounded ordered event collection plus state activation, one-shot hold and cancellation contracts | Authored metadata/cooking and lifecycle integration with VFX/audio/battle remain |
| Sockets and attachment | Partial | Node lookups exist (`getNodeIndexByName`, `getNodeGlobalTransformByName` in [Model.h](../src/engine/render/Model.h)) | Attachment exists as a lookup primitive; a socket/attachment contract with ownership and updates does not |
| Root motion | Partial | `sampleAnimatedPose(..., suppressHorizontalRootMotion)` in [Model.h](../src/engine/render/Model.h) | Suppression exists; controller consumption of root motion does not |
| GPU skinning paths | Partial; bounded verification | M1-G/H [weighted public rig](ANIMATION_VERIFICATION.md) through OpenGL/Vulkan/D3D12 world-mesh-textured drawing in runtime and real editor preview | A production character pipeline, legacy Model drawing, instanced/indirect paths and performance still need independent qualification |

### Navigation and AI support

| Capability | Status | Evidence | Gap for this work |
| --- | --- | --- | --- |
| Navigation mesh and path queries | Not found in inspected scope | Search for `navmesh`, `navigation`, `pathfind`, `astar`, `recast`, `detour` matched comments only | Gate: NPC route/pursuit must be collision-aware and work where the design requires it. A navmesh is optional if authored paths satisfy the slice; build or package it only when measurement shows authored paths are insufficient |
| AI support layer (steering, behaviour, perception) | Not found in inspected scope | No engine API found | Battle AI and NPC behaviour stay game-owned; any shared movement helper is a package/engine decision |

### Rendering, lighting, materials and VFX

| Capability | Status | Evidence | Gap for this work |
| --- | --- | --- | --- |
| Three native backends with a parity contract | Implemented | [RendererParityContract.h](../src/engine/render/RendererParityContract.h), backends under [render/](../src/engine/render), [VERIFICATION.md](VERIFICATION.md); no engine test references the parity contract, it is validated at runtime startup | Contract values are checked at startup by each backend; visual parity still needs per-scene three-API capture |
| Shared PBR shading and neutral PMREM | Implemented | [WorldPbrShaderShared.h](../src/engine/render/WorldPbrShaderShared.h), [NeutralPmrem.h](../src/engine/render/NeutralPmrem.h); no engine test identified | Analytic direct term plus image-based lighting; no dynamic-light system |
| Tone mapping and exposure constants | Implemented, fixed | ACES-style tone mapping at fixed exposure: `world.frag` line 220-221 (exposure 1.15), `world_material.glsl` line 67-76, `model.frag` line 20 (`kSceneExposure = 0.85`, line 60-63), plus [layout tonemap helper](../assets/shaders/lib/tonemap.glsl) | Constants are per-shader values, not a configurable exposure/environment pipeline |
| Dynamic lights, sky/environment parameters, fog, time-of-day values | Not found in inspected scope | No light, sky, fog or environment time-of-day API in engine source; PBR tunables are fixed shared defaults in [WorldPbrShaderShared.h](../src/engine/render/WorldPbrShaderShared.h) | The recovered material splits these: the engine provides lights/environment/fog/interpolation, the game owns the world clock and presets. Exposure exists only as shader constants today |
| Shadow maps | Not found in inspected scope | No shadow pass in engine source. A projected-shadow carrier does exist and is real: game-supplied RGBA texture, matrix, sampling scale and bias in [RenderBackendTypes.h](../src/engine/render/RenderBackendTypes.h) lines 168-180, consumed by all backends | Day/night and a city block need a shadow decision: build a shadow pass, integrate one, or extend the existing projected/baked carrier. The carrier's quality at city scale is Unverified |
| Stylised material support (wet surfaces, cyberpunk looks) | Extension implemented; content Unverified | Project material profiles via [MATERIAL_PROFILES.md](MATERIAL_PROFILES.md) and [WorldMaterialProfile.h](../src/engine/render/WorldMaterialProfile.h) are implemented and tested (`world_material_profile_contract`) | No wet or cyberpunk material content exists or has been qualified; that is project authoring work with three-API visual checks |
| Particles and VFX host | M3-H generic host; native effects partial | [ParticleSystem.h](../src/engine/vfx/ParticleSystem.h), [vfx_preview/](../src/engine/tools/vfx_preview), and PhlosionVFX's [effect host](../../PhlosionVFX/docs/EFFECT_HOST.md) with CPU spawn/attach/cleanup contracts | Game battle bindings and three-API native attack effects remain unqualified |
| Post-processing and anti-aliasing | Partial | Tone mapping and exposure exist as shader constants (see above); [assets/shaders](../assets/shaders) contain no post-processing chain and no anti-aliasing | Missing: a configurable environment/post pipeline, render scale and AA. The Autochess roadmap lists AA and render scale as core roadmap items |
| High-consumer qualification limits | Known, game-owned | [Autochess outstanding issues](../../../Games/PokemonAutochess/docs/OUTSTANDING_ISSUES.md) (`combat-target-focus` appearance guard, private content requirement, no controlled GPU CI gate), [tech debt](../../../Games/PokemonAutochess/docs/TECH_DEBT.md) (perf gate, preview smoke) | GPU qualification is local and manual today; expect the same for Cyberpunk until a runner exists |

### Audio

| Capability | Status | Evidence | Gap for this work |
| --- | --- | --- | --- |
| Audio playback | M3-F/G mixer and SDL adapter foundations | [Audio mixer](AUDIO_MIXER.md) owns decoded PCM voices, buses, fades, looping, spatial mono mixing and bounded SDL output; CPU and dummy-device contracts pass | No decoding/streaming, battle cue bindings or physical playback qualification |
| Audio settings surface | Mixer API available; host binding open | Volume and mute fields in [EngineServices.h](../src/engine/core/EngineServices.h) (lines 75-79), mixer master/bus gain and mute setters | Host preferences are not yet applied to the mixer; ambient has no service field |

### Player UI, text, dialogue, accessibility and localization

| Capability | Status | Evidence | Gap for this work |
| --- | --- | --- | --- |
| Text rendering | M3-E CPU layout foundation; native path partial | [UI text](UI_TEXT_LAYOUT.md) validates UTF-8 and supplies wrapped, scaled codepoint positions with fallback-font indices; [TextRenderer.h](../src/engine/ui/TextRenderer.h) remains byte-oriented and OpenGL-only | Real font coverage, shaping and all-three-API text drawing/HUD qualification remain |
| Immediate UI primitives | Partial; M3-D/E add CPU focus and text layout | [UI focus](UI_FOCUS_NAVIGATION.md) handles named actions, directional focus, repeats and modal rearm; [UI text](UI_TEXT_LAYOUT.md) computes glyph placement; [UIManager.h](../src/engine/ui/UIManager.h) still marks `drawCard` as a stub | No widget drawing or native battle/dialogue HUD; gameplay gating uses the caller's `blocksGameplay` result |
| In-editor UI | Implemented | Dear ImGui docking in [EditorShell.h](../src/engine/editor/EditorShell.h), per [EDITOR_ARCHITECTURE.md](EDITOR_ARCHITECTURE.md) | Editor-only, not a shipping game UI stack |
| Dialogue presentation hooks | Not found in inspected scope | No dialogue, typewriter or portrait API in engine source | Kept game-owned by the recovered plan; CPU text/focus foundations need native UI integration |
| Accessibility (text scale, contrast, motion) | Planned-only | Listed as future menu items in the [Autochess graphics roadmap](../../../Games/PokemonAutochess/docs/DISPLAY_GRAPHICS_ROADMAP.md) | Not found in engine source |
| Localization | Generic string ID table in M3-E | [UI text](UI_TEXT_LAYOUT.md) atomically replaces project-supplied ID-to-UTF-8 text values | Locale selection, formatting/pluralization, shipped fonts and content qualification remain project work |

### Scripting and reflection

| Capability | Status | Evidence | Gap for this work |
| --- | --- | --- | --- |
| Lua/sol2 dependency | Planned-only in the engine | Declared and linked in [vcpkg.json](../vcpkg.json) and [CMakeLists.txt](../CMakeLists.txt); no engine source usage | Optional for this work; the recovered plan does not require an engine scripting layer for the slice |
| Game-side scripting | Implemented in the consumer | Autochess `src/game/scripting` uses sol2 bindings (read-only inspection) | A new game would own or rebuild this |
| Reflection over engine types | Not found in inspected scope | No reflection or serialisation registry | Save data and future tooling need a data contract; today it is per-project |

### Persistence

| Capability | Status | Evidence | Gap for this work |
| --- | --- | --- | --- |
| Runtime save/load | Game-owned by contract | [ENGINE_BOUNDARIES.md](ENGINE_BOUNDARIES.md) assigns save data to the project; engine search found no save-game API | Slice step 5 needs versioned save data with a restore-correct-state acceptance check; the game must supply it |
| Editor-side persistence | Implemented | Authored-scene serialisation in [PhlosionAuthoredScene.h](../src/engine/assets/phlosion/PhlosionAuthoredScene.h); the editor shell writes project files | Editor state, not player progress |
| Settings persistence | Partial | Renderer preference plumbing and `videoPreferencesPath` in [EngineServices.h](../src/engine/core/EngineServices.h) | Video/renderer settings only |

### Editor, content workflows and agent interfaces

| Capability | Status | Evidence | Gap for this work |
| --- | --- | --- | --- |
| Editor host and project descriptor | Implemented | [EditorShell.h](../src/engine/editor/EditorShell.h), [ProjectDescriptor.h](../src/engine/editor/ProjectDescriptor.h), tests/TestProjectDescriptor.cpp | One startup scene per descriptor; no multi-region browsing concept |
| Project plugin ABI | Implemented | [EditorProjectPlugin.h](../src/engine/editor/EditorProjectPlugin.h), [EditorProjectPluginContract.cpp](../src/engine/editor/EditorProjectPluginContract.cpp) | Versioned ABI with configuration matching; the game must implement its plugin |
| Gameplay code reload | Implemented | [EditorGameplayReload.h](../src/engine/editor/EditorGameplayReload.h), tests/TestEditorGameplayReload.cpp | Windows DLL reload only; data and shader reload are not a contract |
| Performance instrumentation | Implemented | `EngineFramePerfStats` in [EngineServices.h](../src/engine/core/EngineServices.h), [EditorPerformance.h](../src/engine/editor/EditorPerformance.h), tests/TestEditorPerformance.cpp | Instrumentation exists; runtime budgets are not enforced |
| Editor automation and capture | Implemented | [EDITOR_PERFORMANCE.md](EDITOR_PERFORMANCE.md) documents unattended runs using `--hidden`, `--fixed-delta`, `--frames`, `--metrics-output`, `--open-scene-at`, `--asset-preview*`, deterministic `--scene-camera-position/-target`, `--state-directory`, and backend capture via `PHLOSION_BACKEND_SCREENSHOT_PATH` / `_FRAME`; `--open-scene-at` validates scene IDs and fails the run on load failure. `editor_performance_contract` (in [TestMain.cpp](../tests/TestMain.cpp)) and `editor_game_preview_routing_contract` cover part of this | Missing only a general external query/edit transport: this is an unattended CLI surface, not a queryable API for a separate process |
| Typed edit commands and undo routing | Implemented | [EDITOR_PROJECT_EXTENSIONS.md](EDITOR_PROJECT_EXTENSIONS.md) declares generic transform capabilities, declarative project commands and commit/cancel/undo routing; source spot checks: [EditorProjectPlugin.h](../src/engine/editor/EditorProjectPlugin.h) lines 180, 244-290, 523-526, 678-689; [EditorShell.cpp](../src/engine/editor/EditorShell.cpp) lines 1483-1494 and 1686; `editor_project_plugin_contract` is a named test | Missing: generic component coverage beyond project layout objects, and an external transport so a separate process or agent can query and submit edits without driving the UI |
| Reusable authoring packages | Planned/partial | [Package architecture](../../PhlosionPackages/docs/PACKAGE_ARCHITECTURE.md); only `phlosion.tile-tools` exists, and the present Blender workflow does not activate it | Additional district-authoring tooling does not exist yet; scope it from actual authoring needs rather than assuming roads or city blocks |

### Diagnostics, performance, builds and release

| Capability | Status | Evidence | Gap for this work |
| --- | --- | --- | --- |
| Frame instrumentation | Implemented | `EngineFramePerfStats` in [EngineServices.h](../src/engine/core/EngineServices.h); per-phase timing in [Scheduler.h](../src/engine/core/ecs/Scheduler.h) | No budget enforcement or regression gate |
| CPU contract tests | Implemented | Five CTest entries in [CMakeLists.txt](../CMakeLists.txt), containing twelve C++ contracts in [tests/TestMain.cpp](../tests/TestMain.cpp), plus the scope note above; [VERIFICATION.md](VERIFICATION.md) | Asset-independent only; nothing executes gameplay at scene scale, and ECS, input, animation, audio, physics and streaming have no independent engine test |
| Build and CI | Implemented | Windows Visual Studio 2026 workflow in [VERIFICATION.md](VERIFICATION.md) | Windows only; hosted CI cannot validate GPU parity or performance |
| Version pinning and distribution | Partial | Source pinning works: Pokemon Autochess `CMakeLists.txt` lines 193-266 pin an exact engine commit and consume it via `add_subdirectory` or `FetchContent`, with exact pins for PhlosionPackages (line 281) and PhlosionVFX (line 341). [EXTRACTION_STATUS.md](EXTRACTION_STATUS.md) records no installed CMake package, no SDK freeze and no packaged distribution | Release/API compatibility checking and clean-machine package qualification are outstanding. An installed SDK is optional, and the release boundary must record the mechanism chosen |
| Profiling at representative scale | Unverified | Autochess keeps profiling local ([TECH_DEBT.md](../../../Games/PokemonAutochess/docs/TECH_DEBT.md)) | Budgets and target hardware remain TBD until requirements are recorded |

## Character content and material qualification

The roster probes material and attachment handling at the edge of what the
current consumer covers: Autochess already exercises some adjacent cases
(Charmander-family fire material, shared Growl coverage, skinned units), but the
inspected evidence does not qualify the whole proposed roster. Each entry needs material,
animation and attachment checks, and none may assume automatic conversion of
fire from glTF. Roster ownership matters: the user explicitly added Quilava and
Typhlosion; the other entries are a prior proposal. Qualification needs the
project's own content and belongs to the game, while the synthetic engine
fixtures stay public.

| Character | What it stresses | Qualification note |
| --- | --- | --- |
| Umbreon | Baseline skeletal mesh, animation, eyes, standard materials | Establishes the reference case; must not regress when fire cases are added |
| Charmander / Charizard | Localised tail flame | Flame must track the tail bone and survive skinned animation |
| Ponyta / Rapidash | Large skinned flame surfaces over mane and tail | Tests flame geometry that is skin-bound rather than a spawned effect |
| Quilava | Multiple flame regions along the back | Tests repeated flame sections and per-region attachment |
| Typhlosion | Large eruptive flame collar/back effect | Probes whether the source uses skinned geometry, animated materials, spawned effect geometry, or a hybrid |
| Gastly (optional) | Translucent/smoky edges | Tests alpha/sorting behaviour, which the engine's blend policy must handle consistently on all three APIs |

Each case must render and animate consistently on OpenGL, Vulkan and Direct3D 12
at matching frames, with expected-content checks, and be re-authorable and
re-importable through the project workflow.

## Unreal-to-Phlosion portability strategy

We take requirements, not Unreal's architecture, and we do not promise engine or
content parity.

Neutral asset exchange (attributed to Epic Games,
[How the glTF exporter handles Unreal Engine content](https://dev.epicgames.com/documentation/unreal-engine/how-the-gltf-exporter-handles-unreal-engine-content)):
the exporter is data-driven and cannot represent everything in Unreal. It
carries meshes, animation sequences and materials/levels within restrictions,
and not arbitrary material expressions. Practical consequence: prefer neutral,
contract-checked assets and treat import, material and animation support as
per-asset-class work proven by golden fixtures. Never promise automatic
gameplay, Blueprint or fire-effect conversion. This matches the user's own
experience that glTF was problematic for fire-heavy characters.

World partitioning (attributed to Epic Games,
[World Partition in Unreal Engine](https://dev.epicgames.com/documentation/unreal-engine/world-partition-in-unreal-engine)):
the documented model is spatial cells with streaming sources, actor-reference
bundling, HLOD and data layers. Useful evidence that world partitioning is a
lifecycle and residency feature, not just a scene loader. Outskirt Stand does
not require this; any later requirement depends on measured region size and
the acceptable transition experience.

Strategy:

1. Keep the design and the game data engine-independent, as the recovered
   discussion already recommends. The battle simulation should be portable:
   pure simulation logic must not include engine types, so it can run in a
   headless test harness and be replayed.
2. Move neutral formats and source assets, and keep game-specific import and
   cook policy in the game repository.
3. Fix an asset contract per class (mesh, skinned mesh, animation, material,
   environment) with golden fixtures and expected-content checks, starting with
   the roster above.
4. Keep reusable systems in the engine or PhlosionPackages/PhlosionVFX; keep
   Pokemon rules, content and adapters in the game.
5. Prove sameness, not similarity: run the same game simulation replay in the
   Unreal prototype and in Phlosion and compare golden results (event/state
   sequence, not pixels alone) so a port can be checked mechanically.
6. Track the Unreal-side pieces a port must re-implement rather than convert:
   Blueprint gameplay graphs, UMG/UI widgets, Niagara effects, material graphs
   and Animation Blueprints. Each needs an owner, a neutral-data equivalent,
   and a per-item re-implementation check; none should be assumed portable.

## Milestones

Owners are roles, not people: **Engine**, **Game** (the Cyberpunk project),
**Packages** (`PhlosionPackages`), **VFX** (`PhlosionVFX`), **Astra** (scope and
acceptance). All milestones carry the cross-cutting gates below. The sequence
follows the accepted priority order in PLAN.md:
requirements and benchmarks, then the engine fixture and asset contracts, then
the walkable Outskirt Stand, the double battle, the persistent loop,
representative environment scale, the editor workflow, and finally the
migration gate. Each milestone names the engine capabilities it therefore pulls
in; no effort estimates are given. Editor authoring work (M6) starts alongside
M1/M2 rather than waiting for the environment milestone; M5 is a prerequisite
only for its final qualification.

**M0 - Requirements and benchmarks.** Owner: Astra + Game. Prerequisite: none.
Exit gate: the recovered context is recorded in the game repository as explicit
requirements and measurable benchmarks covering target platform and hardware,
frame-time and memory budgets, slice acceptance variants, save semantics, audio
scope, environment scale, and excluded scope. The recovered discussion describes
most of this but is not a signed-off specification, and budget values stay TBD
until hardware targets are recorded.

**M1 - Engine fixture and asset contracts.** Owner: Engine + Game.
Prerequisite: M0 (a provisional subset is enough to start). Two separate tracks
that must not be conflated:

- **Engine track (public, synthetic).** An engine-owned, asset-independent
  fixture scene plus per-class asset contracts (mesh, skinned mesh, animation,
  material, environment) with golden fixtures and integrity tests. It uses
  synthetic or redistributable content only and runs without private data.
- **Game track (private content).** The project imports and qualifies its own
  roster and district content, including the fire and translucency cases, using
  its own depot and capture workflow.

This milestone covers existing animation basics and the asset contracts it
checks; blending, events and attachments are completed in M3 so the two do not
cycle. It also starts the async asset work: an explicit loading lifetime and
dependency model, cancellation, and a memory budget for loaded assets, with
load-time and residency measurements feeding M4/M5.

**M2 - Walkable Outskirt Stand.** Owner: Engine + Game. Prerequisite: M1 for
scene content. This milestone pulls in the collision and character-controller
capability, the exploration camera, and action-based input:

Progress: M2-A's static queries, M2-B's CPU kinematic controller, M2-C's discrete
trigger samples, M2-D's named actions/SDL controller adapter and M2-E's CPU
exploration camera are accepted. M2-F adds the shared public course and accepted
automated native runtime/editor integration, with basic physical controls also
exercised. The provisional game's v2 scene data and 108-capture replay now cover
its recovered collision, ramp, blocker, reset and synthetic device/focus cases.
Visible Outskirt steps, physical-device and deferred editor toolbar checks, and
replacement-content fitting remain outside the exit gate. Trigger
sampling does not guarantee events for complete
crossings between samples; the camera probe does not qualify full-frustum or
swept-trajectory clearance.

- static collision, layers, triggers, raycasts and sweeps, plus a kinematic
  controller with walk/run, gravity, grounding, slopes and steps, covered by
  CPU contract tests;
- the exploration camera rig with framing and a stable edit/runtime split for
  camera data;
- named actions with controller support, deadzones and rebinding;
- collision-aware NPC routes where required, including blocked-path handling;
  authored paths may suffice, with a navigation package introduced only if needed.

Exit gate: the player boots the Outskirt Stand scene and walks and runs against
walls, slopes, steps and triggers with both controller and keyboard/mouse at
16:9 and 21:9, with matching-frame three-API captures. The gate also covers
controller hotplug, input focus loss and resume, and a stuck/blocked-player
recovery case. Build or integrate is the project's decision; the behaviour
contract is the engine's.

**M3 - Double battle.** Owner: Game + Engine + VFX. Prerequisite: M2. This
milestone pulls in the animation runtime, battle camera rigs, runtime UI and
audio:

Progress: M3-A adds an [asset-independent animation blend foundation](ANIMATION_BLEND.md)
with crossfades, masked override layers, timed event collection and public glTF
skin-palette integration. M3-B adds [parameter-driven state](ANIMATION_STATE_MACHINE.md).
M3-C begins the [battle camera](BATTLE_CAMERA.md) with a CPU rig. M3-D adds
[runtime UI focus routing](UI_FOCUS_NAVIGATION.md) on the CPU. M3-E begins
[text layout and string IDs](UI_TEXT_LAYOUT.md) on the CPU. M3-F adds the
[runtime audio mixer](AUDIO_MIXER.md) on the CPU.
M3-G adds opt-in SDL queued-device output and a no-device sink.
M3-H adds a generic effect host in PhlosionVFX with spawn/attach/cleanup rules.
M3-I integrates Wes's provisional locomotion clips through the state machine
in the Outskirt preview. M3-J adds a native provisional battle-field camera
preview with four actor stand-ins. M3-K adds a fixed-tick encounter cue
sequence with temporary HUD, audio-mixer and flash bindings plus cleanup.
M3-L connects its generated cue to the SDL output adapter in interactive hosts.
M3-M adds the private recovered battle-HUD presentation and a game-owned
PhlosionVFX impact adapter to the provisional encounter.
Battle systems, roster qualification and the end-to-end exit gate remain.

- blending, crossfade, layers, parameter-driven state and animation events,
  with the roster characters as fixtures;
- camera rigs with target framing, blends, cuts and shake;
- a runtime UI layer with text layout, focus/controller navigation and input
  routing sufficient for the battle HUD, plus NPC dialogue for the Rider Willie
  interaction; qualify UTF-8/font coverage, wrapping, scaling, string identifiers,
  and appropriate subtitle and reduced-motion controls for the selected scope;
- music, SFX and ambient playback with buses, volume, looping and fades, with a
  device-failure path; build or integrate is the project's decision;
- VFX host spawn/attach control for attack effects, with reusable effects from
  PhlosionVFX.

Exit gate: Espeon and Umbreon enter a double battle against enemy Pokemon; the
battle runs with camera, animation, VFX, audio and HUD on all three APIs with
timed animation events, correct cleanup on battle exit, and cancel/skip paths
that leave no orphaned effects, audio or camera state. The gate also covers
spatialised audio and a device-failure fallback. Control returns to the
overworld.

**M4 - Persistent RPG loop.** Owner: Game + Engine. Prerequisite: M3. This
milestone pulls in the save-data contract, scene transition handling, async
loading completion (lifetime and cancellation across transitions) and the
dialogue/quest data hooks the game needs. Exit gate: save, quit and reload
restore the correct state across a scene transition with versioned save data
and an explicit schema-migration path; interrupted and corrupt saves are
rejected or recovered without data loss; scene unload releases resident
content; and the loop is deterministic under headless tests.

**M5 - Representative environment scale and presentation.** Owner: Engine +
VFX + Packages + Game. Prerequisite: M4. This milestone pulls in dynamic lights
with a stated shadow approach, sky/environment parameters, fog and exposure with
runtime day/night interpolation, reusable rain and splash effects from
PhlosionVFX, water rendering, and vegetation instancing with wind, culling and
LOD for Agate, plus a configurable exposure/environment pipeline replacing the
fixed shader constants. Qualify an anti-aliasing and render-scale policy with
the UI and transparent effects; select additional bloom/reflection effects from
the art target rather than assuming a full post-processing stack.
Exit gate: a representative district at target density
holds the M0 budgets on the target machine under day, night and rain, with
matching-frame three-API captures, expected-content checks, and separate water
and vegetation qualification scenes. The gate includes a repeated
transition/weather soak, p95 and p99 frame times against the M0 budget, and a
VRAM ceiling for the same scenario.

**M6 - Editor production workflow and agent interfaces.** Owner: Packages +
Engine + Game. Prerequisite: begins alongside M1/M2 (authoring work starts from
the first district content); M5 gates only its final qualification. Exit gate:
designers can author and re-import a district through project adapters and
packages; and an agent can **inspect** structured scene and asset state,
**command** deterministic edit and run operations, **validate** assets and
contracts with structured results, and **capture** deterministic frames with
backend and frame identifiers, without manually driving the editor UI. The
existing unattended automation and capture flags are the starting point; the
missing piece is a general external query/edit transport beyond the in-process
plugin adapter. Surface and asset schemas stay versioned, and the runtime must
not depend on the agent interfaces.

**M7 - Migration and release gate.** Owner: Astra + Game + Engine.
Prerequisite: M2 to M6 cover the slice. Exit gate: the game consumes an exact
engine and package version through a recorded mechanism, and the return gate
below passes end to end, including the simulation-replay and adapter-mapping
checks in the portability section.

The release build must record the engine/package/content revisions actually
used: a developer's local source-directory override does not enforce the
FetchContent commit pin. Test API/plugin compatibility and content/save schema
migrations, clean-machine dependency restoration and packaged startup, missing
or corrupt content diagnostics, crash/log collection, and required runtime
dependency notices. An installed SDK is optional; a reproducible recorded
source pin is sufficient. The release makes an explicit compatibility promise,
with maintenance patches permitted rather than a promise never to change code.

## Cross-cutting gates

These apply from the first slice; they are not cleanup phases.

- **Renderer parity**: every affected gameplay and editor case runs on native
  OpenGL, Vulkan and Direct3D 12 at matching frames with expected-content
  checks, following [VERIFICATION.md](VERIFICATION.md) and the consumer
  [parity contract](../../../Games/PokemonAutochess/docs/RENDERER_PARITY_CONTRACT.md).
  Existing Autochess captures do not certify this game.
- **Profiling**: budgets are measured, not asserted. No readiness percentage or
  benchmark number is claimed while hardware targets are unknown.
- **Data stability**: asset, scene and save schemas are versioned with explicit
  migration paths before content scale grows.
- **Authoring**: everything shipped must be authorable and re-importable by the
  project through documented contracts, including through the agent interfaces
  above.

## Migration boundary and return gate

Boundary: engine primitives stay in PhlosionEngine; reusable specialized
systems may live in PhlosionPackages or PhlosionVFX; Pokemon rules, content,
Shadow/battle semantics and game-specific editor adapters stay in the game
([ENGINE_BOUNDARIES.md](ENGINE_BOUNDARIES.md)). The game depends on
PhlosionVFX; PhlosionVFX does not copy game code.

The return gate for leaving a prototype (Unreal or otherwise) requires all of:

1. The packaged Outskirt Stand slice runs from a build a reviewer can launch.
2. Save, quit and reload restore the correct state across a scene transition,
   with versioned save data.
3. Authoring and re-import round-trip through the project's editor workflow,
   including the character roster's fire and translucency cases.
4. Representative content scale is measured against explicit CPU, GPU and
   memory budgets (values TBD until M0 records hardware targets).
5. Every affected native API passes matching-frame visual and expected-content
   checks in gameplay and in the affected editor preview, plus CPU contracts.
6. The same game simulation replay produces matching golden results in the
   prototype and in Phlosion, and every Unreal-side Blueprint, UI, Niagara,
   material and Animation Blueprint adapter has a recorded owner and
   re-implementation check.
7. Unverified coverage and known limitations are written down rather than
   inferred from a green build.

## Open questions

- Target hardware and the resulting CPU/GPU/memory budget values.
- Art and content choices that only the user can settle: which source materials
  and effects should be preserved literally versus reinterpreted, and how the
  Cyberpunk setting should look.
- Unclear recovered intent, limited to design questions the audit cannot resolve
  from code: which battle presentation timing and camera language are wanted,
  and whether a later city-scale region needs streaming or authored scene
  transitions.
- Build-or-integrate selections once requirements exist: physics/collision
  library, audio backend, water and vegetation approach, and the shadow approach
  for day/night.

The Markdown audit log for this document lives in
[EVIDENCE.md](agent-work/cyberpunk-readiness/EVIDENCE.md).
