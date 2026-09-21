# Cyberpunk engine readiness audit

Status: Reviewed roadmap; runtime readiness remains unverified
Date: 2026-09-20
Owner: Astra (scope, architecture, acceptance)
Audited engine baseline: `c17d2122bd36b3c3bb49c4e703900f40cccfbd76` (`main`)

Documentation-only audit of Phlosion Engine against the recovered Pokemon
Cyberpunk direction. It records what the engine source and tests contain today,
what is missing, a dependency-ordered roadmap, and a migration boundary. It
starts no engine work and migrates nothing.

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

The distinction matters for the remaining material work: the decoder currently
bakes base-color factors into encoded bytes, factor-only emission is lost on
the legacy OpenGL Model path according to its source, and empty image bytes
produce inconsistent placeholders (retained as an artifact probe). Model's
runtime loader calls only the base-color and emissive decode helpers; passing
normal/metallic-roughness/occlusion CPU tests does not qualify those maps in a
rendered character. Independent per-role UVs/transforms, compressed texture
decoding and native material appearance remain unverified. A following material
pass must establish the intended shared runtime path and qualify any rendering
changes on all three APIs, including the editor preview.

This is partial M1 completion. Next: qualify the actual production
character/content paths and continue material/texture/environment qualification.
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
dependency-resolution/public-fixture/typed-storage/import/pose/texture-verification gaps and twelve-contract count
for the local implementation.

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
| Physics/collision, character controller, animation runtime, camera rigs, action input, runtime UI and audio were missing | Confirmed by this audit: absent or partial, see the matrix and milestone list | These remain the freeze blockers; nothing in the completed cleanup work addressed them |

## Capability matrix

Each row states what the engine has now, whether any test was identified, and
the gap for the Outskirt Stand slice. Rows that name a test are covered by it;
rows marked "no engine test identified" are source-inspected only, and "where
it is proven" means consumer usage, not engine verification.

### Platform, input and camera

| Capability | Status | Evidence | Gap for this work |
| --- | --- | --- | --- |
| Window, drawable size, VSync, swap | Implemented | [Window.h](../src/engine/platform/Window.h); no engine test identified | Fullscreen/borderless policy and display-mode enumeration are consumer-side |
| Keyboard and mouse input | Implemented | [InputEvent.h](../src/engine/input/InputEvent.h), [SdlKeyMap.h](../src/engine/input/SdlKeyMap.h), event pump in [Application.cpp](../src/engine/runtime/Application.cpp); no engine test identified | Raw keys and buttons; no text-input or IME events |
| Gamepad / controller | Not found in inspected scope | Search of engine source, assets, tests and build manifests for `gamecontroller`, `joystick`, `sdl_haptic`, `controllerrumble`: no matches | The slice explicitly requires controller hotplug, mapping, deadzones and appropriate prompts |
| Named action mapping, rebinding, context layers | Not found in inspected scope | No action-map type in engine source | Gameplay currently reads raw keys, which the recovered roadmap also listed as a gap |
| Camera | Partial | [Camera3D.h](../src/engine/render/Camera3D.h); contract in tests/TestCamera3D.cpp | One camera type; no rigs, target framing, blends, cuts or shake, which the battle and exploration cameras need |
| Cursor capture and pointer policy | Unverified | No distinct contract found | Needed for exploration and battle input modes |

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
| Physics/collision layer | Not found in inspected scope | Engine search for `physics`, `rigidbody`, `collision`, `raycast`, `jolt`, `bullet`, `physx`: one comment in [PhlosionAuthoredScene.h](../src/engine/assets/phlosion/PhlosionAuthoredScene.h) line 56 | No collision world, layers, triggers, raycasts, sweeps or mesh colliders |
| Character controller | Not found in inspected scope | Same search | Walk/run, gravity, grounding, slope and step handling do not exist; the slice requires them |
| Traversal (jumping, ledges, ramps) | Unverified | Autochess has game-owned traversal animation code; no engine traversal contract found | Needs a real controller policy before the slice |

The recovered material's guidance matches the evidence: this needs excellent
kinematic character/world collision, not a full rigid-body stack. Integrating a
mature library (Jolt was named as a suggestion, not an accepted decision) is a
valid alternative to building one; vendor selection is out of scope here.
Destructible or vehicle physics stays excluded.

### Animation

| Capability | Status | Evidence | Gap for this work |
| --- | --- | --- | --- |
| Skeletal animation sampling | Implemented; bounded verification | [ModelPoseSampler.cpp](../src/engine/render/ModelPoseSampler.cpp), [animation verification](ANIMATION_VERIFICATION.md); M1-G/H CPU contracts cover LINEAR/STEP/CUBICSPLINE T/R/S, hierarchy and loop/root-motion policies; native public-rig runtime/editor captures cover all three APIs | Per-instance CPU sampling; no blending graph or state machine. Private rigs and the legacy Model GPU draw path remain unqualified |
| Blending, crossfade, layers, parameters | Not found in inspected scope | No animator type in engine source | Battle and exploration need it; completed by M3. The recovered roadmap treated it as a freeze blocker |
| Animation events and notifies | Not found in inspected scope | No event hook in the animation API | Needed for footsteps, hit reactions and state transitions |
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
| Particles and VFX host | Partial | [ParticleSystem.h](../src/engine/vfx/ParticleSystem.h), [vfx_preview/](../src/engine/tools/vfx_preview); reusable effects in PhlosionVFX | CPU particle simulation at current consumer scale; rain, splashes and attack effects are unqualified |
| Post-processing and anti-aliasing | Partial | Tone mapping and exposure exist as shader constants (see above); [assets/shaders](../assets/shaders) contain no post-processing chain and no anti-aliasing | Missing: a configurable environment/post pipeline, render scale and AA. The Autochess roadmap lists AA and render scale as core roadmap items |
| High-consumer qualification limits | Known, game-owned | [Autochess outstanding issues](../../../Games/PokemonAutochess/docs/OUTSTANDING_ISSUES.md) (`combat-target-focus` appearance guard, private content requirement, no controlled GPU CI gate), [tech debt](../../../Games/PokemonAutochess/docs/TECH_DEBT.md) (perf gate, preview smoke) | GPU qualification is local and manual today; expect the same for Cyberpunk until a runner exists |

### Audio

| Capability | Status | Evidence | Gap for this work |
| --- | --- | --- | --- |
| Audio playback | Not found in inspected scope | Word-bounded engine search for `audio`, `sound`, `mixer`, `miniaudio`, `openal`, `fmod`, `wav`, `ogg`, `SDL_mixer`, `SDL_Audio` matched only `fmod` math calls; not in [vcpkg.json](../vcpkg.json) | No device, mixer, bus, streaming, looping, fades or spatial audio; the slice's step 4 requires audio |
| Audio settings surface | Planned-only | Volume and mute fields in [EngineServices.h](../src/engine/core/EngineServices.h) (lines 75-79) | Fields exist without a backend, and the consumer only stores preferences |

### Player UI, text, dialogue, accessibility and localization

| Capability | Status | Evidence | Gap for this work |
| --- | --- | --- | --- |
| Text rendering | Partial | [TextRenderer.h](../src/engine/ui/TextRenderer.h): SDL_ttf glyph cache keyed by `unsigned char` | Single-byte glyph path: no UTF-8 decoding, fallback fonts, wrapping or shaping; dialogue needs robust text layout |
| Immediate UI primitives | Partial | [UIManager.h](../src/engine/ui/UIManager.h) (`drawCard` marked stub), [Rect.h](../src/engine/ui/Rect.h) | No layout, widget set, focus/controller navigation or input routing; the slice needs dialogue and battle HUD |
| In-editor UI | Implemented | Dear ImGui docking in [EditorShell.h](../src/engine/editor/EditorShell.h), per [EDITOR_ARCHITECTURE.md](EDITOR_ARCHITECTURE.md) | Editor-only, not a shipping game UI stack |
| Dialogue presentation hooks | Not found in inspected scope | No dialogue, typewriter or portrait API in engine source | Kept game-owned by the recovered plan; engine needs reliable text layout and UI input to support it |
| Accessibility (text scale, contrast, motion) | Planned-only | Listed as future menu items in the [Autochess graphics roadmap](../../../Games/PokemonAutochess/docs/DISPLAY_GRAPHICS_ROADMAP.md) | Not found in engine source |
| Localization | Not found in inspected scope | Search for `localiz`, `locale`, `translat`, `utf8`, `unicode`: one asset-path comment in [AssetPackFormat.h](../src/engine/core/AssetPackFormat.h) | No string table, locale switching or font coverage |

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
