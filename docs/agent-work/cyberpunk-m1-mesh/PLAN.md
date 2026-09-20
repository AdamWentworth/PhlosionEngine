# M1-E: typed static mesh resource and its first consumer

Date: 2026-09-20. Astra design; dispatch only after the M1-D organization review
and authorized commit. Record that committed baseline in CHECKPOINT.md.

## Objective and boundary

Implement the first production `.phmesh` contract in engine_core and use it in
the public fixture. The asset architecture already assigns cooked geometry,
submeshes and bounds to `.phmesh`, but current source has only Model/glTF/cache
and draw-carrier types; no typed PHRC mesh decoder exists. M1-D's PGEO geometry
is deliberately example-specific. Replace that geometry boundary with reusable,
validated engine data instead of promoting the whole example into the engine.

This pass covers static indexed triangles, one geometry level, material slots
and bounding boxes. Skinning/skeletons/animation, LODs, compression, material
payloads, importers/cook CLIs and production Model/cache migration are separate
passes. No renderer/shader/editor-host API, game or package changes are needed.
Keep the existing Model/glTF path compatible and do not route engine code through
examples/tools/tests. Engine core remains free of graphics-context requirements.

## Public contract

Add `src/engine/assets/phlosion/PhlosionMeshAsset.h/.cpp` in namespace
`engine::assets::phlosion`, linked to engine_core. Use plain CPU/std types:

- `MeshVertex`: float arrays position[3], normal[3], tangent[4], texcoord[2],
  color[4]. Defaults: position/UV zero, normal +Z, tangent +X with w=+1,
  color white. No joint/weight fields in static v1.
- `MeshBounds`: float arrays min[3], max[3].
- `MeshSubmesh`: uint32 firstIndex, indexCount, materialSlot.
- `MeshAsset`: string meshId; vectors of MeshVertex, uint32 indices,
  MeshSubmesh and string materialSlots; MeshBounds bounds.
- `MeshAssetLimits`: maxEncodedBytes=64 MiB, maxVertices=1,048,576,
  maxIndices=3,145,728, maxSubmeshes=65,536, maxMaterialSlots=4,096.
- `validateMeshAsset(asset, limits={}, error=nullptr)`,
  `encodeMeshAsset(asset, outBytes, limits={}, error=nullptr)`,
  `decodeMeshAsset(bytes, outAsset, limits={}, error=nullptr)`,
  `loadMeshAsset(store, virtualPath, outAsset, limits={}, error=nullptr)`.
  Return bool; use const input references and `std::string*` diagnostics,
  matching existing asset APIs. `load` is a synchronous IAssetStore convenience,
  not another async scheduler. Encode/decode/load leave output unchanged on
  failure and clear diagnostics on success. Do not partially publish invalid data.

Vertex coordinates are model-local, right-handed Y-up. Preserve index order and
authored attributes; never silently flip axes/winding, normalize, clamp, drop
triangles or remap slots. Consumers supply transforms, culling and slot bindings.
Material slot names are unique nonempty labels, not asset IDs. Material/skeleton
dependencies are not part of static v1; a future binding contract owns them.

Validate both authored and decoded data: nonempty bounded mesh ID and slot names
(at most 1024 UTF-8 bytes each); at least three vertices, nonempty triangle index
list divisible by three; each index in range; finite vertex/bounds values;
tangent w exactly -1/+1; color channels in [0,1]; finite normal/tangent xyz with
nonzero length. Normal/tangent xyz need not be unit length or orthogonal: preserve
source data, document this limit. Bounds must equal component-wise min/max over
all vertices, with no implicit repair. Each nonempty submesh is triangle aligned,
references a valid slot, and the ordered submeshes partition the full index list
exactly once (no gaps/overlap/overflow). Require at least one material slot and
submesh, reject duplicate labels, and enforce caller limits with checked sizes.
Degenerate triangles may be represented; geometry quality is a cooker concern.

## On-disk v1

Use existing PHRC encoding/integrity with magic `PHMS`, root schemaVersion 1,
flags 0 and no dependencies. Manifest root is an object with `root_type="Mesh"`,
`schema_version=1`, `mesh_id`, `vertex_layout="pntc-f32-v1"`,
`coordinate_system="right-handed-y-up"`, `topology="triangles"`, counts,
`bounds` min/max arrays, material slot labels and submesh records. Include
`cooker="phlosion-mesh-v1"` and `target_profile="portable"`; do not imply an
external cooker exists. Reject unsupported version/layout/topology/profile and
malformed/missing/wrong-type fields. Integers must be integer, nonnegative and
range-checked before narrowing; reject fractions and wraparound. Explicitly
reject unsupported semantics such as skin/LOD declarations, rather than silently
accepting an asset requiring those features. Document the exact manifest schema.

Exactly two chunks, ordered `VTX0` then `IDX0`, flags 0/alignment16. VTX0 is
16 float32 components per vertex (64 bytes), in the public field order above;
IDX0 is uint32 per index. Encode explicit little-endian component bits, never
raw C++ struct bytes/padding or native-endian memcpy as serialization. Validate
chunk types/counts/flags, exact lengths, header/manifest schema agreement and
PHRC integrity before returning data. Unknown/duplicate/missing chunks fail.
Do not modify generic PHRC semantics to support this schema. Limits bound input
and logical decoded counts, not total peak allocations of PHRC/JSON copies.

## First consumer: real scene dependency graph

Migrate the public fixture's six geometry resources from embedded PGEO to typed
PHMS assets required by its PHSC root. Keep textures/material/motion metadata in
the fixture for now. Use explicit versioned registry IDs per A/B mesh resource,
with a stable logical meshId per object. Declare expected PHRC content hashes on
scene-to-mesh dependency edges, resolve to `AssetKind::Phrc`, and decode each
mesh from the staged snapshot graph. All data required to render comes from that
validated snapshot; no extra provider read or fallback after activation.

The fixture scene JSON gets its own schema-version increment and records mesh
asset references instead of private geometry-file records. Remove PGEO encoding/
decoding and associated raw-struct serialization. Adapt tamper tests to mutate
typed mesh documents while repairing container integrity so semantic validation
is genuinely exercised. A failed semantic mesh candidate must preserve active A;
add a CPU regression for that boundary. Retain missing-dependency failure and
successful B replacement/caching behavior. Consume submesh ranges/slot mapping
in presentation, even if each current visual object uses one submesh, and give
the CPU contract a meaningful multi-submesh asset.

Rendered scene/camera/motion/colors and the accepted guards are unchanged. Run
the same four cases on three native APIs in runtime and real editor (24 images),
with original thresholds, expected-content and source/binary/frame provenance.
Compare against the accepted M1-D captures where practical to prove migration
preserves appearance. CPU source-asset and schema failures are not GPU claims.

## Tests and acceptance

Add `tests/TestPhlosionMeshAsset.cpp` to the existing C++ contracts. Cover:
deterministic encode and round trip, a fixed golden little-endian vertex/index
payload independent of the encoder, multi-submesh/slot preservation, bounds,
provider loading errors, unchanged output on all failure paths, and tight custom
limits. Re-encode malicious PHRC documents with valid hashes to test wrong magic/
version/layout/chunks/counts, integer narrowing/fractions, nonfinite attributes,
bad bounds/indices/partitions/slots, unknown skin/LOD semantics and truncation.
Do not weaken existing PHRC or fixture tests to accommodate the migration.

Build all targets with warnings as errors in build-presentation Debug, run all
CTest entries, run the relocated public capture runner's complete GPU matrix,
inspect visuals and retain logs in ignored artifacts/m1-e/. Verify the mesh
decoder links through Phlosion::Core and its tests need no graphics context.
No repeated GPU runs after passing unless code or evidence changes justify them.

Worker owns the new production mesh files, CMake/test registration, new mesh
tests, affected example fixture code and its existing tests, fixture/mesh guides,
and REPORT.md in this phase directory. Tools/image guards may only receive
necessary metadata/path adaptation; no threshold/content weakening. Root owns
PLAN/CHECKPOINT, roadmap, verification/index and asset-architecture status update.
One native astra_flash_builder; preserve others' edits, no recursive agents,
commits/pushes/dependencies or writes to other repositories. Return concrete
architecture blockers to Astra. Allow a coherent implementation/test/visual loop
up to an hour, with a checkpoint only at a real blocker/host limit. Astra reviews
the actual patch/evidence and assigns acceptance.
