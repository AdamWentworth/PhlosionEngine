# Managed world resource scopes

`IWorldResourceScope.h` supplies a separate optional interface. Obtain it with
`dynamic_cast<IWorldResourceScope*>(&backend)`. The existing `IRenderBackend`
virtual interface and editor plugin ABI are unchanged. OpenGL, Vulkan and D3D12
implement the interface. All calls use the renderer's owner thread and context.

```cpp
auto* scopes = dynamic_cast<IWorldResourceScope*>(&backend);
if (!scopes) return false;
const auto scene = scopes->createWorldResourceScope(64ull * 1024 * 1024);
if (!scene || !scopes->beginWorldResourceScope(scene)) return false;
// Prewarm/draw cached indexed world geometry and its material textures here.
scopes->endWorldResourceScope();
engine::render::WorldResourceScopeStats stats;
const bool healthy = scopes->getWorldResourceScopeStats(scene, stats) &&
    stats.status == engine::render::WorldResourceScopeStatus::Ready;
// After the last desired draw, including inside an editor preview callback:
scopes->requestWorldResourceScopeRelease(scene);
```

Handles are monotonically unique across native renderer instances. Zero denotes
legacy unowned drawing, is never returned for a successful managed allocation,
and cannot be begun or released. A scope handle contains no backend pointer and
does not itself retain resources. Scope creation returns zero on capacity or
metadata allocation failure. Begin/end are non-nested and return false for
invalid operations. Missing, foreign, failed and pending scopes cannot be begun.
Draw/prewarm failures set a sticky status and diagnostic, so callers must check
scope health after submitting work. An unsuccessful managed cache allocation
does not silently succeed through the legacy transient geometry path.

The required ownership path is immutable cached indexed world geometry and its
cached material textures. Geometry and texture identities are namespaced before
lookup; the same content/key in two scopes has independent native ownership.
Geometry keys remain immutable within a scope. Legacy keys are unchanged.
Direct3D 12 stores cached meshes in the GPU default heap, including managed
scopes. Temporary upload buffers are released after their copy fence completes;
normal scope retirement still waits for queued rendering. For timing comparisons,
`PHLOSION_D3D12_MESH_CACHE_DEFAULT_HEAP=0` retains the upload-heap path. This does
not change dynamic/transient geometry allocation.
Environment and fallback textures are shared, unowned, and excluded. Sprite,
editor surface, transient geometry/instance/skinning, pipeline and frame resources
are outside this ownership ledger. Dynamic geometry should not be used as an
alternative to the managed cached geometry contract.

Release immediately closes admission, including when requested inside a draw
callback. It does not delete objects or modify submitted GPU work. The next
`beginFrame` performs collection before recording new work:

* OpenGL calls `glFinish`, checks the GL error result, deletes owned VAOs,
  buffers and textures, and invalidates cached bindings.
* Vulkan requires successful `vkDeviceWaitIdle`, frees owned material descriptor
  sets, removes material pointers, compacts/rebuilds indexed material tables for
  every frame, then destroys owned geometry allocations and images. Managed
  geometry uses dedicated allocations; it never consumes the legacy 64 MB
  append-only geometry arena.
* D3D12 signals the queue fence and checks completion, including device removal.
  It releases owned buffer/texture COM objects and recycles texture SRV slots and
  eight-descriptor material blocks. It never resets the shared heap high-water
  mark or recycles live editor/sprite descriptors.

This maintenance boundary can block. It is not a nonblocking streaming promise.
If GPU completion fails, resources remain pending and an error is reported.
Successfully collected handles become missing: querying them returns false.
Release also works for failed scopes. Pending scopes count toward the fixed
256-scope limit until collection. Callers must still balance begin/end; a stale
active handle rejects admission rather than changing subsequent draws to unowned
allocations.

Select material profiles outside world draw batches (Vulkan requires selection
between frames). OpenGL profile replacement waits for completion before its
existing geometry-cache teardown, debits the freed geometry from each live scope,
and invalidates bindings. The scopes and their textures stay live; later draws
can recreate geometry within the same payload cap. This cache invalidation also
appears in cumulative reclamation counts.

## Accounting and limits

`WorldResourceScopeStats::retained` and aggregate `WorldResourceStats` derive from
successfully created native cache entries. Aggregates cover **managed scopes**,
not all renderer resources. `allocated`/`reclaimed` are cumulative; `retained` and
`pendingRelease` are current. For each count/byte field, successful collection
preserves `allocated - reclaimed == retained`.

| Field | Meaning |
|---|---|
| `geometryBuffers` | Native retained buffers: two per GL/D3D12 mesh, one combined Vulkan allocation per managed mesh |
| `textures` | Owned native texture/image objects |
| `materialDescriptors` | Descriptor entries: eight per D3D12 material block or Vulkan material set; zero on GL |
| `textureDescriptors` | Standalone owned texture SRVs: one per D3D12 texture; zero on GL/Vulkan world textures |
| `payloadBytes` | Vertex/index bytes plus retained RGBA8 mip pixel bytes |
| `apiAllocationBytes` | Vulkan memory requirements; D3D12 resource allocation info; estimated logical storage on GL |

OpenGL reports `apiAllocationBytesEstimated=true`. Native descriptors have no
portable per-entry allocation byte query and contribute zero byte estimates;
their entry counts and reusable capacity are tracked separately. D3D12 descriptor
heap memory and Vulkan descriptor pool/table capacity remain shared allocations.
Free-list entries may retain bounded host metadata after collection. Vulkan
material table slots are compacted and reused, preserving live material objects.

The per-scope cap bounds retained logical geometry and mip payload, using checked
arithmetic. Driver padding, descriptor heap/pool bytes, upload staging, decoding
temporaries, CPU/container overhead, shared resources and process/driver memory
are outside the cap. API allocation bytes are not process memory measurements.
Managed malformed mip chains, native allocation failures and budget overflows
must be treated as failed scene admission, even when some earlier resources in
the same scope successfully allocated. Release the failed scope to reclaim them.

The Vulkan pool uses `VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT`, and
descriptor frees follow GPU completion as required by the
[Vulkan descriptor specification](https://docs.vulkan.org/spec/latest/chapters/descriptorsets.html).
D3D12 recycling follows
[fence-based resource management](https://learn.microsoft.com/en-us/windows/win32/direct3d12/fence-based-resource-management).

CPU scope-contract tests establish identities, limits, accounting and state
transitions. They do not establish native destruction or visual parity. Native
runtime/editor lifecycle qualification must exercise all three APIs, preserve a
simultaneously live scene, use repeated unique content, inspect native counters,
and check expected rendered content before and after retirement.
