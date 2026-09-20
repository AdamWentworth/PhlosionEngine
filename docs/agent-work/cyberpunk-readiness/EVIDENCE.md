# Cyberpunk readiness audit: evidence log

Date: 2026-09-20
Engine baseline: `c17d2122bd36b3c3bb49c4e703900f40cccfbd76` (`main`)
Worker scope: `docs/CYBERPUNK_ENGINE_READINESS.md`, this file, and one link row
in `docs/README.md`. No code, build, config, asset or Git state was changed.

This is a Markdown audit log recording what was actually inspected and which
commands were run. The audit document states conclusions; this file states scope
and results so the negative findings can be re-checked.

## Workspace state at start

```
git status --short   ->  ?? docs/agent-work/
git log --oneline -3 ->  c17d212 docs: adopt Apache 2.0 license (#1)
                         411c8a4 License original engine code under MIT
                         0f9dc5a Use a complete pinned vcpkg checkout in engine CI
```

The only pre-existing change was Astra's untracked
`docs/agent-work/cyberpunk-readiness/PLAN.md`, which was read and not modified.

## Commands run

All commands were read-only and ran from `D:/Projects/Phlosion/PhlosionEngine`
unless a path argument says otherwise. Exit statuses are recorded as returned by
the shell (`0` = matches found or command succeeded, `1` = no matches for `rg`).

| Command (abridged) | Exit | Salient result |
| --- | --- | --- |
| `rg -in 'physics\|rigidbody\|collision\|raycast\|jolt\|bullet\|physx' src tests CMakeLists.txt vcpkg.json` | 0 | One comment: `PhlosionAuthoredScene.h:56` about tile seams keeping collision/navigation from drifting. No physics implementation. |
| `rg -in -w '\b(audio\|sound\|mixer\|miniaudio\|openal\|fmod\|wav\|ogg\|speaker\|SDL_mixer\|SDL_Audio)\b' src tests CMakeLists.txt vcpkg.json` | 0 | Only `std::fmod` math calls (`ModelAnimation.cpp:69`, `D3D12RenderBackendWorldPipeline.cpp:123`). No audio subsystem, no audio dependency. |
| `rg -in -w 'navmesh\|navigation\|pathfind\|astar\|recast\|detour\|steering\|behaviortree' src tests CMakeLists.txt vcpkg.json` | 0 | Comments only: `PhlosionAuthoredScene.h:56` and a routing-test string in `tests/TestEditorGamePreviewRouting.cpp:59`. |
| `rg -in -w 'savegame\|SaveData\|userdata\|profilesave' src tests` | 0 | Only Dear ImGui `UserData` calls in `EditorShell.cpp`. No engine save-game API. |
| `rg -in 'gamecontroller\|joystick\|sdl_haptic\|controllerrumble' src assets tests CMakeLists.txt vcpkg.json` | 1 | No matches anywhere in the engine. No gamepad support. |
| `rg -in -w 'localiz\|locale\|translat\|utf8\|utf-8\|unicode' src docs/README.md` | 0 | One comment: `AssetPackFormat.h:18` notes UTF-8 paths. No localization layer. |
| `rg -n -w 'lua\|sol2\|sol::state\|lua_State' src` | 1 | No engine source uses Lua or sol2, although both are linked in `CMakeLists.txt` and declared in `vcpkg.json`. |
| `rg -n 'ofstream\|std::thread\|std::async\|async\(' src/engine` | 0 | Logging/editor writes, model and PMREM caches, and `std::async` texture decode at `D3D12RenderBackendTextures.cpp:401`. No job system or worker pool. |
| `rg -in 'shadow\|point light\|spot light\|directional\|tonemap\|hdr\|bloom\|msaa\|skybox\|ibl\|pmrem\|frustum\|occlusion\|cull\|lod\|terrain' src/engine/render src/engine/core` | 0 | Cull-mode raster state, PMREM/material carriers and `projectedShadow*` fields in `RenderBackendTypes.h` (game-supplied texture, matrix, sampling scale). No dynamic lights, shadow pass, fog, exposure or post chain. |
| `rg -n 'audioMasterVolume\|audioMute' src/engine/core/EngineServices.h` | 0 | Volume/mute fields at lines 75-79 with no backend. |
| `rg -n 'getOrCreateGlyph\|unsigned char c' src/engine/ui/TextRenderer.h` | 0 | `getOrCreateGlyph(unsigned char c)` at line 41: single-byte glyph path. |
| `rg -n 'drawCard\|stub' src/engine/ui/UIManager.h` | 0 | `drawCard` documented as a stub at lines 18-19. |
| `rg -n 'std::async' src/engine/render/d3d12/D3D12RenderBackendTextures.cpp` | 0 | Line 401: texture decode futures. |
| `rg -n 'kModelCacheVersion' src/engine/render/ModelCache.cpp` | 0 | Cache format version 9 at line 80; cache keyed by source size/mtime. |
| `rg -n 'files=\{\|std::map' src/engine/assets/phlosion/PhlosionSceneArchive.h` | 0 | Scene and prefab archives hold `std::map<std::string, std::vector<std::uint8_t>>` (lines 74, 120): whole-archive in-memory loads. |
| `Get-Content` on engine headers (ECS `World`/`Scheduler`, `FixedStep`, `GameLoop`, `EngineServices`, `Paths`, `Window`, `InputEvent`, `SdlKeyMap`, `Model`, `ModelAnimationTypes`, `ResourceManager`, `ParticleSystem`, `RendererParityContract`, `ProjectDescriptor`, `EditorGameplayReload`, `PhlosionAuthoredScene`, `PhlosionSceneArchive`, `IAssetStore`) | 0 | Source of the matrix rows. |
| `Get-Content` on `VERIFICATION.md`, `EXTRACTION_STATUS.md`, `ENGINE_BOUNDARIES.md`, `README.md`, `CMakeLists.txt` (targets and CTest list), `vcpkg.json` | 0 | Build/test/release context and the completed material and boundary extraction. |
| `rg --files` / `Get-ChildItem` on `src`, `tests`, `docs`, `assets/shaders` | 0 | Inventory used for the matrix and the "not found in inspected scope" claims. |

## Consumer and sibling evidence (read-only)

| Source | Finding used |
| --- | --- |
| PokemonAutochess `docs/DISPLAY_GRAPHICS_ROADMAP.md` | Renderer backend, VSync, resolution and GPU preference are implemented; FPS cap, UI scale and quality preset are placeholder; AA, render scale and dynamic resolution are roadmap items; ray tracing/DLSS/frame generation are deliberately deferred. |
| PokemonAutochess `docs/OUTSTANDING_ISSUES.md` | `combat-target-focus` appearance guard failure on a baseline, private package requirement for editor configuration, private content requirement for full demo/GPU checks, no controlled GPU performance gate in CI, no public release checkpoint. |
| PokemonAutochess `docs/TECH_DEBT.md` | No merge-blocking perf gate; shared projected render/build CPU needs fresh measurement; preview and runtime smoke remain partly manual. |
| PokemonAutochess `docs/RENDERER_PARITY_CONTRACT.md` and `docs/TEST_PLAN.md` | Three-API parity contract values and the local qualification protocol; evidence that GPU qualification is local. |
| PokemonAutochess `src` targeted searches | Audio preference setters exist in game-side Lua bindings but no audio device/playback code anywhere; save-related files are editor persistence only; no gamepad usage. |
| PhlosionPackages `docs/PACKAGE_ARCHITECTURE.md` | Package ownership, `phlosion.package.json` identity/version rules, and the single existing `phlosion.tile-tools`. |
| `D:/Projects/Phlosion/PhlosionVFX` listing | Separate repository with docs, tests and a preview application; reusable effect ownership. |
| PokemonCyberpunkUnreal `Source` listing | One native module (`PokemonCyberpunk.Build.cs`, `.cpp`, `.h`) plus target files. Binary `Content/` was not inspected. |

## Recovery context (context only, not code truth)

The recovered shared conversation is "Engine Gap Analysis":
<https://chatgpt.com/share/6aaee44c-a1a0-83e8-8b4c-e827f3140144>, supplied by
Astra. It was read through a read-only local extraction cached at
`C:/Users/adam/AppData/Local/Temp/phlosion-cyberpunk-shared-messages.json`
(360 KB, 906 entries, 16 user messages, duplicated); that temp file is ephemeral
and is not a durable reference, so the share link is cited in the audit. The
audit references:

- user messages at indices 24, 28, 136, 146, 172, 449 (Unreal/agent workflow,
  Quilava/Typhlosion inclusion, glTF fire fidelity, engine-vs-Unreal decision,
  effort/model advice, and the "what is missing from my engine" question);
- assistant messages at indices 378 (earlier engine roadmap), 173 (source
  audit), 147 (API freeze advice), 137 (Unreal decision), 25 (test roster).

These messages are planning context. They were not treated as evidence that code
exists, and their claims were re-checked against the repository, as recorded in
the reconciliation table of the audit document.

## Correction cycle 1 evidence (Astra review)

Requested corrections were verified against the repository before editing. New
evidence gathered in this cycle:

| Command / read | Result |
| --- | --- |
| `Get-Content -Raw tests/TestMain.cpp` | Twelve named contracts registered: world material profile, resource container, authored scene, environment patch, scene archive, project descriptor, editor renderer preference, camera view-plane pan, editor project plugin, editor game preview routing, editor gameplay reload, editor performance. Confirms no independent ECS, input, window, animation, audio, physics or streaming test. |
| `Get-Content docs/EDITOR_PERFORMANCE.md` (automation section) | Documents `--hidden`, `--state-directory`, `--fixed-delta`, `--frames`, `--metrics-output`, `--open-scene-at=FRAME:SCENE_ID`, `--asset-preview*`, deterministic `--scene-camera-position/-target`, and backend capture via `PHLOSION_BACKEND_SCREENSHOT_PATH` / `PHLOSION_BACKEND_SCREENSHOT_FRAME`. Invalid scene IDs and load failures fail the run. |
| `Get-Content docs/EDITOR_PROJECT_EXTENSIONS.md` (Rule / ownership / capability model) | The engine owns generic selection, transform gizmos, live preview, commit/cancel and undo/redo routing, plus a declarative project-command renderer; the game owns transform capabilities and command execution. |
| `Get-Content docs/EDITOR_ARCHITECTURE.md` (M1 "Safe scene editing") | M1 is active: stable selections, editor documents, typed commands, transactions, undo/redo, transform gizmos and saveable project-owned overrides; canonical cooked scenes stay read-only. M4 describes the future local agent using the same command bus. |
| `rg -n 'commit\|cancel\|undo\|redo' src/engine/editor/EditorShell.cpp` and `.h` | `Undo Scene Edit` / `Redo Scene Edit` menu gated by `canUndoSceneEdit` / `canRedoSceneEdit` (lines 1483-1494), `Release a transform drag to save; Esc cancels it.` (line 1686), project-command help text (line 2975), and `undoSceneEditRequested` / `redoSceneEditRequested` in the shell header. |
| `rg -n 'struct EditorProjectCommand\|EditorProjectLayoutObject\|commandId' src/engine/editor/EditorProjectPlugin.h` | `EditorProjectLayoutObject` (line 180), `EditorProjectLayoutObjectCommand` (line 244), command field/value/result types (lines 254-290), layout-object enumeration (lines 523-526), and project command dispatch (lines 678-689). |
| `Get-Content D:/Projects/Games/PokemonAutochess/CMakeLists.txt` (lines 188-290) | Exact `PHLOSION_ENGINE_GIT_TAG` commit pin (line 197) consumed via `add_subdirectory` or `FetchContent` (lines 250-266), plus exact pins for PhlosionPackages (line 281) and PhlosionVFX (line 341). Installed SDK is therefore not a prerequisite for pinning. |
| `rg -n 'tonemap\|toneMap\|exposure\|Exposure' assets/shaders/...` | ACES-style tone mapping at fixed exposure: `vulkan/world.frag` lines 220-221 (1.15), `vulkan/world_material.glsl` lines 67-76, `model/model.frag` line 20 (`kSceneExposure = 0.85`) and lines 60-63, plus `lib/tonemap.glsl`. |
| `rg -n 'SDLK_ESCAPE\|frameDt = std::min\|camera = Camera3D\|SDL_WINDOWEVENT_SIZE_CHANGED' src/engine/runtime/Application.cpp` | `frameDt = std::min(frameDt, 0.25)` at line 339 (about 15 fixed updates maximum per frame); Escape quits at line 303; window resize calls `updateDrawableSizeAndViewport()` at line 310 and rebuilds the camera at line 317. |
| `rg -n 'projectedShadow\|struct WorldTextureData' src/engine/render/RenderBackendTypes.h` | Projected-shadow carrier is real: RGBA texture, matrix, wrap, sRGB flag, enable flag, sampling scale and bias (lines 168-180), consumed by the world pipelines. |

Corrections applied to the audit document in this cycle:

1. Evidence classes now separate implementation from verification, with the
   twelve named contracts and four source checks listed and unnamed coverage
   called out as absent.
2. Editor automation and capture are credited as implemented, with typed
   commands and undo routing credited separately; the remaining gap is a
   general external query/edit transport and generic component coverage.
3. Version pinning is Partial with consumer `CMakeLists.txt` evidence instead of
   "not found"; an installed SDK is optional.
4. Tone mapping/exposure constants, the projected-shadow carrier and the
   implemented material profile extension are credited; wet/cyberpunk content
   stays Unverified and the missing piece is a configurable environment/post
   pipeline.
5. The runtime loop row now records the 0.25 s clamp, the hardcoded Escape quit
   and the camera reset on resize, with verification needed for pause/menu and
   camera continuity.
6. Streaming language no longer assumes megacity scope; cell/HLOD streaming is
   conditional on a measured region-size decision, async asset work is assigned
   to M1/M4/M5, and navigation has an explicit collision-aware route/pursuit
   gate with navmesh optional.
7. M1 separates public synthetic engine fixtures from game-owned private roster
   qualification, M3 owns blending/events/attachments, and the character
   section records that the user added only Quilava and Typhlosion while
   Autochess already covers some adjacent cases.
8. Exit gates now cover collision edge cases, controller hotplug and input
   focus/resume, timed events and cleanup, corrupt/interrupted saves with
   migration, scene unload lifetime, transition soak, p95/p99 frames and VRAM,
   plus simulation-replay golden results and Unreal adapter mapping.
9. The shared conversation URL is cited in both documents, the EVIDENCE file is
   described as a Markdown audit log, "Latest steering" is replaced with an
   explicit audit position, and open questions are limited to hardware, art and
   unresolved design intent.

## Verification performed on this deliverable

| Check | Result |
| --- | --- |
| Markdown link check over `docs/CYBERPUNK_ENGINE_READINESS.md` and this file | All relative link targets resolved (see command below). |
| `git diff --check` | No whitespace errors. |
| Scope inspection (`git status --short`) | Only the three assigned paths changed: new readiness document, this evidence file, one added link row in `docs/README.md`. |

Link check command:

```powershell
$files = 'docs/CYBERPUNK_ENGINE_READINESS.md','docs/agent-work/cyberpunk-readiness/EVIDENCE.md'
foreach ($f in $files) {
  $dir = Split-Path -Parent $f
  $text = Get-Content -Raw $f
  [regex]::Matches($text, '\]\(([^)#]+)') | ForEach-Object {
    $target = $_.Groups[1].Value
    if ($target -match '^https?://') { return }
    $path = Join-Path $dir $target
    if (-not (Test-Path $path)) { Write-Output ("MISSING " + $f + " -> " + $target) }
  }
}
```

## Limitations and unverified coverage

- No build, CTest run, editor launch, gameplay run or GPU capture was performed;
  this deliverable is documentation only, as the accepted contract states.
- Dated engine and Autochess records describe earlier Autochess work. They are
  not evidence for this design and were not rerun.
- "Not found in inspected scope" claims are limited to the search scopes above.
  Unreal binary content, private depots and generated trees were not inspected.
- The recovered conversation is a reconstruction produced from shared HTML and
  contains duplicated and tool-heavy content; only the referenced messages were
  read, and they are treated as context rather than specification.
- The roadmap reconciles previously listed gaps against the inspected source.
  Hardware budgets and final art/presentation choices remain product decisions.

Checkpoint: Astra accepted the documentation after one correction cycle and
final wording/acceptance-gate edits. This accepts the audit, not runtime readiness.
