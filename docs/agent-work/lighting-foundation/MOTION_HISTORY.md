# Motion history producer foundation — 2026-10-26

C2 remains partial. The original CPU producer groundwork below is now connected
to native field velocity/identity targets on Windows D3D12 and Vulkan.
Battle Pokemon producers are now connected in [the battle milestone](BATTLE_MOTION.md).
Temporal reconstruction and removal of the source-colour redraw remain outstanding. C1/C2 and P2 are not complete; no speedup is claimed.

## Implemented

- WorldMotionHistory owns separate previous-camera/object state per view.
  Successful captures promote atomically; unwinding aborts without promotion.
- Scene-owned object identity plus authored primitive/LOD identity survives
  draw reordering and compacted visibility lists. No geometry pointer, draw
  index, skin-palette pointer or compacted instance index is used as identity.
- Previous model/instance transforms, bone palettes, specialized vertex
  parameters and wind palettes are copied before caller storage can change.
  A revision or deformation-layout change rejects the previous state.
- First visibility, visibility gaps, view/scene resets, resize, aborted frames
  and explicit camera cuts reject history. Callers must signal discontinuities
  and gaps; simulation ticks are not a substitute for rendered-view identity.
- The glTF adapter supplies sampled mesh-node transforms and skeletal palettes
  when explicitly enrolled. Prewarm and transparent submissions do not enroll.
- The velocity reference contract is unjittered engine clip space to top-left
  previous-UV-minus-current-UV displacement and previous 0..1 depth. Nonfinite,
  behind-eye and near/far-plane-invalid values are rejected.

The research anchors are the existing SOURCE_RENDER_CALL_PATH.md in
Cyberpunk2077Research/lighting/reflections, the material-output motion convention
and the depth/camera motion initialization in renderNode_MotionBlur. This is
independent Phlosion code, not a renderer imported from the reference project.

## Game producer qualification

PokemonCyberpunk opts in with PHLOSION_CAPTURE_MOTION_HISTORY=1. Normal launches
leave it disabled. The field records its primary geometry only: stand/ground,
boundary, Wes/NPC/story glTF poses and GPU-wind desert instances. Shadow maps,
probe faces, planar reflections, screen-source captures and the battle scene
snapshot do not promote this history. Editor scene and game-preview cameras
have separate counters and previous poses. Encounter transitions clear field
history; release drops it.

The handshake's motion_history object reports objects, valid_previous,
deformed, deformation_changed, rigid_changed, deformation_bytes and rendered_views.
Its stage is explicitly pose-producers-only.

Original producer-only limits: battle Pokemon producers and native raster
writers were missing; the native writer milestone below supersedes the latter; legacy CPU-deformed foliage is not enrolled; rain,
transparent surfaces and camera-independent secondary views have no motion
producer. This milestone does not add temporal accumulation to them. Primary
camera jump detection is conservative; future camera controllers must supply
explicit cut/generation events where appropriate.

## Checks and evidence

Worker: gpu-worker Windows, MSVC RelWithDebInfo (optimized), EVGA RTX 2060 SUPER.
Task evidence: WORKER_EVIDENCE_ROOT/phlosion-motion-foundation-20261026.
Controller handoff: PROJECTS_ROOT/Phlosion/PhlosionEngine/artifacts/agent-work/motion-foundation-20261026.

PhlosionWorldMotionHistory tests owned palettes, instance/LOD identity, culled
and reappearing objects, independent cameras, failed captures, cuts, resize,
revision/layout changes, invalid inputs and velocity sign/depth conventions.
The glTF renderer contract samples actual authored rig poses, submits two
instances of one asset, checks previous-pose retention and excludes prewarm
and transparent draws. Existing game environment, profile, exploration and
battle contracts remain required.

Native runtime/editor checks use both enrolled Windows APIs, fixed wet-night
frames, camera movement, validation layers and image controls. Check cpu.json,
runtime-checks.json and final-checks.json for the actual qualified source,
results and retained failures. Candidate-r2 is the final per-view-counter
revision; earlier candidate results alone do not qualify that revision.
No performance timing is inferred from these correctness runs.

## Next connected work

1. Add native velocity and exact identity targets to the shared surface pass.
   Evaluate the existing vertex deformation twice with owned current/previous
   inputs, including previous wind state; preserve alpha/LOD coverage.
2. Enroll battle characters and propagate explicit view/cut events through
   both hosts. Qualify moving-camera, rigid, skinned and wind vectors numerically
   on D3D12/Vulkan, including disocclusion and resize.
3. Add temporal invalidity/dilation/reconstruction and previous composed-colour
   feedback, connect the dedicated reflection consumer, then remove the
   redundant redraw and inline search. Measure the completed connected path.

Hybrid rain's material-specific impacts and puddle-only rings are preserved.


Final qualification: all three engine suites and all four game suites passed.
The final candidate passed wet-field runtime, wet-field editor and real battle
entry checks on D3D12 and Vulkan. The first two capture 626 visible parts with
626 valid previous states; 430 deformation states change between frames.
Those states occupy 138,752 deformation bytes for this view. Battle entry
clears the field object history and the field snapshot does not repopulate it.
Final native 1080p wet-night images are pixel-identical to capture-disabled
controls on each API. Earlier camera-orbit checks captured 640 visible parts
and 444 changing deformation states, with valid previous identities.

These are correctness and preservation results, not GPU motion-image tests,
retail parity measurements or performance timings.


## Native field velocity and identity — 2026-10-27

PHLOSION_NATIVE_MOTION=1 replaces the field contact prepass with the shared
surface capture and enrolls the actual primary-view draw submissions.
The ordinary geometry draw does not submit a second history record. Normal
launches keep the new path disabled. PHLOSION_CAPTURE_MOTION_HISTORY remains
available for CPU-only qualification. The native handshake stage is
native-field-velocity.

The surface pass now has four existing RGBA16F material planes, a fifth RGBA16F
plane containing signed previous-UV-minus-current-UV, previous 0..1 depth and
validity, and an R32_UINT identity plane. IDs retain all 32 bits; zero means no
enrolled owner. This is a qualification layout, not the completed compact C1 layout.
The later compact format milestone supersedes these material formats; motion
and exact IDs remain unchanged. See [compact surfaces](COMPACT_SURFACES.md).

Each surface vertex evaluates the same skin/wind function with owned previous
inputs and then current inputs. Current alpha/LOD coverage determines the
rasterized pixels. Invalid history writes a nonzero current identity with zero
validity; masked holes, transparent draws and unenrolled owners have no usable
motion. Later reconstruction must reject disocclusions using depth and identity;
the writer alone does not solve temporal visibility.

The glTF and desert adapters scope draw enrollment and clear it afterward.
Packets own previous transforms and deformation data until upload; previous wind
uses the game's existing material hook, including its early-return path.
Vulkan reuses the existing instance storage binding for packet words. D3D12 uses
a vertex-stage structured buffer. The interface does not change WorldTextureData
or the model-provider ABI. Project Vulkan manifests may supply motion_vertex;
attempting shared capture without the required variant fails explicitly.

Native readback is opt-in qualification only. Queue captureWorldSurfaceMotion
during the active scene after the surface capture, then readWorldSurfaceMotion
after endFrame. It synchronizes the GPU; never use it for timing. The game runtime
can emit identity coverage/movement counts at its requested capture frame with
PHLOSION_MOTION_READBACK_PATH. Production rendering performs no readback.

Evidence: gpu-worker Windows, optimized RelWithDebInfo, EVGA RTX 2060 SUPER,
WORKER_EVIDENCE_ROOT/phlosion-native-motion-20261027; controller handoff is
PROJECTS_ROOT/Phlosion/PhlosionEngine/artifacts/agent-work/native-motion-20261027. PhlosionWorldMotionNativeTests reads actual
GPU targets and checks first visibility, static/rigid/camera/combined motion,
both bone-palette layouts, alpha holes, transparency, behind-eye rejection,
per-instance identities, enrollment clearing and the real project wind shader.
Its high-bit identity fixture checks exact integer storage, and vector/depth
tolerances account for RGBA16F rounding. The short first Vulkan profile-load
timeout is retained with the successful longer-allowance run.

Next: battle Pokemon motion producers and explicit view/cut plumbing, then
invalidity/dilation/temporal reconstruction, existing composed-colour feedback
and removal of redundant reflection work. Native buffers alone neither complete
C2/P2 nor establish a new parity percentage or frame-time gain.


Native writer qualification completed on the Windows worker: all seven CPU
contract suites passed; native numerical tests passed on both APIs; all 24
surface material/coverage/resize/reset tests passed. Both APIs passed wet-field
runtime/editor, camera orbit and real battle entry. Normal default-off images
are pixel-identical to the prior controls. The wet field records 626 parts,
including 430 changing deformation states, and its GPU readback contains 235
visible identities on D3D12. These are coverage/correctness observations, not
parity percentages or frame-time results.

The final readback guard was rebuilt and requalified on both APIs. A subsequent
D3D12 shader whitespace cleanup was rebuilt and checked with the native fixture
and runtime/editor images; those images are identical to the guard-qualified
candidate. Refer to native-final.json, surface.json, game-final.json,
native-clean.json, game-clean.json, cpu.json and validation.json. Original
failures/timeouts and cold shader-cache waits remain in the task evidence.


Battle follow-up: [native model poses, authored camera cuts and view transitions](BATTLE_MOTION.md).
