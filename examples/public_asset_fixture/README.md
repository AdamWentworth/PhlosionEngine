# Public asset fixture example

This directory is the runnable sample. It contains the deterministic archive
data, archive-session lifecycle, shared presentation code, the standalone
runtime host and the Phlosion Editor project plugin:

| File | Role |
| --- | --- |
| `FixtureData.h/.cpp` | Synthetic PHSC/PHRC archives, typed `PHMS` mesh resources, fixture schema and snapshot validation |
| `FixtureSession.h/.cpp` | `PhlosionArchiveSession` activation and replacement lifecycle |
| `FixturePresentation.h/.cpp` | Camera, rigid motion, cached indexed draws, role rectangles and handshake JSON |
| `FixtureRuntimeMain.cpp` | Standalone `PublicAssetFixtureRuntime` host |
| `FixtureEditorPlugin.cpp` | `PublicAssetFixtureEditorPlugin` project plugin (ABI 34) |

Verification support lives outside this directory and is deliberately not a
dependency of the sample:

- `tools/public_asset_fixture/` owns image comparison and the guard CLI
  (`FixtureImageGuard`, `FixtureGuardMain`), the shared PowerShell process
  launcher (`FixtureProcess.psm1`) and the capture runner
  (`run_capture_matrix.ps1`).
- `tests/public_asset_fixture/` owns the adversarial PowerShell harness
  (`test_fixture_harness.ps1`); the C++ contract stays at
  `tests/TestPublicAssetFixtureRendering.cpp`.

CMake exposes the sample as `public_asset_fixture` and the verification
support as `public_asset_fixture_verification`. The runtime and editor plugin
link only the sample; the guard and tests link both. See
[`docs/PUBLIC_ASSET_FIXTURE.md`](../../docs/PUBLIC_ASSET_FIXTURE.md) for the
schema, capture contract and verification workflow.
