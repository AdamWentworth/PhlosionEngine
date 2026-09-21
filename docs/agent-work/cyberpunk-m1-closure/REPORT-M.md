# Bundle M GPU foundation report

Implementation worker: explicitly selected Astra fallback after Flash HTTP 402.
Scope: GPU foundation only; root owns scene/session consumers, CMake/TestMain
registration, qualification and shared readiness documents. No commits, staging,
provider changes, or recursive agents.

## Delivered

* Separate `IWorldResourceScope` opt-in interface and header-only
  `WorldResourceScopeState` admission/accounting helper. Existing render backend
  virtual ABI remains unchanged. Unique handles, non-nested begin/end, pending
  retirement, sticky failure diagnostics, 256 live-scope limit and checked logical
  payload budgets are implemented.
* Native cache entries carry ownership and keys are namespaced before lookup.
  Counts are committed after successful allocation. Fallback/environment,
  sprite/editor/transient/frame resources remain outside managed accounting.
* GL deletes scoped VAOs/buffers/textures after completion and invalidates bindings.
* Vulkan allocates dedicated managed geometry rather than leaking arena space;
  after device idle it individually frees material sets and rebuilds/compacts
  indexed tables before deleting images/geometry. Its pool supports individual
  frees. Buffer allocation unwinds partial native objects on failure.
* D3D12 uses a checked queue fence before releasing objects/recycling texture SRVs
  and material blocks. Live sprite/editor/unowned descriptor identities remain
  untouched; the shared heap high-water mark is never reset by scope collection.
* CPU helper contract tests and `docs/WORLD_RESOURCE_SCOPES.md`, including exact
  accounting limits, descriptor-count meanings, API allocation estimates and the
  blocking maintenance boundary.

Principal new files:

* `src/engine/render/IWorldResourceScope.h`
* `src/engine/render/WorldResourceScopeState.h`
* `src/engine/render/opengl/OpenGLRenderBackendResourceScopes.cpp`
* `src/engine/render/vulkan/VulkanRenderBackendResourceScopes.cpp`
* `src/engine/render/d3d12/D3D12RenderBackendResourceScopes.cpp`
* `tests/TestWorldResourceScope.cpp`

Existing edits are confined to the three concrete backend headers, their cache,
allocation, pipeline and lifecycle implementations. The root registered the new
sources/tests and integrated the optional interface in its consumer work.

## Verification

Commands from `D:/Projects/Phlosion/PhlosionEngine`:

```powershell
cmake --build build-presentation --config Debug --target engine_render PhlosionEngineTests -- /m /p:TreatWarningAsError=true /verbosity:minimal
& build-presentation/Debug/PhlosionEngineTests.exe
```

Both exited 0. The native library compiled all three APIs with warnings treated
as errors; all 32 registered CPU contracts passed, including the new scope
contract and root's scene session/lifecycle contracts.

Logs: `artifacts/m1-m/gpu-foundation-build.log` and
`artifacts/m1-m/gpu-foundation-contracts.log` (ignored artifacts).
Targeted `git diff --check` passed.

## Qualification boundary

No native runtime/editor visual or repeated GPU lifetime qualification is claimed
by this worker. Root owns the three-API integrated matrix and expected-content
checks. The cap is retained logical payload only, not peak/process/driver memory;
GL API allocation bytes are explicitly estimates. Maintenance can wait for GPU
completion. Legacy unowned resources retain their original lifetime behavior.

## Follow-up native cache review

GL profile replacement previously purged cached geometry without updating the
new managed ledger. It now checks completion before replacing the profile,
rejects profile replacement during a world batch, debits each freed managed mesh
while keeping the scope/textures live, and invalidates binding state. New CPU
coverage verifies debit, re-admission within the same cap, balanced cumulative
reclamation after rebuilding, and pending retention after a completion failure.

Exact correction paths: `WorldResourceScopeState.h`, `OpenGLRenderBackend.cpp`,
`opengl/OpenGLRenderBackendWorldPipeline.cpp`,
`d3d12/D3D12RenderBackendLifecycle.cpp`, and `tests/TestWorldResourceScope.cpp`.
The D3D12 correction clears cached meshes during explicit shutdown (previously
they persisted until the C++ backend object itself was destroyed). Vulkan bulk
geometry/texture/material destruction is confined to shutdown; its profile
replacement only rebuilds pipelines. D3D12 sprite-heap initialization is called
only during device construction; profile replacement preserves caches. GL bulk
texture teardown is confined to shutdown.

The CPU test verifies ledger behavior, not a native profile-switch screenshot.
The integrated fixed-profile native lifecycle qualification remains root-owned.

Correction verification repeated the same build and full CPU executable commands:
both exited 0, all 32 contracts passed. Final logs:
`artifacts/m1-m/gpu-foundation-correction-build.log` and
`artifacts/m1-m/gpu-foundation-correction-contracts.log`.
Targeted `git diff --check` passed (only checkout CRLF conversion notices).

Status: **ready_for_review**. Native source/build ownership returned to root;
no remaining worker blocker.
