# Animation fixture example

This directory is the runnable sample for the public weighted skeletal
animation fixture. It authors a tiny hinged strip rig, samples poses through the
shared CPU pose sampler and submits the original bind vertices plus a
caller-owned skin palette to the native renderers:

| File | Role |
| --- | --- |
| `AnimationFixtureCase.h/.cpp` | The four authored cases: clip, sample time, logical frame and the analytic tip angle |
| `AnimationFixtureRig.h/.cpp` | Two-joint rig, one skin, LINEAR and STEP rotation clips, bind strip vertices/indices, mesh transform and camera |
| `AnimationFixturePresentation.h/.cpp` | Pose sampling, `inverse(mesh) * jointGlobal * inverseBind` palette, background/rig draws and the handshake document |
| `AnimationFixtureRuntimeMain.cpp` | Standalone `AnimationFixtureRuntime` host (hidden native window, no backend fallback) |
| `AnimationFixtureEditorPlugin.cpp` | `AnimationFixtureEditorPlugin` project plugin loaded through the existing editor ABI |

The sample never deforms vertices on the CPU: it submits bind positions with
joint indices and weights, sets `gpuSkinning` on the world draw and passes the
palette pointer. Each host presents the bind pose first and then the requested
case pose through the same geometry cache key and palette storage, so an
unchanged or stale palette is visible in the capture.

Verification support lives outside this directory and is deliberately not a
dependency of the sample:

- `tools/animation_fixture/` owns the image primitives, the authored
  expectations, the guard CLI (`AnimationFixtureGuard`) and the capture runner
  (`run_capture_matrix.ps1`, which reuses the shared native process launcher).
- `tests/animation_fixture/` owns the adversarial PowerShell harness; the C++
  contracts stay at `tests/TestAnimationFixture.cpp`.

CMake exposes the sample as `animation_fixture` and the verification support as
`animation_fixture_verification`. The runtime and editor plugin link only the
sample; the guard and tests link both. See
[`docs/ANIMATION_VERIFICATION.md`](../../docs/ANIMATION_VERIFICATION.md) for the
rig contract, capture matrix and provenance requirements.
