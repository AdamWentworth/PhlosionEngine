# Engine verification

## Automated source and CPU checks

[Engine CI](../.github/workflows/ci.yml) runs on pushes to `main`, pull requests
and manual dispatch. It uses the Windows Visual Studio 2026 runner, restores
the manifest-pinned vcpkg dependencies, builds all enabled Debug targets with
the engine warning policy enabled, and runs CTest.

The settings helper applies `/W4 /WX` to targets that use it. M2-D's compiler
audit found that the pre-existing aggregate `PhlosionEngineTests` target bypasses
that helper and retains `/W1 /WX-` in the ordinary generated project. Its local
M2-D qualification additionally forced `/WX` and checked actual compiler records;
that temporary qualification does not change the repository's default policy.
Do not infer every target's flags from the top-level option alone.

The build includes the native editor, plugin probe and Vulkan shader programs.
It does not fetch a game, private assets or optional package repositories.
No test in the current engine CTest suite needs a graphics context.

| CTest entry | Coverage |
| --- | --- |
| `PhlosionCollisionContracts` | Eight standalone C++ groups against real Jolt geometry: analytic ray/capsule contacts, transformed boxes/meshes, exact overlaps, filters/metadata, ID/capacity/geometry ownership, malformed input/output preservation, configuration bounds, thread ownership and repeated authored query sequences |
| `PhlosionCharacterContracts` | Ten standalone C++ groups against real Jolt geometry: fixed-tick analog movement, gravity/landing, walls/corners/thin sweeps, slopes/ceilings, stairs/floor following/ledges, recovery/teleport, filters/support removal, identities/capacity/threads, malformed inputs/transactional bounds and repeated authored sequences |
| `PhlosionTriggerContracts` | Seven standalone real-geometry groups for discrete sensor Enter/Stay/Exit membership, precise overlap geometry, filters/metadata/order, teleport and failure preservation, removed/replaced sensors, character/world/thread ownership, capacity and repeated sequences |
| `PhlosionInputContracts` | Six SDL-free groups for named action mapping, button/axis transitions, rebinding, deadzones, controller identity/selection, focus/enable recovery, bounds and thread ownership |
| `PhlosionUiFocusContracts` | Spatial focus, disabled/dynamic items, keyboard/synthetic-controller action composition, held repeat, modal gating/rearm, activation/cancel precedence, transactional failures and thread ownership |
| `PhlosionUiTextContracts` | Strict UTF-8, transactional string-table replacement, fallback metrics, analytic wrapping/scaling, explicit line breaks, unsupported text and preserved output on failure |
| `PhlosionAudioMixerContracts` | Owned PCM clips, numeric stereo mix, buses/mute, loop/end, sample-time fades, spatial pan/attenuation, cleanup, invalid input and owner-thread rules |
| `PhlosionSdlAudioContracts` | SDL dummy-device startup/retry, null-sink progress, queue cap, stop/shutdown cleanup, subsystem references and owner-thread rules |
| `PhlosionSdlInputContracts` | Three real SDL virtual-device groups for mapped-controller discovery, all 15 buttons/six axes, normalization, queued taps and event/poll duplicate observations, hotplug/disconnect, window focus filtering and subsystem/handle ownership |
| `PhlosionCameraContracts` | Ten groups for exploration framing, delta/rate controls, follow/recovery damping, real sphere obstruction queries, validation/ownership, relative movement and repeated CPU input/character/camera composition |
| `PhlosionBattleCameraContracts` | CPU battle camera framing at multiple aspects, impossible-shot rejection, cuts/blends/tracking, deterministic shake, reduced motion, reset, transactions and thread ownership |
| `PhlosionExplorationContracts` | Optional public course: SDL extension validation, viewport/input gates, zero/multiple-tick cadence, bounded replay, real movement/sensors/camera/NPC composition, safe recovery and SDL lifetime ownership |
| `PhlosionExplorationEvidenceContracts` | Optional course's independent state and ray/box pixel oracles, malformed state/content rejection and pristine recovery; no graphics context |
| `PhlosionSdlExtensionValid` | Plugin probe accepts the sample's separate versioned SDL callback table |
| `PhlosionSdlExtensionMalformed` | Plugin probe rejects a present truncated SDL callback table before reading callbacks |
| `PhlosionSdlExtensionAbsent` | Plugin probe accepts the existing scene plugin through the unchanged legacy contract |
| `PhlosionEngineContracts` | Thirty-four C++ contracts: material profiles, containers, typed static meshes, scenes, environment patches, asynchronous asset loading, the dependency-aware asset graph session, staged scene/prefab archive activation, public archive/render-fixture data and image guards, existing glTF scene/skeleton/clip import, material metadata, real PNG/JPEG texture decoding and role fallbacks, temporary-fixture ownership, shared LINEAR/STEP/CUBICSPLINE pose sampling, M3 crossfades/masked override layers/timed events/parameter-driven state, model-cache sampler codec/version identity, owned CPU glTF scene validation/retention and renderer preflight, archive-backed glTF worker sessions, GPU resource-scope state/accounting, animation/material/integrated-scene content and lifecycle evidence guards, descriptors, renderer preference, camera pan, plugin ABI, preview routing, reload and performance accounting |
| `PhlosionEngineSemanticBoundary` | Removed game systems, identifiers, materials and former source paths cannot return to engine code/shaders |
| `PhlosionPackedNormalContract` | Shared packed-normal source contract |
| `PhlosionMaterialDebugViewContract` | Material debug-view source contract |
| `PhlosionReviewLightingProfileContract` | Generic review-lighting source contract |
| `PhlosionPublicAssetFixtureHarness` | CPU-only synthetic image/manifest negative tests, capture provenance and native process handling; enabled when fixture hosts and PowerShell are available |
| `PhlosionAnimationFixtureHarness` | CPU-only skeletal fixture image/provenance/transition rejection checks and process handling; enabled when animation fixture hosts and PowerShell are available |
| `PhlosionMaterialFixtureHarness` | CPU-only five-map content, fallback, evidence, numeric-boundary and run-path rejection checks; enabled when material fixture hosts and PowerShell are available |
| `PhlosionSceneFixtureHarness` | CPU-only integrated scene evidence/content rejection checks, including 29 mutations and pristine recovery; enabled when scene fixture hosts and PowerShell are available |

Local equivalent:

```powershell
cmake --preset vs2026 -DPHLOSION_BUILD_EDITOR=ON -DPHLOSION_BUILD_TESTS=ON -DPHLOSION_WARNINGS_AS_ERRORS=ON
cmake --build --preset debug --parallel 4
ctest --preset debug
```

Tests enforce particular contracts, not a blanket claim of correctness. Review
the changed implementation and relevant consumer behavior as well.

The opt-in [collision world](COLLISION_WORLD.md) has its own executable linked
only to `Phlosion::Collision` and a 120 second CTest timeout. Its eight groups
are separate from the 32 `PhlosionEngineContracts` groups. Queries use real
Jolt boxes and triangle meshes without a graphics context. They do not qualify
character movement, trigger events, scene integration or visual behavior.

The separate [character controller](CHARACTER_CONTROLLER.md) executable links
only `Phlosion::Collision` and also has a 120 second timeout. Its ten groups
qualify the bounded static-world movement API, including 4096-character capacity,
support removal, blocked teleport and rollback preservation. They do not qualify
host scheduling, input devices, trigger events or a native walkable scene.

The separate [trigger sampler](CHARACTER_TRIGGERS.md) executable links only
`Phlosion::Collision` and has a 120 second timeout. It checks discrete endpoint
membership and lifecycle event state. A complete crossing between samples is
explicitly outside that behavior; no continuous sensor detection, game event
dispatch, host scheduling or native walkable-scene qualification is implied.

The [input action layer](INPUT_ACTIONS.md) has separate pure and SDL-backed
executables with 120-second timeouts. SDL tests use owned virtual controllers
without a window or graphics context. They establish the adapter's bounded
behavior, not physical-device coverage, editor capture/focus behavior or a
native walkable scene. Existing hosts are not migrated by the input module.

The [exploration camera](EXPLORATION_CAMERA.md) has a standalone CPU executable
linked to Camera and Input, without SDL or rendering. It uses real collision
geometry and kinematic characters. Mathematical 16:9/21:9 framing checks are
separate from native image qualification; final radial sphere clearance is
separate from swept camera motion or whole-frustum clearance. Host integration
still requires the native runtime/editor scene checks below.

The [runtime UI focus module](UI_FOCUS_NAVIGATION.md) has a standalone CPU
executable linked to Input, without SDL or rendering. It checks keyboard and
synthetic-controller action composition, but does not establish font coverage,
native text/layout presentation or automatic gameplay action consumption.

The [runtime UI text module](UI_TEXT_LAYOUT.md) also runs without a graphics
context. Its synthetic metrics prove text-layout and fallback-selection math,
not coverage of a real font, native glyph rasterization or cross-API images.

The [runtime audio mixer](AUDIO_MIXER.md) has a standalone CPU contract. Its
numeric samples prove mixing and lifecycle rules. The SDL adapter's dummy-driver
contract proves queuing and failure cleanup, but not physical device playback
or a game's cue bindings.

The [battle camera](BATTLE_CAMERA.md) has a standalone CPU executable linked to
Camera. It checks analytic perspective framing with independent projection,
cuts, moving-target blends, shake containment, reduced motion and transactional
failures. It changes no native presentation path; runtime/editor camera images
remain a separate M3 qualification gate.

The optional [public exploration course](EXPLORATION_FIXTURE.md) adds the five
entries above when its fixture, editor, tests and existing scene fixture are
enabled. Its native matrix uses the actual editor Game preview and standalone
runtime, with explicitly synthetic focus and owned SDL virtual controllers.
The primary 96 captures cover eight states, both hosts, all three APIs and exact
16:9/21:9 viewports. Supplemental steep-slope and ledge-drop cases add twelve
16:9 captures. Independent state/content/provenance guards complement the
unchanged image thresholds; physical-device observations remain a separate check.

The asynchronous loading contract and its retained-byte-budget limits are
described in [ASYNC_ASSET_LOADING.md](ASYNC_ASSET_LOADING.md). The loader tests
are CPU only: they coordinate a synthetic provider with condition variables and
never create a graphics context. `PhlosionEngineContracts` carries a 120 second
CTest timeout as a regression backstop so a blocked loader cannot hang the suite
unboundedly.

The dependency-aware graph session, its required-closure and hash rules, and its
accounting limits are described in
[ASSET_DEPENDENCY_LOADING.md](ASSET_DEPENDENCY_LOADING.md). That contract is also
CPU only: it drives a synthetic in-memory provider, gated reads and public PHRC
fixtures, and creates no graphics context.

The staged scene/prefab activation session, its candidate lifecycle and its
snapshot ownership rules are described in
[ARCHIVE_ACTIVATION.md](ARCHIVE_ACTIVATION.md). That contract is CPU only as
well: it composes the graph session with the existing typed archive stores,
drives a synthetic in-memory provider with gated reads, and creates no graphics
context. It makes no game, editor or renderer integration claim.

The [archive-backed glTF session](GLTF_SCENE_SESSIONS.md) builds on that contract:
provider reads and glTF/image decoding run on owned workers, with atomic scene
activation, stale-result suppression, retained snapshots and accepted-output
byte limits. CPU gates establish thread identity and cancellation/destruction
behavior. [World resource scopes](WORLD_RESOURCE_SCOPES.md) have separate CPU
state/accounting tests and native lifecycle qualification. The 20 adversarial
lifecycle evidence mutations reject leaks, wrong identities, stale release
observations and malformed numbers; they do not substitute for native runs.

The [static mesh contract](MESH_ASSETS.md) covers deterministic PHMS encoding,
literal little-endian payload bytes, strict schema/UTF-8/geometry validation,
limits, multi-submesh slot preservation and unchanged outputs on failure.
Its fixture consumer also exercises two-slot activation and preservation of
active A when candidate bindings or mesh semantics fail. These are CPU checks;
the rendered matrix below uses the existing single-slot visual scene.

The [glTF import contracts](GLTF_IMPORT_VERIFICATION.md) exercise the existing
file loader, scene/skeleton/clip importer and CPU material resolver using tiny
authored public fixtures. They verify imported values and fixture ownership;
M1-H also samples imported cubic translation/rotation/scale curves. The separate
[texture contract](GLTF_TEXTURE_VERIFICATION.md) added in M1-I decodes actual
PNG/JPEG data through the existing five CPU helpers, checks sampler metadata,
and feeds real decoded alpha into the material resolver. These CPU checks do
not deform GPU vertices or qualify GPU sampling, UV binding or material
appearance. Native rendering requires separate evidence.

The [material fixture contract](MATERIAL_VERIFICATION.md) verifies five decoded
role inputs, staging ownership, scoped diagnostics and the image/handshake
guards. Its adversarial harness checks missing evidence, altered binary hashes,
path escapes, malformed or overflowing numbers, wrong maps and spatial defects
that preserve average color. These tests use synthetic images without a graphics
context; the separate native matrix below supplies the rendering evidence.

The [animation contracts](ANIMATION_VERIFICATION.md) exercise the shared CPU
pose evaluator, root-motion/loop policies, hierarchy transforms, scale
compensation, cubic tangent fidelity, malformed cubic channels, the production
cache sampler codec/version gate and the public rig's palette space. Synthetic image and manifest
checks validate the native capture guards without a graphics context. The
separate native matrix below supplies bounded GPU evidence.

## Native rendering and editor changes

OpenGL, Vulkan and Direct3D 12 are equal supported targets. Renderer or visual
editor work requires all affected surfaces on all three native APIs, including
expected-content checks at matching scene/simulation frames. Startup logs and
source contracts alone cannot prove visual correctness.

Use the consuming project's scene and material fixtures when the change affects
its rendering. Keep game content and capture harnesses in that project. The
current Autochess consumer documents its [parity contract](https://github.com/AdamWentworth/PokemonAutochess/blob/master/docs/RENDERER_PARITY_CONTRACT.md)
and [test plan](https://github.com/AdamWentworth/PokemonAutochess/blob/master/docs/TEST_PLAN.md).
Generic engine defaults also need independent coverage when changed.

- Keep resolution, simulation time, camera, content and presentation settings comparable.
- Rebuild compatible editor/project-plugin pairs when public interfaces change.
- Check models, materials, animation, scene visibility and UI, not only pixel similarity.
- Record backend, source revision and coverage limits. Do not loosen thresholds
  or silently fall back to another API to turn a failed check green.

Hosted CI is a Windows build/CPU gate. Full GPU qualification and performance
measurement remain local; this workflow does not certify other operating systems,
every GPU/driver, all project content or every editor scenario.

## Recorded evidence

On 2026-09-22, M3-F/G added the [runtime audio mixer and SDL output](AUDIO_MIXER.md).
The complete Debug build and all 25 CTest entries passed, including numeric
mixer and SDL dummy-device contracts. No physical output-device check or game
battle cue integration has been recorded. PhlosionVFX's separate effect-host
target and its two-contract Debug suite also passed; its renderer-neutral
lifecycle does not establish native effect presentation.

On 2026-09-22, the M3-E [runtime UI text foundation](UI_TEXT_LAYOUT.md) added
strict UTF-8 string tables and measured glyph placement. Its standalone CPU
contract covers locale-table replacement, fallback metrics, wrapping, scaling,
invalid text and output preservation. The full Debug build and all 23 CTest
entries passed. No native text presentation claim is made.

On 2026-09-22, the M3-D [runtime UI focus foundation](UI_FOCUS_NAVIGATION.md)
passed the full 22-entry Debug CTest suite. After the final neutral-rearm
adjustment, the full build and affected UI contract passed again. The module
routes named keyboard/controller actions through stable focus IDs, repeat,
modal capture and neutral rearm. No UI rendering path changed;
native battle HUD/dialogue images and text qualification remain open.

On 2026-09-22, the M3-C [battle camera foundation](BATTLE_CAMERA.md) passed the
full 21-entry configured Debug CTest suite after a complete build. Its new CPU
contract covers multi-aspect target fitting, impossible shots, moving targets,
interrupted and opposite-side blends, bounded shake that keeps edge targets in
frame, reduced-motion cleanup and owner-thread behavior. No runtime or editor
camera rendering changed, so this is CPU qualification only. Synthetic native
battle-camera presentation and final roster/environment shots remain open.

On 2026-09-22, the M3-B [parameter-driven animation state](ANIMATION_STATE_MACHINE.md)
passed the full 20-entry configured CTest suite (34 C++ contracts), including
typed parameter transitions, event timing, one-shot hold, forced cancellation,
reset and transactional failure checks. The eight-case public animation matrix
passed 48 native runtime/editor captures across OpenGL, Vulkan and Direct3D 12,
with zero failures in 48 expected-content entries, 48 pairwise renderer
comparisons and 60 cross-case checks. The updated negative-evidence harness
passed as part of CTest. The contact sheet, matrix, captures and handshakes are
retained locally under `artifacts/m3-b/native-20260922/`. This qualifies the
synthetic state-driven skeletal path on this workstation; private roster clips,
authored graph/event import and battle systems remain outside this evidence.

On 2026-09-22, the M3-A [animation blend foundation](ANIMATION_BLEND.md)
passed the full 20-entry configured CTest suite, including CPU pose/event
contracts and an analytic blended glTF skin-palette check. The updated
animation fixture's 42 native captures passed across the
standalone runtime and actual editor on OpenGL, Vulkan and Direct3D 12. Its
matrix reports zero failures in 42 expected-content entries, 42 pairwise
renderer comparisons and 54 cross-case checks. The hermetic negative-evidence
harness passed. Captures, handshakes, logs and matrix are retained locally in
`artifacts/m3-a/native-20260922/`. This qualifies the public two-joint blend
and existing skinning path on this workstation; roster characters, authored
state graphs/events and battle integration remain unqualified.

On 2026-09-22, Astra accepted the asset-independent implementation and automated
qualification for [M2-F's public exploration course](agent-work/cyberpunk-m2-exploration/ACCEPTANCE.md)
from M2-E commit `36b6986`. The enabled Debug build passed and all twenty CTest
entries passed in 349.04 seconds. Final native evidence includes 96 exploration
captures at 16:9/21:9, twelve supplemental steep-slope/ledge-drop captures, and
the existing 36-case scene regression, covering both actual hosts and all three
APIs. Both exploration matrices have zero maximum MAE/RMSE/changed-pixel ratio
at unchanged thresholds. Independent state/content/provenance checks and all 27
native evidence mutations passed, including pristine recovery.

Evidence is under `artifacts/m2-f/`: `build-full-03`, `ctest-all-03`, `matrix-04`,
`supplemental-01`, `scene-regression/final-02`, `negative-01` and `compiler/`.
Root reviewed the patch, one consolidated correction cycle, native contact
sheets, a raw editor capture and 936 current file identities. The main plugin
ABI/InputEvent and accepted module algorithms are unchanged. Replay uses owned
virtual devices and explicitly synthetic focus. Subsequent visible D3D12 sessions
exercised basic physical controls with SDL reporting `Xbox 360 Controller`.
Runtime movement/run/camera/focus/pause/reset and editor movement/camera/focus
are corroborated by traces. Manual editor toolbar transitions are deferred by
the user; reported physical unplug/reconnect has no SDL inventory transition
in the trace. Final character/environment fitting remains a separate gate.

On 2026-09-26, the PokemonCyberpunk provisional Outskirt scene passed its
game-owned nine-phase M2 replay: 108 captures across both hosts, 16:9/21:9,
and OpenGL/Vulkan/D3D12. The verifier checked authored collision selection,
recovered ramp support, wall blocking, reset, synthetic focus loss/resume,
virtual-controller reconnect, visible Wes/environment pixels, and state parity.
All 54 runtime/editor viewport pairs were pixel identical; the 72 cross-API
pairs had maximum MAE 0.785/255 and 1.89% pixels changing by more than eight.
Evidence is in `PokemonCyberpunk/artifacts/outskirt-m2-scene/report.json`.
The synthetic transitions do not establish physical device/OS behavior, and
the scene has no qualified visible step case.

On 2026-09-22, Astra accepted [M2-E's exploration camera](agent-work/cyberpunk-m2-camera/ACCEPTANCE.md)
from M2-D commit `bd76391`. The final enabled VS2026 Debug build passed, and all
fifteen CTest entries passed in 333.88 seconds; the camera entry took 0.10 seconds.
Ten groups cover analytic framing/damping and real Jolt obstruction geometry,
plus a repeated input/character/camera sequence. A separate wrong-owner
destruction probe exited through its terminate handler with the expected code 73.
Actual compiler records show `/W4 /WX` for both new targets. The existing
aggregate warning exception and original CMAKE_CXX_FLAGS are unchanged.

Evidence under ignored `artifacts/m2-e/` includes `09-final-build.log`,
`10-all-ctest.log`, `CTest-LastTest.log`, `11-destruction.exit.txt`,
`12-imports.log`, copied compiler/link tlogs, `source-binary-sha256.json` and
`evidence-sha256.json`, with command/exit sidecars and retained negative attempts.
Root reviewed the actual implementation, all test groups and guide, matched
all fifteen recorded source/document/binary hashes and checked the dependency
boundary. The test executable imports only Windows/C++ runtimes. No renderer,
host event path, InputEvent or plugin ABI changed. Mathematical aspect checks
are CPU evidence; native scenes, physical devices and GPU parity remain future
integration work. Radial sphere clearance is not full-frustum/trajectory proof.

On 2026-09-22, Astra accepted [M2-D's input actions and SDL adapter](agent-work/cyberpunk-m2-input/ACCEPTANCE.md)
from M2-C commit `6a72ded`. The full enabled VS2026 Debug build and fourteen
CTest entries passed (299.96 seconds). After discovering the legacy aggregate
test target's warning-policy exception described above, a temporary global
`/WX` qualification rebuilt all 37 compiled targets and passed all fourteen
CTest entries again (355.57 seconds). Compiler records contain 177 commands,
all with `/WX`; only `PhlosionEngineTests` uses `/W1` instead of `/W4`.
The original local `CMAKE_CXX_FLAGS` was restored. A final test-only RAII cleanup
then passed another all-target build and both input executables directly.

Raw commands, logs, exit codes and final hashes remain in `artifacts/m2-d/`:
`full-build.log`, `full-ctest.log`, `global-wx-build.log`, `global-wx-ctest.log`,
`compiler-flags-summary.json`, `all-target-compiler-commands.json`,
`restored-flags.txt`, `review-build.log`, `review-pure-tests.log`,
`review-sdl-tests.log`, `final-pure-imports.log`, `final-sdl-imports.log` and
`verified-file-hashes.json`. Negative development runs are retained separately.
Root reviewed source/tests and verified the eleven recorded source/document/
binary hashes against the final files. SDL reports runtime 2.32.10. The pure
test imports only Windows/C++ runtimes; the adapter test adds SDL2d.dll and no
graphics backend DLL. This is local CPU/virtual-device qualification; physical
controllers, camera movement and runtime/editor input routing remain unverified.
No render behavior changed, so no new GPU qualification is claimed.

On 2026-09-22, Astra accepted [M2-C's discrete trigger sampling](agent-work/cyberpunk-m2-triggers/ACCEPTANCE.md)
from M2-B commit `21672db`. The full enabled VS2026 Debug build with the engine
warning policy enabled passed; the affected targets used `/W4 /WX`. All twelve
CTest entries passed (297.60 seconds). Final test-only
creation/removal and teleport-crossing cases passed the affected rebuild and
trigger CTest (0.09 seconds); all seven groups passed directly. Existing collision
and character test sources were unchanged.

Raw logs are in `artifacts/m2-c/`: `build-all-01.log`, `ctest-all-01.log`,
`build-trigger-final.log`, `ctest-trigger-final.log`, `trigger-groups-final.log`,
`compile-link-settings.log` and `trigger-imports.log`. Root inspected the actual
source, tests, complete metadata/order assertions and publication/ownership
behavior, with no outstanding findings. Imports are only Windows/C++ runtimes.
This qualifies discrete CPU membership samples on the local Windows host;
continuous crossing events, game dispatch and native runtime/editor integration
remain outside this evidence. No rendering changed and no new GPU claim is made.

On 2026-09-22, Astra accepted [M2-B's kinematic character foundation](agent-work/cyberpunk-m2-character/ACCEPTANCE.md)
from M2-A commit `b570f4e`. The full enabled VS2026 Debug build with the engine
warning policy enabled passed; the affected targets used `/W4 /WX`. All eleven
CTest entries passed (312.42 seconds). Final test-only
teleport/filter assertions then passed another all-target build and the affected
character CTest (0.42 seconds); all ten character groups also passed directly.
Existing query tests and their assertions were unchanged.

Evidence under `artifacts/m2-b/` includes `build-all-01.log`, `build-all-02.log`,
`ctest-all-01.log`, `ctest-character-final.log`, `character-groups-final.log`,
`compile-link-settings.log` and `character-imports.log`. Root reviewed the actual
code, tests, retained negative/final evidence and relevant installed Jolt rollback
and slope logic. The standalone executable imports only Windows/C++ runtimes;
Jolt remains pinned at 5.4.0. Qualification is CPU-only on this Windows host;
no renderer or runtime/editor integration changed, and no GPU claim is made.

On 2026-09-20, Astra accepted [M2-A's static collision foundation](agent-work/cyberpunk-m2-collision/ACCEPTANCE.md)
from M1 commit `08f5791`. The full enabled VS2026 Debug build with `/WX` passed,
and all ten CTest entries passed (292.07 seconds). Review then added analytic
world-space checks for a translated/rotated mesh and owner-thread rejection
plus independent threaded-world lifetime checks. The affected `/WX` rebuild
and collision CTest passed again (eight groups, 0.09 seconds); production code
and the other nine test entries were unchanged.

Raw logs are in `artifacts/m2-a/`: `build-all.log`, `ctest-all.log`,
`build-collision-review-1.log` and `ctest-collision-review-1.log`. Actual compiler,
linker, package and DLL-import evidence confirms the independent collision
target and manifest-pinned Jolt 5.4.0#0. The resolved MSVC x64 package uses the
SSE2 baseline, without added AVX/SSE4 requirements. This is a Windows CPU
qualification; no rendering or host integration changed and no new GPU result
is claimed.

On 2026-09-20, Astra accepted the [engine-facing M1 closure](agent-work/cyberpunk-m1-closure/ACCEPTANCE.md)
from `56cdf24`: archive-backed worker glTF sessions, atomic activation and
explicit native resource ownership/reclamation. The full `/WX` Debug build and
nine CTest entries passed (32 CPU contracts, 296.29 seconds). After correcting
the scene guard's mode-specific capture frame, its two affected CTest entries
passed again (88.79 seconds). Logs are in `artifacts/m1-closure/`:
`build-final.log`, `build-guard-final.log`, `ctest-final.log` and
`ctest-guard-final.log`.

The accepted `lifecycle/lifecycle-final` matrix contains 36 native runtime/editor
captures across OpenGL, Vulkan and D3D12, each preceded by 12 unique
load/cancel/retained-draw/release cycles while an independent GPU scope remains
live. At frame 42, unload cases have zero managed objects/bytes/scopes before
host shutdown. Every capture passes the existing expected-content and parity
guards plus strict native lifecycle accounting. All 36 PNGs exactly match the
legacy scene captures, themselves identical to accepted M1-K. The final contact
sheet was reviewed.

Another 114 native captures passed in `regression/scene/scene-01` (36),
`regression/archive/archive-01` (24), `regression/animation/animation-01` (36)
and `regression/material/material-01` (18). `final-audit.json` rehashes all 20
binary identities for these five runs and checks their sizes and 150-capture
total. The acceptance record includes measured bytes/timings and limits:
one Windows/GTX 1070 Debug host, explicit managed cached-resource ownership,
no peak/process-memory or nonblocking streaming claim. Private game assets
remain separate.

On 2026-09-20, Astra accepted M1-K's [owned CPU glTF scene and renderer
adapter](GLTF_SCENE_ASSETS.md), based on M1-J `cc600e1` plus the accepted working
tree. The full Debug build with warnings as errors passed, as did 29 CPU
contracts and all nine CTest entries (284.37 seconds). A separate configuration
built the scene guard/runtime with tests, editor and other fixture options off.
Logs are under `artifacts/m1-k/`: `astra-build-final.log`,
`astra-ctest-accepted.log` and `astra-scene-only-final.log`.

The accepted [integrated scene matrix](SCENE_FIXTURE.md) is
`artifacts/m1-k/matrix/m1-k-accepted-scene/`: 36 captures at frame 6, covering
six lifecycle/content cases across three native APIs and runtime/editor.
The guard checks expected geometry, deformation, replacement preservation,
factor-only emission and background-only unload per API/host. All 60 interior
color-factor patches matched all 25 expected pixels at channel tolerance 3.
The standard parity limits remain MAE .01, RMSE .08, changed ratio .05 and
channel tolerance 8. Astra also inspected the final contact sheet.

Regression runs `regression/archive/m1-k-accepted-archive` (24 captures),
`regression/animation/m1-k-accepted-animation` (36) and
`regression/material/m1-k-accepted-material` (18), relative to
`artifacts/m1-k/`, all passed. `astra-final-audit.json` rehashes and checks sizes
for all 16 recorded binary identities across the four accepted runs, totaling
114 native captures. Earlier runs are historical; these final runs include
the material-reference overflow correction.

The [acceptance record](agent-work/cyberpunk-m1-integration/ACCEPTANCE.md)
documents Astra's review and the user-authorized handoff after Flash's provider
balance error. This qualifies the documented synchronous subset on one
Windows/GTX 1070 host in Debug. It does not qualify asynchronous scene activation,
GPU cache release/residency, general glTF/PBR conformance, production game
adapters or private assets. M1 remains partial; these changes are included in
the commit `Integrate owned glTF scenes with shared rendering`.

On 2026-09-20, Astra accepted M1-J's [five-map material
verification](MATERIAL_VERIFICATION.md), based on committed M1-I `a037f88` plus
the accepted working tree. The only production change routes empty encoded
image data through existing role-specific fallbacks. The permanent five-role
regression fails on the old black normal fallback; base/emissive remain
unchanged, so no legacy Model cache invalidation is required.

The full Debug build with warnings as errors passed, as did 26 CPU contracts and
all eight CTest entries (223.66 seconds). Final logs are
`artifacts/m1-j/overflow-final-build.log` and `overflow-final-ctest.log`.
The final native run `artifacts/m1-j/matrix/m1-j-final3/` contains 18 captures:
three cases x three APIs x runtime/editor, all at 845x532. The 18 cross-API pairs
have worst normalized MAE 0.0001075, RMSE 0.0018735 and changed ratio 0.0012636;
all nine whole-image host pairs are pixel-identical. Limits remain MAE .01,
RMSE .08, changed ratio .05 and channel tolerance 8. Diagnostic interiors have
maximum channel deviation 2 under tolerance 3. Twelve targeted authored-versus-
fallback lit-region comparisons differ throughout the measured region.

Astra reviewed the actual patch, contact sheet and final logs. Review corrections
added owned temporary assets, matched-size host captures, per-pixel content
checks, lit-region comparisons and mandatory bounded evidence. Independent
probes first exposed missing-metrics and numeric-overflow false passes; the
corrected guard rejects them. `astra-final-negatives.log` preserves final root
checks, and `astra-final-audit.json` verifies all 12 final binary hashes/sizes
across the material matrix and retained `archive/m1-j-archive` (24 captures)
and `animation/m1-j-animation` (36 captures). Earlier material runs remain
historical artifacts; `m1-j-final3` is the accepted run.

This qualifies one public layout on Windows/GTX 1070 in Debug: decoded map
identity, channel routing, display treatment and fallback behavior. It does not
qualify general PBR equations, GPU scalar-factor application, independent UVs,
filtering, compressed formats, private characters or production game adapters.
The legacy encoded-byte base-factor and factor-only emission gaps remain.
M1-J is included in the commit `Verify native material maps and fix empty-image
fallbacks`; integration changed documentation only.

On 2026-09-20, Astra accepted M1-I's [glTF texture decoding
verification](GLTF_TEXTURE_VERIFICATION.md), based on committed M1-H `4ab3ac7`
plus tests and documentation. The production loader feeds real encoded PNG/JPEG
fixtures to the five existing CPU decode helpers. Exact PNG pixels, orientation
and alpha are checked across external/data URI and padded bufferView routes,
including GLB; JPEG uses a flat patch with a per-channel tolerance of 3 out of 255
and exact opaque alpha. Coverage includes role selection, wrapping/filter enums,
defaults and factors, real alpha-mode integration, corrupt images, bounded
invalid selections and independent retained outputs.

The final full Debug build with warnings as errors, 25 CPU contracts and all
seven CTest entries passed (`artifacts/m1-i/final-build.log`, `final-tests.log`,
`final-ctest.log`; CTest 86.70 seconds). Astra reviewed the tracked/new source
and one consolidated correction: corrupt payloads are bound to all five roles
and asserted before decoding; the retention case uses the same source image;
empty-byte defect assertions live only in an artifact probe; debug environment
flags are scoped off. Four deliberate mutations fail on pixel, sampler or
corrupt-image binding errors and are reverted. An ambient-debug probe produces
zero contract texture dumps with the guard and 32 with a temporary inverse
mutation. The retained empty-byte patch applies cleanly to the final source.

This is CPU evidence only, with no production or shader changes and no new
native captures. Remaining gaps include encoded-byte base-factor application,
source-derived factor-only emissive loss on the legacy OpenGL Model path,
inconsistent empty-byte placeholders, absent runtime calls to three decode
helpers, per-role UV binding and compressed image formats. World-backend
material behavior requires separate tracing and native qualification; these
tests do not certify five-map rendering or cross-API material parity. This
records the M1-I boundary; M1-I was committed as `a037f88`, and M1-J above
subsequently corrected the empty-byte fallback and added bounded native evidence.

On 2026-09-20, Astra accepted M1-H's [cubic animation
fidelity](ANIMATION_VERIFICATION.md), based on committed M1-G `afaeb1e` plus the
M1-H working tree. The existing importer retains cubic tangent triplets and the
shared sampler evaluates them for T/R/S. Cache v10 invalidates earlier lossy
results through the production identity gate. Exact/clamped keys bypass tangent
arithmetic; malformed cubic samples skip their channel. Legacy LINEAR/STEP
policies and backend/shader code are unchanged.

The final warning-clean Debug build, seven CTest entries and 24 CPU contracts
passed (`artifacts/m1-h/build-full-02.log`, `ctest-03.log`, `tests-03.log`).
Tests include non-unit intervals, multi-segment translation, two-key scale,
quaternion sign/normalization, extreme finite tangents at exact/clamped keys,
non-finite interpolation rejection, import-to-sampling and cache roundtrip.
The CLI harness independently rejects each linearized cubic image and passes
the pristine matrix after restoring mutations (`harness-02.log`).

`artifacts/m1-h/m1h-run02/` contains all 36 native animation captures (six poses,
three APIs, runtime and real editor), zero failures, expected-content checks
and stable palette transitions. Worst cross-API normalized MAE/RMSE are
0.0000074/0.00039 under unchanged limits. The original 24 images match M1-G
pixel for pixel (`compare24-run02/summary.json`); all 36 match the pre-correction
M1-H run (`compare-run02-vs-run01/summary.json`). Astra inspected the patch,
corrections, contact sheet and final binary hashes.

Archive regression evidence is `artifacts/m1-h/archive-regression/m1h-archive-final/`
with 24 passing final-build captures (`archive-final.log`). Astra found that the
older archive guard checks digest format and file size, but does not re-hash
binaries: its earlier PASS did not establish final-build provenance. The older
archive capture was superseded, and Astra independently checked all four hashes
in each final manifest against the actual binaries. Future archive evidence
reuse still requires that explicit hash check.

This qualifies one public two-joint rig on Windows/GTX 1070 Debug. Native cubic
coverage is one two-key Z-rotation curve sampled twice; broader T/R/S cases rely
on CPU contracts. Full Model cache integration, legacy Model GPU drawing,
private characters, morphs, blending, instanced/indirect skinning, materials and
performance remain outside this evidence. M1-H was committed as `4ab3ac7`.

On 2026-09-20, Astra accepted M1-G's [pose-sampling and skeletal
verification](ANIMATION_VERIFICATION.md), based on `9c0d812` plus the M1-G
working tree, subsequently committed as `afaeb1e`. Model delegates to its existing CPU evaluator through
ModelPoseSampler; the extraction preserves the inspected algorithm and public
behavior. No backend, shader, cache schema or editor-host code changed.

The final warning-clean VS2026 Debug build, seven CTest entries and all 23 C++
contracts passed: ignored `artifacts/m1-g/build-full-05.log`, `ctest-05.log` and
`tests-fix-03.log`. The new harness rejects 24 negative evidence/content cases
(`harness-fix-02.log`). Child-project checks confirm fixture hosts default off
and the shared sampler works without a graphics context. The retained extraction
comparison checks 183 normalized statements; Astra also reviewed the delegated
helpers and source-array wiring.

The final `artifacts/m1-g/m1g-run04/` records all 24 native animation captures:
four poses on OpenGL/Vulkan/Direct3D 12 in the standalone host and actual editor
plugin. Expected pixel content checks cover the anchored base, moved tip and
blended middle, independently of sampled pose results. Both hosts update one
palette allocation across the bind-to-posed transition; all recorded addresses
and changed-content checks agree. Worst cross-API normalized MAE/RMSE are
0.0000038/0.00029 under unchanged 0.01/0.08/0.05 limits and channel tolerance 8.
Final images match the previous corrected run pixel for pixel. Astra inspected
the native images and rechecked all four final binary hashes.

The archive regression captured 24 passing cases under
`artifacts/m1-g/archive-regression/m1g-archive-02/`; its binaries stayed unchanged
during review corrections, so the retained matrix was revalidated rather than
recaptured (`archive-revalidation.log`). That rigid fixture does not test poses.
Animation evidence is one public two-joint rig on Windows/GTX 1070 Debug, through
the world-mesh-textured path. It excludes private characters, the legacy
OpenGL-only Model draw path, instanced/indirect skinning, cubic interpolation,
morphs, production materials and performance budgets.

On 2026-09-20, Astra accepted M1-F's [existing glTF import
verification](GLTF_IMPORT_VERIFICATION.md), committed as `9c0d812` from baseline
`9d094c9`. Three new CPU contracts exercise real file loading and imported
scene/skeleton/clip values, material metadata and temporary-fixture ownership.
Production sources are unchanged. Review corrections made the synthetic rig
structurally coherent and isolated every test-owned temporary directory.

The final VS2026 Debug build with warnings as errors, all six CTest entries and
all twenty-one C++ contracts passed. Evidence is retained locally in ignored
`artifacts/m1-f/build-full-08.log`, `ctest-all-04.log` and
`tests-from-ctest-dir-04.log`. The helper-only two-process probe also passed
(`helper-probe-concurrent-02.log`); deliberate regressions failed before being
reverted (`regression-probe-0{1..5}.log`). No GPU run was needed for this
tests/documentation-only pass, and none is claimed. Pose sampling, skeletal
deformation, texture decoding and material appearance remain outside these
original contracts. CUBICSPLINE tangent loss was an audited limitation closed
by M1-H above; absent morph-weight channels remain unsupported.

On 2026-09-20, Astra accepted M1-E's [typed static mesh resource](MESH_ASSETS.md)
after one consolidated review-fix cycle. The reusable contract lives in
engine_core; the example consumes six required PHMS resources from its staged
dependency graph. It preserves submesh ranges, slot bindings and tangents without
changing renderer or editor-host APIs. Existing Model/glTF/cache paths remain
compatible. M1-E was committed as `9d094c9`, based on
`68aed76373c169494d9238d267cd0e50cd885ff2`.

The final VS2026 Debug build with warnings as errors and all six CTest entries
passed (eighteen C++ contracts). A child project linking only Phlosion::Core
encoded/decoded a mesh without graphics dependencies. Logs are in ignored
`artifacts/m1-e/build-full-03.log`, `ctest-03.log` and
`link-proof-{build,run}-02.log`. Regressions cover malformed UTF-8, fixed binary
bytes, a valid two-slot consumer and missing/duplicate bindings; deliberate
binding/tangent regressions failed their checks before being reverted.

The final `artifacts/m1-e/m1e-run03/{manifest,matrix}.json` and `guard.log`
record all 24 native runtime/editor cases, unchanged thresholds, zero matrix
failures and matching final binary hashes. Every capture is pixel-identical to
the accepted M1-D `reorg-run02` counterpart; comparisons are retained in
`artifacts/m1-e/m1e-run03-vs-m1d/`. Worst cross-API normalized MAE/RMSE remain
0.0000052/0.00024. Failed replacement preserves A30 exactly. This is one
Windows/GTX 1070 Debug host, not qualification of private game assets, skeletal
animation, production materials, other hardware or performance budgets. Tangent
forwarding has CPU evidence; the fixture materials do not visibly consume it.

On 2026-09-20, M1-D added the [public asset fixture](PUBLIC_ASSET_FIXTURE.md):
shared archive-driven geometry/texture presentation through a standalone host and
the actual editor project-plugin ABI. Four activation/replacement cases passed
on native OpenGL, Vulkan and Direct3D 12 in both hosts (24 captures). Expected
content checks cover checker orientation, depth occlusion, blending, rigid marker
position and changed GPU cache identities. Failed replacement is pixel-identical
to A30; cross-API worst normalized MAE/RMSE are 0.0000052/0.00024 under unchanged
0.01/0.08/0.05 parity limits and channel tolerance 8. Host frames 6/8 and logical
frames 0/30 are recorded separately; backend/frame/log/metric checks reject
fallbacks and incomplete or duplicated coverage.

The final VS2026 Debug build with warnings as errors passed, as did six CTest
entries (17 C++ contracts plus the CPU harness). Fixture hosts disabled with
tests enabled also passed five CPU entries, and a child-project configure
confirmed fixture hosts default off. Native captures and original binary
provenance are in ignored `artifacts/m1-d/fix-run03/`. Astra restored the original
board-coverage minimums (0.4 interior, 0.35 edge) during review, rebuilt and reran
CTest, then revalidated all retained captures with that stricter guard:
`artifacts/m1-d/astra-{build,ctest,matrix}.log`, `astra-matrix.json` and
`astra-manifest.json`. No additional renderer run was needed for a guard-only
change. Evidence is one Windows/NVIDIA GTX 1070 host; it does not qualify private
game content, skeletal animation, production material pipelines or budgets.

Before committing M1-D, the sample and its verification machinery were separated:
examples/public_asset_fixture holds the runnable sample, tools/public_asset_fixture
holds guards/capture tools, and tests/public_asset_fixture holds the PowerShell
harness. Rebuild and six CTest entries passed after relocation. The moved runner
captured all 24 native cases again with identical reported metrics and unchanged
guards: `artifacts/m1-d/reorg-run02/`, `reorg-build-final.log` and
`reorg-ctest-final.log`. Implementations were checked against pre-move hashes;
the sample no longer links the verification library.

On 2026-09-20, the M1-C staged archive activation iteration added
`PhlosionArchiveSession` with candidate staging over the accepted graph loader,
the existing typed archive decoders and explicit activation. The active
snapshot is preserved on candidate graph/decode failure, cancellation and
discard; `unload` clears the session's active reference,
and snapshots already handed to callers stay readable afterwards, as does a
retained snapshot after the provider itself is released. The existing
`build-presentation` Debug tree (VS2026, editor/tests enabled, engine warnings
treated as errors) rebuilt cleanly, and the full CTest set passed (5 of 5
entries, including all sixteen C++ contracts). Logs are local under
`artifacts/m1-c/`. This iteration is pure CPU and produces no rendering or
visual-parity evidence.

On 2026-09-20, the M1-B dependency-aware asset iteration added
`PhlosionAssetGraphLoader` with its required-closure traversal, hash
constraints, cycle checks, bounded asset/edge/byte limits, cancellation and
reset lifecycle, and the dependency-only prefab CPU fixture. The existing
`build-presentation` Debug tree (VS2026, editor/tests enabled, engine warnings
treated as errors) rebuilt cleanly, and the full CTest set passed (5 of 5
entries, including all fifteen C++ contracts). Logs are local under
`artifacts/m1-b/`. This iteration is pure CPU and produces no rendering or
visual-parity evidence.

On 2026-09-20, the M1-A asynchronous asset iteration added
`AsyncAssetLoader` with its explicit `requestStop()` lifecycle boundary, the two
CPU contracts above and their documentation. The existing `build-presentation`
Debug tree (VS2026, editor/tests enabled, engine warnings treated as errors) rebuilt cleanly
with the new `engine_core` source and standard thread linkage, and the full CTest
set passed (5 of 5 entries, including all fourteen C++ contracts). Logs are local
under `artifacts/m1-a/`. This iteration is pure CPU and produces no rendering or
visual-parity evidence.

On 2026-09-19, the presentation review built a separate `build-presentation`
Debug tree with MSVC, editor/tests enabled and warnings treated as errors.
All five CTest entries passed, including all twelve C++ contracts and the
semantic boundary. Logs are local under `artifacts/presentation-review/`.
This review changes documentation, media and CI rather than runtime behavior.

The material extraction at engine commit `4e555f2` has separate
[consumer verification](https://github.com/AdamWentworth/PokemonAutochess/blob/master/docs/CHARACTER_MATERIALS.md#boundary-verification-2026-09-19):
listed editor and native game cases across three APIs, default-engine checks,
CPU contracts and scoped performance measurements. Consult that dated record
for its exact limits. Subsequent consumer-specific qualification failures belong
in the [game's issue register](https://github.com/AdamWentworth/PokemonAutochess/blob/master/docs/OUTSTANDING_ISSUES.md).
