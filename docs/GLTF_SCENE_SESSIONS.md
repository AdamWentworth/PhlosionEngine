# Archive-backed glTF scene sessions

`engine::render::gltf::scene::GltfSceneSession` connects the existing
[archive/dependency loader](ARCHIVE_ACTIVATION.md) to the owned
[glTF scene path](GLTF_SCENE_ASSETS.md). It accepts Scene or Prefab archive IDs
and the virtual path of a glTF/GLB document inside the root archive. Required
dependencies are validated by the existing graph loader; the document's external
buffers and images must be present in that root archive's virtual store.

## Lifecycle

Construct with a shared `IAssetStore`, an `AssetResolver`, and optional graph,
import and decoded-payload limits. Use all public methods on one owner thread.

1. `begin(id, kind, documentPath, error)` admits a candidate only from `Idle`.
   It schedules archive loading without reading the provider on the caller.
2. Call `pump()` regularly. `Loading` covers archive/dependency loading;
   `Decoding` covers glTF parsing, external resource reads, geometry and image
   decode on one owned worker. No graphics context is used by either worker.
3. `Staged` exposes a shared immutable candidate containing the matching archive
   and CPU scene. `activate()` publishes that pair and returns to `Idle`.
4. Continue drawing `active()` while another candidate loads. Failure leaves it
   unchanged. `discard()` clears a candidate/error and permits another request.
5. `cancel()` applies during `Loading`/`Decoding`; `unload()` discards the
   candidate and releases the session's active reference.

Cancellation suppresses publication; it cannot interrupt an already running
provider read or decode. Monotonic candidate identity rejects late results even
after a new request begins. One decode can run and at most one replacement can
wait; requests do not accumulate an unbounded job queue. Retained snapshots
remain usable after replacement, unload and session destruction.

Public lifecycle calls never join a worker. Destruction requests stop, discards
queued work and joins; it can wait for an active read/decode. `pump()` retains
the archive layer's caller-side graph/PHRC interpretation work, so it is not a
hard frame-time guarantee. The capture sample's bounded completion barrier is
test synchronization, not a suggested production loading loop.

## Store import and accounting

The synchronous `loadGltfSceneAsset(store, virtualPath, ...)` overload shares
the filesystem importer's validation/build code. It reads the document and all
external buffers/images from the supplied store, with no filesystem/network
fallback or temporary extraction. Data URIs and GLB binary chunks are supported.
Relative segments are normalized within the virtual root. URI escapes decoded
by fastgltf are validated afterwards: absolute/drive/network paths, root escape,
control bytes, query/fragment delimiters and remaining malformed escapes fail.
Missing required external bytes fail; present undecodable images keep the
existing documented role fallback policy. Failed import preserves output.

`maxDecodedResidentBytes` defaults to 256 MiB. It limits logical geometry/index
and decoded pixel payload in active plus accepted candidate outputs. An
over-budget candidate fails before publication and preserves active. It does
not prevent allocation of a candidate's decode temporaries. Cancelled in-flight
work, metadata, node/skin/clip arrays, source storage, archive copies and
externally retained snapshots are outside this cap. `decodedResidentBytes()`
reports those accepted session-owned outputs, including a completed result
awaiting `pump()`.

Each snapshot records `encodedGraphBytes` (the loaded graph's encoded payload
sum), `decodeSeconds`, and the scene's decoded-payload statistics. These are
distinct measurements, not process memory, peak memory or load-time targets.

## GPU ownership

CPU activation does not allocate or evict GPU caches. Create an explicit
[world resource scope](WORLD_RESOURCE_SCOPES.md), pass its handle through
`GltfSceneDrawOptions::resourceScope`, check draw success, and request release
when its last desired draw has been submitted. A failed GPU admission may have
allocated earlier resources; release that scope. The adapter validates CPU
bindings before drawing, balances scope begin/end, and surfaces native scope
failures. It cannot roll back already submitted draw calls.

Use a separate scope per independently releasable scene. Scope zero retains
legacy unowned cache behavior. Release runs at the next frame maintenance
boundary after GPU completion and can block. This is explicit ownership and
reclamation, not general streaming or deferred nonblocking upload scheduling.

## Verification

`TestGltfSceneSession.cpp` uses real encoded Scene/Prefab archives and authored
glTF/PNG/buffer data. It checks store/filesystem equivalence, URI boundaries,
embedded data and GLB, missing dependencies, atomic activation, failure
preservation, budget edges and retained snapshots. Condition-variable gates and
actual thread IDs establish worker execution, cancellation, stale-result
suppression, exception handling and destruction/provider lifetime.

The [scene lifecycle matrix](SCENE_FIXTURE.md) supplies the separate native
runtime/editor evidence across OpenGL, Vulkan and D3D12.
