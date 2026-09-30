# Public asset fixture (M1-D)

Status: Implemented fixture, capture matrix and adversarial harness
Last updated: 2026-09-20

## Purpose

`examples/public_asset_fixture/` is a distributable, license-free fixture that
proves an archive-session consumer can present loaded content through OpenGL,
Vulkan and Direct3D 12, both in a standalone host and in the real Phlosion
Editor scene preview. It composes the accepted
[staged archive session](ARCHIVE_ACTIVATION.md) with the existing public
renderer draw APIs; no production renderer, editor or core API changes are
involved.

The fixture renders decoded archive content. It has no hardcoded scene
fallback: geometry comes from typed `.phmesh` resources that the scene requires
as dependency edges, materials and textures come from the same activated
snapshot, and a candidate is validated before activation. See
[typed static mesh assets](MESH_ASSETS.md) for the engine contract.

## Build option

`PHLOSION_BUILD_PUBLIC_ASSET_FIXTURE` controls the fixture hosts:

| Consumer | Default | Result |
| --- | --- | --- |
| Standalone engine build (`cmake -S .`) | `ON` | Example library, verification support, `PublicAssetFixtureRuntime`, `PublicAssetFixtureGuard` and (with the editor) `PublicAssetFixtureEditorPlugin` |
| Consumed with `add_subdirectory` | `OFF` | No fixture hosts, plugin or guard; the example and verification libraries are built only when `PHLOSION_BUILD_TESTS` is enabled so the CPU contracts keep working |

CMake keeps the runnable sample and its verification support separate.
`public_asset_fixture` (example) contains only the data/session/presentation
code that the runtime and editor plugin link. `public_asset_fixture_verification`
contains image comparison and depends on the example; the guard and the CPU
contracts link it.

## Files

| Directory | Path | Role |
| --- | --- | --- |
| Example | `examples/public_asset_fixture/FixtureData.h/.cpp` | Deterministic PHSC/PHRC archive generation, typed `PHMS` mesh resources, the documented fixture schema, content validation and content hashes |
| Example | `examples/public_asset_fixture/FixtureSession.h/.cpp` | `PhlosionArchiveSession` lifecycle: initial A activation, failed replacement, valid replacement, bounded preparation |
| Example | `examples/public_asset_fixture/FixturePresentation.h/.cpp` | Camera, rigid motion, cached indexed draws, role rectangles and handshake JSON |
| Example | `examples/public_asset_fixture/FixtureRuntimeMain.cpp` | Standalone hidden-window host (`PublicAssetFixtureRuntime`) |
| Example | `examples/public_asset_fixture/FixtureEditorPlugin.cpp` | Real editor project plugin (`PublicAssetFixtureEditorPlugin`) using ABI 35 |
| Example | `examples/public_asset_fixture/README.md` | Directory ownership and sample/verification split |
| Verification | `tools/public_asset_fixture/FixtureImageGuard.h/.cpp` | PNG load/save, viewport detection, image metrics, checker/expected-content checks, synthetic capture builder and contact-sheet composition |
| Verification | `tools/public_asset_fixture/FixtureGuardMain.cpp` | `PublicAssetFixtureGuard detect|compare|expect|matrix|synth|echo` |
| Verification | `tools/public_asset_fixture/FixtureProcess.psm1` | Shared native-process launcher, screenshot-log parser and PNG-size helper |
| Verification | `tools/public_asset_fixture/run_capture_matrix.ps1` | Native PowerShell runner for the 24-capture matrix |
| Tests | `tests/public_asset_fixture/test_fixture_harness.ps1` | Adversarial CLI/validator and process-launcher tests (registered as `PhlosionPublicAssetFixtureHarness`) |
| Tests | `tests/TestPublicAssetFixtureRendering.cpp` | C++ CPU contracts for fixture validation, lifecycle and image guards |

## Fixture schema

Each scene archive embeds a versioned JSON document plus one texture file per
material binding. The document is
`"schema": "phlosion.public-asset-fixture-scene"`, version `2`, with a logical
`scene_id` of `fixture/scene` and a `content_id` of `a` or `b`. Both content
variants share the same logical scene identity; their archive roots are
`fixture/scene-a` and `fixture/scene-b`.

Geometry is not embedded as a private record. Every object references a typed
`.phmesh` document through a required scene dependency edge that declares the
expected PHRC content hash. Each mesh has a stable logical id
(`fixture/mesh/<role>`) and a versioned registry id per content variant
(`fixture/mesh/<role>-a`/`-b`), which the fixture resolver maps to
`AssetKind::Phrc`. Texture files keep the small fixture record (`PTEX`, version
1): width, height and RGBA8 pixels. The JSON records each object's mesh asset
id, logical mesh id, expected mesh document hash and structural counts, the
material binding for every mesh material slot label, each texture file's FNV-1a
hash, and the overall `content_hash` computed over the mesh document bytes and
texture bytes in object order. Every declared mesh slot must be bound exactly
once; a missing or duplicated binding is rejected before activation.

`validateFixtureSnapshot` runs before any activation and rejects a snapshot
that has no typed scene archive, an object whose mesh reference does not match
its role, a mesh asset missing from the staged graph, a mesh resolved as
something other than the expected PHRC resource, a declared mesh hash that does
not match the loaded document, a mesh that the typed decoder rejects, a mesh id
or structural count that disagrees with the document, a missing material
binding for a declared slot, a duplicated binding for one slot, a missing or
unreadable embedded texture, a
per-file or overall hash mismatch, invalid `PTEX` headers, non-positive or
document/decoded-mismatched texture dimensions, duplicate or empty object
ids/roles, unsupported material values, a non-finite or out-of-range alpha
cutoff, a non-finite or out-of-range camera (position, target, field of view,
clip planes), non-finite or invalid rigid-motion metadata, or a missing required
object role.

Every archive also declares the required dependency edge `fixture/leaf/core`
and one required edge per typed mesh resource, each with an explicit expected
content hash. The failed replacement archive (`fixture/scene-missing`) declares
the required edge `fixture/leaf/missing`, which the store deliberately does not
contain, so the dependency-aware graph fails before any replacement can
activate.

Object roles are fixed and guard-checked: `background`, `board`, `occluder`,
`hidden`, `blend` and `marker`. The board uses an indexed textured quad with a
2x2 checker texture. The farther hidden object is a dark green ring drawn last,
so the nearer blue occluder must win through depth testing and a removed ring
cannot be mistaken for the bright green background. The blend object uses
alpha blending over the background, and the marker moves rigidly with the
logical frame.

Cache keys follow the identity rule: `textureKey` stays
`fixture/tex/<object>` across content variants, while `textureCacheKey` and
`geometryCacheKey` include the content hash. A stale GPU cache keyed only by
logical identity is therefore observable, and a consumer regression that
reuses the previous content's key fails the B-frame content checks.

## Cases

| Case | Logical frame | Lifecycle |
| --- | --- | --- |
| `a-frame0` | 0 | Activate A, present frame 0 |
| `a-frame30` | 30 | Activate A, present frame 30 |
| `failed-replacement` | 30 | Activate A, then load `fixture/scene-missing`; the graph fails and A stays active |
| `valid-replacement-b` | 30 | Activate A, then stage, validate and activate B |

Replacement cases use one `PhlosionArchiveSession` and one presentation owner.
The runtime presents A before it attempts the replacement; the editor plugin
draws A before the same session attempts replacement, so the replacement is a
real content transition over existing GPU cache state. The activation count is
one for `a-frame0`, `a-frame30` and `failed-replacement`, and two for
`valid-replacement-b`.

## Hosts

`PublicAssetFixtureRuntime` creates a hidden native window, selects the
requested backend without fallback and exits nonzero when the requested API is
unavailable. It rejects an actual backend that differs from the requested one.

`PublicAssetFixtureEditorPlugin` is loaded by `PhlosionEditor` through a
generated `phlosion.project.json` and presents in the Scene viewport. The
runner generates the descriptor under the artifact directory, copies the
plugin DLL beside it, and never uses a fake editor wrapper.

Both hosts write a handshake JSON document with:

- `host`, `case`, `logical_frame`, `surface` dimensions;
- `requested_backend`, `actual_backend`, `gpu`, `gpu_discrete`;
- active `root`, `content_id`, `content_hash`, `archive_file_count`,
  `activation_count`, `triangles`;
- `preparation_complete` and the transition fields `attempted`, `succeeded`,
  `failed`, `requested_root`, `outcome`, `error`;
- normalized role rectangles for every rendered object, computed from the same
  camera and motion used to draw.

## Capture matrix and provenance

The matrix is four cases x three native APIs x two hosts = 24 deterministic
captures. Existing backend capture controls are used
(`PHLOSION_BACKEND_SCREENSHOT_PATH` / `PHLOSION_BACKEND_SCREENSHOT_FRAME`).
The runtime captures at frame 6, the editor at frame 8 (editor frames 14).
Logical frame selection comes from `PHLOSION_FIXTURE_LOGICAL_FRAME`; it is
never inferred from wall time or host frame counters.

The runner records, in `manifest.json`:

- the engine source revision and dirty state, build configuration, PowerShell
  and .NET versions, and the requested capture/editor frames;
- SHA-256, byte count and modification time for the runtime, editor, plugin and
  guard binaries;
- per entry: host, API, case, screenshot/handshake/stdout/stderr/metrics paths,
  process exit code, requested capture frame, parsed screenshot backend, parsed
  observed capture frame and screenshot dimensions.

The guard re-reads the raw logs rather than trusting the recorded fields. For
every entry it requires:

- exactly one `[Screenshot][<Backend>] WROTE` record, no `FAILED` record, the
  requested API's native backend, the requested capture frame, the exact
  capture path and dimensions matching the PNG;
- a `[ParityContract][<Backend>] PASS` line with no matching `FAIL` line (the
  runner sets `PHLOSION_PARITY_CONTRACT_FATAL=1` for every child);
- no `using OpenGL compatibility mode` fallback text;
- a zero process exit code and, for editor entries, metrics whose
  `renderer.backend` matches the requested API, whose frame count reaches the
  requested editor frames and whose viewport matches the handshake surface.

Editor captures are full-window screenshots. The guard detects the viewport
from the fixture's saturated-green background, crops it, and uses one crop per
host for every case and API on that host; the full screenshots are retained.
Standalone captures fill the whole window and resolve to the full image.

## Guard thresholds and expected content

Pairwise comparisons use fixed thresholds, applied only after the crop:

- normalized MAE <= 0.01;
- normalized RMSE <= 0.08;
- changed-pixel ratio <= 0.05 at channel tolerance 8/255.

A manifest may request stricter values but is rejected if it omits the
thresholds, uses non-finite or non-positive values, or loosens any approved
value. `--require-complete` additionally requires exactly the unique Cartesian
set of `{runtime, editor} x {opengl, vulkan, d3d12} x four cases`; unknown,
duplicate and missing tuples fail the run, including skipped comparisons.

Expected-content checks use interior regions of the handshake role rectangles:

- green background corners and global green coverage;
- the A (magenta/cyan) or B (orange/yellow) checker board, including an edge
  band, a presence check for the two board colors, and a 2x2 quadrant check
  that requires both colors and the expected screen-space arrangement, so a
  solid-color board or a flipped checker fails;
- a blue occluder interior with no dark-green or bright-green leak, and a dark
  green hidden-object ring proving the depth order;
- a blended region distinct from the unblended mirrored control region;
- a white marker whose measured centroid matches the expected motion and whose
  side matches the logical frame and content variant.

The matrix also asserts that the failed replacement frame is pixel-identical to
the equivalent A frame, that A frame 0 differs visibly from A frame 30 (moving
marker) and that A frame 30 differs visibly from B frame 30.

## Running

```powershell
cmake --build build-presentation --config Debug --parallel 4
ctest --test-dir build-presentation -C Debug --output-on-failure
.\tools\public_asset_fixture\run_capture_matrix.ps1 -RunId my-run
```

The runner writes captures, logs, `manifest.json`, `matrix.json` and
`contact-sheet.png` under a fresh `artifacts/m1-d/<run-id>/` directory. It uses
Windows PowerShell 5.1 / .NET Framework or PowerShell 7 / .NET Core, launches
each child directly through `System.Diagnostics.Process` with
`CreateNoWindow`, asynchronously drains stdout/stderr, propagates the exit
code, and enforces a finite timeout with tree cleanup
(`Process.Kill(true)` when available, otherwise `taskkill /T /F`). Per-process
environment overrides replace the previous global environment mutation, so the
caller's environment is never modified.

GPU runs stay opt-in and are not registered in CTest. The CPU contracts
(`public_asset_fixture_rendering_contract`) and the adversarial harness
(`PhlosionPublicAssetFixtureHarness`) run in the normal CTest set.
`PhlosionPublicAssetFixtureHarness` uses only the hermetic `synth` command and
a harmless two-level PowerShell child; it covers duplicate/missing/unknown
matrix tuples, loosened and invalid thresholds, exit-code/capture-frame/backend
provenance tampering, blank content, argument quoting with spaces and shell
metacharacters, nonzero child exit codes and timeout tree cleanup.

## Limits

This fixture establishes content-driven rendering, depth, blending, rigid
motion and a texture/geometry replacement at one logical identity on all three
native APIs in both hosts. It does not claim skeletal animation, production
PBR/material interoperability, private Pokemon assets, streaming, physics,
UI/audio, performance budgets or game readiness.

## Recorded evidence

On 2026-09-20, the existing `build-presentation` Debug tree (VS2026,
editor/tests enabled, engine warnings as errors) rebuilt cleanly and the full
CTest set passed 6 of 6 entries, including 17 C++ contracts and the
`PhlosionPublicAssetFixtureHarness` adversarial CLI/process suite. A fresh
child-project configure with `add_subdirectory` confirmed
`PHLOSION_BUILD_PUBLIC_ASSET_FIXTURE=OFF` by default, with
`PhlosionEngineTests` and `public_asset_fixture` present and no fixture host
project files. These checks were then repeated after the directory relocation
described above: the `build-presentation` Debug build was warning-clean, CTest
passed 6 of 6, and the relocated runner
(`tools/public_asset_fixture/run_capture_matrix.ps1`) captured the complete
24-case native matrix as `reorg-run02` with `failure_count = 0`, proving the
new script roots, import path, generated project descriptor and plugin
resolution.

The relocation comparison in
`artifacts/m1-d/reorganization-result.json` records the before/after SHA-256 of
every affected file: the guard, image-comparison and process-launcher files
moved byte-identically, the example data/session/presentation files and the C++
contract were unchanged in place, and only the runner, harness and
`CMakeLists.txt` have content edits (script root/import defaults and the
example/verification library split).

The complete local GPU matrix before the relocation (`fix-run03`, historical
paths) captured all 24 cases with per-entry provenance, exact screenshot-frame
and parity-contract gates. Pairwise API comparisons inside each host passed
with a worst normalized MAE of 0.0000052 and worst normalized RMSE of 0.00024;
A30 vs failed-A30 was pixel-identical on every host/API; A0 vs A30 changed
1.25-1.40% of pixels and A30 vs B30 changed 31-35%. Logs, handshakes, editor
metrics, `manifest.json`, `matrix.json` and the contact sheet are under
`artifacts/m1-d/fix-run03/`; the post-relocation matrix is under
`artifacts/m1-d/reorg-run02/`.

Astra's final review restored the original board-coverage minimums of 0.4
interior and 0.35 edge alongside the new quadrant checks. The Debug build and
all six CTest entries passed again, and all 24 retained native captures passed
the stricter guard. Final revalidation evidence is under
`artifacts/m1-d/astra-{build,ctest,matrix}.log`, `astra-matrix.json` and
`astra-manifest.json`; the original capture manifest remains intact.

M1-E then replaced the fixture's embedded `PGEO` geometry with typed `.phmesh`
dependency resources (scene schema version 2). The
`build-presentation` Debug tree rebuilt warning-clean, all six CTest entries
passed, and the same four cases were captured on native OpenGL, Vulkan and
Direct3D 12 in both hosts (`artifacts/m1-e/m1e-run01/`, `failure_count = 0`).
Pairwise API comparisons inside each host passed with a worst normalized MAE of
0.0000052 and worst normalized RMSE of 0.00024 under the unchanged
0.01/0.08/0.05 limits; A30 vs failed-A30 was pixel-identical; A0 vs A30 changed
1.25-1.40% and A30 vs B30 changed 31-35%. Comparing every retained capture with
the accepted `artifacts/m1-d/reorg-run02/` image at the same host/API/case gave
a maximum channel difference of 0 on all 24 pairs, so the migration preserves
appearance exactly. Logs, handshakes, editor metrics, `manifest.json`,
`matrix.json`, the contact sheet and the cross-run comparison records are under
`artifacts/m1-e/`.

Astra's M1-E review then requested one correction cycle: strict UTF-8
validation for mesh names, one mesh hash per object plus one binding per slot in
the fixture consumer, forwarding the mesh tangent through the world-vertex
adapter, and a literal golden payload in the mesh contract. The corrected tree
rebuilt warning-clean, all six CTest entries passed again, and the complete
24-case native matrix was recaptured as `artifacts/m1-e/m1e-run02/` and then,
after two reverted test-sensitivity mutations and a clean rebuild, as the final
set `artifacts/m1-e/m1e-run03/`; both runs report `failure_count = 0` under the
same thresholds, and `m1e-run03` records fresh binary hashes that match the
on-disk hosts. Those captures are pixel-identical to the accepted
`artifacts/m1-d/reorg-run02/` images and to the initial
`artifacts/m1-e/m1e-run01/` captures at every host/API/case
(`artifacts/m1-e/m1e-run03-vs-m1d/`, `artifacts/m1-e/m1e-run03-vs-run02/`): the
fixture's materials do not consume tangents, so forwarding them is a data-path
correction rather than a visible change.
