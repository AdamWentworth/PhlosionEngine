# M1 closure: asynchronous scene activation and GPU lifetime

Baseline: main `56cdf24` (clean at orientation). User requested completion of
the remaining engine-facing M1 work, not the private game-content track.
Astra plans/reviews; native `astra_flash_builder` implements bounded bundles.
User explicitly permits Astra implementation if Flash remains unavailable.
No automatic commit or push. Preserve all other work.

## Scope and acceptance boundary

M1-K proves synchronous CPU glTF import, animation/material integration and
native rendering. Remaining engine-facing work is (1) archive/async integration,
(2) explicit GPU ownership and reclamation, and (3) measured repeated lifecycle
qualification. General streaming, hard frame-time/process-memory guarantees,
private roster qualification, new material features and M2 are excluded.

Two dependency-ordered bundles are appropriate: CPU archive/session contracts
must stabilize before the renderer/sample can consume them. One writer at a
time; Astra owns this plan, CHECKPOINT, shared readiness and verification docs.

## L: background glTF decoding and atomic archive scene activation

### Architecture

Add a synchronous store-backed overload of `loadGltfSceneAsset` using
`const engine::IAssetStore&`, a virtual document path, existing output/error/load
options. Keep the filesystem overload and its behavior. Share validation/build
code; do not copy the importer. Parse in-memory document bytes and resolve all
external buffers/images through the supplied store. No temporary-file extraction
or fallback to the host filesystem/network. Preserve data-URI/GLB behavior of
the documented subset. Reject absolute/drive/UNC/network/query/fragment paths,
bad URI encodings and paths escaping the virtual store root; normalize relative
segments before store lookup. Missing external required bytes fail with a useful
diagnostic rather than accidentally reading a same-named local file. Present but
undecodable images retain the existing documented role fallback policy.

Add `GltfSceneSession` under `src/engine/render/gltf`, composing the existing
`PhlosionArchiveSession` with one owned background decode worker. Public methods
are owner-thread only and never join/wait for provider I/O or glTF decode.
Existing archive graph pump/typed archive decode semantics remain documented:
graph reads are background, but graph/PHRC interpretation can run in pump.
The worker performs glTF parsing, external store reads, geometry and image decode
against the immutable staged archive store. Do not create graphics objects there.

The session admits a candidate containing root archive asset ID, Scene/Prefab
kind and embedded glTF virtual path. Constructor takes store, resolver and
options (existing graph/load options plus a decoded retained-payload cap).
API: begin, pump, state, error, staged, active, activate, cancel, discard, unload;
states Idle/Loading/Decoding/Staged/Failed/Cancelled. A snapshot owns both the
archive snapshot and `shared_ptr<const GltfSceneAsset>`, plus decode/encoded and
decoded retained-payload measurements. A failed/cancelled/discarded candidate
never changes active. Activation publishes the matching pair atomically on the
owner thread, without re-reading/decoding. Externally retained snapshots survive
replacement, unload and session destruction.

Cancellation/discard suppress late worker results using monotonic candidate
identity; a released decode can finish but must never publish into a later
candidate. Support a new candidate after discard without waiting for that
decode. Bound queued/current decode jobs (one worker, at most one pending job),
with explicit rejection/backpressure rather than unbounded accumulation.
Destructor joins after stop, and may wait for an already active provider/decode.
Handle worker exceptions as candidate failures and wake/shut down reliably.

The decoded cap bounds logical geometry/pixel bytes held by session active plus
staged/candidate outputs before admission/publication; an over-budget candidate
fails and preserves active. Check arithmetic. Encoded graph budgets retain their
existing definition. Do not claim a global peak-memory budget: parser/decoder
temporaries, container overhead, source-store allocations and externally retained
snapshots lie outside these counters. Report each category honestly.

### Ownership and verification

Worker may edit GltfSceneAsset.{h,cpp}, add GltfSceneSession.{h,cpp}, narrowly
adjust existing decoder helpers if necessary, CMakeLists.txt and TestMain.cpp,
new/affected CPU tests and test helpers, GLTF_SCENE_ASSETS.md, a new session guide,
and `REPORT-L.md` here. Do not edit backends, shaders, example/guard behavior,
shared roadmap/verification/checkpoint, cache schema or plugin ABI in bundle L.
You are not alone in the codebase; preserve others' edits and adapt to them.

CPU tests must exercise real encoded scene/prefab archives containing glTF,
binary buffers and PNG data through existing archive/dependency loading. Cover
filesystem/store equivalence, no disk fallback, URI/path escape rejection,
malformed input and missing dependencies, active A while B loads, failed B,
cancel/discard during a gated decode, stale-result suppression and a new request,
successful B activation, retained A after unload/source destruction, stop/join
and provider lifetime, per-candidate and active+candidate budget overflow.
Use bounded condition-variable gates and actual thread IDs, not sleeps as proof.
Tests may add a narrow injected decode function in an internal test seam when
needed to gate worker completion; production defaults must use the real importer.

Build enabled Debug targets with /WX in build-presentation; run full CTest and
existing 36-scene native matrix if shared importer behavior changed. Retain logs
under ignored artifacts/m1-l. Report exact commands, exits, files and limitations.
Work through routine debugging; checkpoint on a material contract/environment
blocker. Budget up to 120 minutes, no recursive agents, no provider/config changes.

## M: GPU ownership, lifecycle integration and qualification

The renderer foundation is independent of L and can run while root finishes its
CPU contracts. Consumer integration depends on both accepted interfaces. Flash
returned HTTP 402 before editing; per the user's explicit fallback authorization,
root Astra implements L/integration and an explicitly selected Astra worker owns
only the independent native renderer foundation. No overlapping writers.

### Approved GPU contract

Add `IWorldResourceScope` (separate opt-in interface, obtained by dynamic_cast
from IRenderBackend) and common value types in a new shared header. All three
native renderers implement it; it does not change the existing renderer/plugin
vtable contract. API naming may follow conventions, but freeze the header early:
create a renderer-owned monotonically unique scope with a logical GPU payload
byte cap; begin/end a non-nested draw scope; request release (safe during an
editor callback); query scope and aggregate resource statistics. Zero is legacy
unowned behavior. Invalid/retired/foreign IDs and nested scopes fail explicitly.
Scope handles do not retain a backend pointer or own native resources themselves.

Only managed cached indexed/textured world drawing is required. Scope identity
namespaces cache keys before lookup so identical content in separate scopes has
independent GPU ownership. Legacy/unowned cache identities remain unchanged.
Fallback/environment textures remain shared/unowned and are excluded from scope
counts. Do not capture unrelated sprite/editor/transient resources into a scope.
Retirement immediately closes admission; actual resource destruction occurs at
the next beginFrame maintenance boundary before recording, after completed GPU
use is established (GL completion, Vulkan device idle, D3D12 checked queue fence).
This maintenance can block and must be documented. A failed GPU wait cannot be
treated as successful release. Scope metadata/capacity must also be reclaimed;
unknown/released scopes return missing, not live empty success.

Track actual owned geometry buffers/pages, texture objects, material descriptors
and their retained logical payload bytes, pending-release bytes/counts, allocation
and reclamation totals. Resource counts come from backend state, not CPU asset
claims. Expose API allocation bytes where meaningful and distinguish estimates
(especially OpenGL texture storage) from driver/process memory. Enforce checked
per-scope payload admission and a bounded live-scope count; include pending
retirements in limits until collection. Allocation/budget failures surface through
scope status so the caller cannot mistake a silent native fallback for success.

Vulkan: managed geometry uses scope-owned pages/allocations, never strands bytes
in the global append-only arena. Free owning resources after completion; release
material descriptor sets using a pool that permits individual frees; invalidate
dependent material caches/table pointers and safely rebuild/reuse table slots.
Preserve live scopes and unowned resources. D3D12: reclaim texture SRV slots and
eight-descriptor material blocks into bounded reusable free lists; never reset
the shared heap high-water mark across live sprites/editor descriptors. Recycle
only after queue completion and invalidate dependent material binding caches.
OpenGL: delete owned VAOs/buffers/textures after completion and invalidate binding
caches which could retain deleted/reused object names.

Reference rationale: Vulkan descriptor-pool/free rules in
https://docs.vulkan.org/spec/latest/chapters/descriptorsets.html and D3D12 GPU
completion ownership in https://learn.microsoft.com/en-us/windows/win32/direct3d12/fence-based-resource-management.

Worker owns new scope interface/common helpers; concrete backend headers,
native backend implementations and narrowly related allocation/lifecycle helpers;
new standalone CPU helper tests and a new WORLD_RESOURCE_SCOPES.md guide;
REPORT-M.md here. Root owns GltfSceneRenderer/session/sample/guard integration,
CMake/TestMain registration and all shared roadmap/checkpoint docs. Worker sends
the header contract once stable and any new cpp/test paths for root registration;
root must not mutate backend-owned files. No shader/profile feature changes,
whole-cache purges, game files, new dependencies, commits or provider changes.
Worker compiles engine_render (root registers new cpp sources) and runs meaningful
helper CPU tests if present. Root performs final integrated native qualification.

Required behavior: explicit scene GPU ownership, safe reclamation after submitted
uses finish, reusable geometry/texture/material descriptor capacity, independent
live scenes preserved, counters grounded in actual backend resources, and clear
admission limits. Release must be safe from the real editor preview callback.
An explicit maintenance boundary may wait for GPU completion; no claim of
nonblocking streaming. Legacy unowned cache behavior remains compatible.

Integrate L and the GPU contract into the public scene example. Exercise load,
pending/failed/cancelled replacement, accepted replacement, retained old scene,
repeated unique-content reloads, and unload while the host keeps rendering.
GPU resource/descriptor counts must stabilize and return to a measured baseline;
observe live geometry/pixels on every API and both runtime/editor. Record loading
and CPU/GPU retained-byte measurements with their exact accounting limits.
Expected-content/parity and evidence rejection checks remain mandatory; do not
weaken M1-K guards or count process shutdown as release qualification.

Final acceptance: build /WX, all CPU/CTest checks, three-API runtime/editor
integrated lifecycle matrix and affected archive/animation/material regressions.
Astra reviews actual code and evidence, requests one consolidated correction if
needed, then updates M1 exit status. Completion is based on gates, not the earlier
rough 80% estimate. Private game qualification remains separate.

## Outcome

Both bundles and their integration were accepted under the user-authorized Astra
fallback. See [ACCEPTANCE.md](ACCEPTANCE.md) for review, final test/matrix evidence,
measured accounting and limits. The engine-facing M1 checklist is complete;
the working tree is ready for review/commit. No M2 work or automatic commit/push.
