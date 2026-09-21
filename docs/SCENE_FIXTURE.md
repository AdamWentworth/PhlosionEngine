# Scene fixture: integrated glTF character and environment

Status: implemented and verified on native OpenGL, Vulkan and Direct3D 12 in
both the standalone runtime host and the real editor plugin. The engine path it
exercises is documented in [glTF scene assets](GLTF_SCENE_ASSETS.md).

## What the sample demonstrates

`examples/scene_fixture` authors one small public glTF document and presents it
through the engine's CPU scene path:

- a block character (torso, emissive head lamp, static left arm) with a
  two-joint weighted limb,
- a floor, a wall and a rotated prop, each with its own material slot,
- real PNG base-color maps, nontrivial base RGB factors and one
  emissive-factor-only surface with no emissive map,
- an authored elbow rotation clip (identity at t=0, the bent pose at t=1),
- a non-identity mesh-node transform on the weighted limb, which the palette
  composition cancels exactly.

The additional two-color inspection panel is also imported from glTF. Its
diagnostic view tests color-factor math independently of the lit scene.

The sample owns content, camera and lifecycle choices only. Import, palette
composition and draw binding come from `src/engine/render/gltf`; the sample
never constructs `Model` and never hand-builds runtime geometry.

## Cases

Six cases are captured on each native API by both hosts (36 captures):

| Case | Authored state |
| --- | --- |
| `a-bind` | The explicit bind pose of content A |
| `a-pose` | The same in-process asset sampled at the authored bent pose |
| `failed-replacement` | Draws A, fails a malformed replacement, keeps drawing A |
| `replacement-b` | Draws A, replaces the same source path with content B, draws B |
| `emission-off` | Content A with the emissive factor authored to zero |
| `unloaded` | Draws A, releases the active CPU scene, draws only the background |

Each case applies its authored transition at frame 3 and is captured at frame 6,
so the captured frame always shows the case's final state. The handshake records
the active asset identity/generation, the replacement outcome and diagnostic,
the unload state, the draw statistics and the analytic presentation regions.
It is written at the capture frame, including actual render/transition counters.
The `a-pose` case starts in bind pose and changes the same loaded snapshot at
frame 3. A separate, tiny imported albedo panel uses the generic adapter under
debug view 2 and resets that state after drawing.

## Verification

`tools/scene_fixture/run_capture_matrix.ps1` sizes the runtime viewport from the
validated editor viewport, records process/PNG/handshake/binaries evidence and
runs `SceneFixtureGuard matrix`. The guard derives every expectation from the
authored source, never from a capture:

- the displayed background from the authored linear color through the
  documented ACES tone map and sRGB transfer,
- the weighted-limb tip from the authored rig and analytic palette composition,
  checked at its projected position in every capture,
- base-color factor application from source bytes converted to linear color,
  multiplied once, and encoded for display: both 5x5 interior pixel patches
  must match the independent expectation within three channel values,
- environment presence regions derived by projecting the authored floor and
  wall and excluding the character's projected bounds,
- the emissive-factor effect as a head-region luminance difference between
  `a-pose` and `emission-off`,
- the replacement change as a character-region difference between `a-pose` and
  `replacement-b`,
- failed-replacement preservation by comparing `a-pose` and
  `failed-replacement` pixels,
- absence of scene content after unload.

The transition/replacement/emission comparisons run separately for every
API/host pair, so the whole-image parity tolerance cannot hide a missing small
effect on one backend.

Every capture must also agree with every other capture of the same case across
hosts and APIs under the standing thresholds (normalized MAE <= 0.01, normalized
RMSE <= 0.08, changed-pixel ratio <= 0.05 at channel tolerance 8). The guard also
requires the exact 36-case Cartesian set, rejects stale binary hashes/sizes and
requires capture evidence paths to stay inside the run directory. Binaries are
rehashed at their recorded build paths. Logs, screenshot frame/dimensions,
handshake hash/state and required editor project metrics must agree; malformed,
missing or overflowing metadata fails verification.

CPU coverage: `tests/TestSceneFixture.cpp` (catalog, analytic rig, staged
document import, lifecycle transitions, oracle including a negative case) and
`tests/scene_fixture/test_fixture_harness.ps1` (hermetic synthetic matrix,
29 adversarial mutations, pristine re-validation).

## Running it

```powershell
cmake --build --preset debug
powershell -NoProfile -ExecutionPolicy Bypass -File `
    tools/scene_fixture/run_capture_matrix.ps1 -EngineRoot . -RunId my-scene-run
```

Evidence lands in `artifacts/m1-k/matrix/<run-id>`: `manifest.json`,
`validation.json`, `contact-sheet.png`, `guard.log` and per-capture
`capture.png` / `handshake.json` / `stdout.log` / `stderr.log` / `metrics.json`.
The runner and guard are Windows tooling; native qualification in this pass
was performed on Windows with a GTX 1070. Other platforms are unverified.

## Limits

The sample is deliberately small and public. It is not private roster content,
does not exercise asynchronous archive streaming or GPU residency management,
and does not qualify general glTF or full PBR conformance.
