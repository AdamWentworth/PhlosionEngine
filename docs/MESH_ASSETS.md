# Typed static mesh assets (`.phmesh`)

Status: Implemented CPU contract (M1-E)
Last updated: 2026-09-20

## Goal and boundary

[`PhlosionMeshAsset`](../src/engine/assets/phlosion/PhlosionMeshAsset.h) is the
first `.phmesh` storage and validation contract. The engine already loads and
renders meshes through Model/glTF and its model cache, samples skeletal
animations, and supports standard PBR and project material profiles. This pass
adds a typed PHRC representation for static geometry; it does not introduce
those existing rendering capabilities or migrate their production consumers.
The asset architecture assigns cooked geometry, submeshes and bounds to
`.phmesh`; this pass implements static v1 and its first consumer, the
[public asset fixture](PUBLIC_ASSET_FIXTURE.md).

Custom cooked formats are an optional asset-pipeline direction, not prerequisites
for every game. Verify and extend existing model/animation/material paths when
they meet the consuming game's needs.

Static v1 covers one geometry level of indexed triangles, material slot labels
and bounding boxes. It is model-local, right-handed Y-up and deliberately leaves
transforms, culling, animation and slot bindings to consumers:

- no skinning, joints, weights, skeletons or morph targets;
- no LOD levels and no mesh compression;
- no material payloads or texture references: material slots are labels, not
  asset IDs;
- no importer, cooker CLI or production `Model`/`.pacmdl` migration.

The type lives in `engine_core`, is renderer independent, and needs no graphics
context to encode, decode or validate. Engine code never routes through
examples, tools or tests.

## CPU contract

```cpp
#include "engine/assets/phlosion/PhlosionMeshAsset.h"

engine::assets::phlosion::MeshAsset mesh;
// mesh.meshId, vertices, indices, submeshes, materialSlots, bounds

std::string error;
std::vector<std::uint8_t> bytes;
engine::assets::phlosion::encodeMeshAsset(mesh, bytes, {}, &error);

engine::assets::phlosion::MeshAsset decoded;
engine::assets::phlosion::decodeMeshAsset(bytes, decoded, {}, &error);
```

| Type or function | Meaning |
| --- | --- |
| `MeshVertex` | `position[3]`, `normal[3]`, `tangent[4]`, `texcoord[2]`, `color[4]` as `std::array<float, N>`. Defaults are zero position/UV, +Z normal, +X tangent with `w = +1` and white. |
| `MeshBounds` | Component-wise `min[3]` and `max[3]`. |
| `MeshSubmesh` | `firstIndex`, `indexCount` and `materialSlot` into `MeshAsset::materialSlots`. |
| `MeshAsset` | `meshId`, `vertices`, `indices`, `submeshes`, `materialSlots` and `bounds`. |
| `MeshAssetLimits` | Bounds on encoded bytes, vertices, indices, submeshes and material slots. |
| `validateMeshAsset` | Validates authored data only. |
| `encodeMeshAsset` | Validates and writes a `PHMS` document. |
| `decodeMeshAsset` | Decodes and validates a `PHMS` document. |
| `loadMeshAsset` | Synchronous `IAssetStore` read plus decode. |

Every function returns `bool`, takes a `std::string*` diagnostic and leaves its
output unchanged on failure. Diagnostics are cleared on success. `loadMeshAsset`
is a convenience for callers that already own a staged document; it is not
another asynchronous session.

Vertex coordinates are model-local and right-handed Y-up. Index order and
authored attributes are preserved exactly: the engine never flips axes or
winding, normalizes, clamps, drops triangles or remaps slots. Normal and tangent
`xyz` must have nonzero length but need not be unit length or orthogonal, so
source data survives round trips. Material slot names are unique nonempty labels
of at most 1024 UTF-8 bytes; they are not asset IDs, and static v1 declares no
material or skeleton dependency. A future binding contract owns that mapping.

The mesh id and every material slot label must be valid UTF-8: truncated
sequences, lone or stray continuation bytes, overlong encodings, surrogates and
codepoints above U+10FFFF are rejected with a diagnostic. Names are carried
verbatim, so an invalid byte is never replaced, and a document whose manifest
carries invalid UTF-8 fails decode through the normal diagnostic path instead of
escaping as an exception. The 1024-byte bound counts bytes, not codepoints, so a
1024-byte multibyte name is accepted and a 1025-byte one is not.

## Validation rules

Authored and decoded data pass the same checks:

- bounded nonempty `meshId`; at least three vertices and at least one triangle,
  with an index count divisible by three;
- every index inside the vertex range;
- finite position, normal, tangent, texcoord and color values, color channels in
  `[0, 1]`, tangent `w` exactly `-1` or `+1`, and nonzero-length normal and
  tangent `xyz`;
- `bounds` equal to the component-wise min/max over all vertices with no
  implicit repair;
- at least one unique nonempty material slot label and at least one submesh;
- each submesh nonempty, triangle aligned, referencing a valid slot, and the
  ordered submeshes partitioning the whole index list exactly once with no gap,
  overlap or overflow.

Degenerate triangles may be represented; geometry quality is a cooker concern.
The partition check uses a subtraction form, so no range arithmetic can wrap
even if the library is ever built for a 32-bit host.

## On-disk v1

`.phmesh` is a normal PHRC document, so it shares the container header, chunk
integrity and content hash with every other Phlosion resource. The generic PHRC
semantics are unchanged; `PHMS` is a root schema on top of them.

| Property | Value |
| --- | --- |
| Magic | `PHMS` |
| Root schema version | `1` |
| Container flags | `0` |
| Dependencies | none |
| Chunks | exactly two, ordered `VTX0` then `IDX0`, flags `0`, 16-byte alignment |
| `VTX0` | 16 little-endian `float32` components per vertex (64 bytes) in the public field order |
| `IDX0` | one little-endian `uint32` per index |

Component bits are written and read explicitly per field. Raw C++ struct bytes,
padding and native-endian `memcpy` are never used as serialization.

The manifest is a JSON object. Unknown fields are rejected, so an asset that
declares unsupported semantics such as skinning or LODs fails instead of being
silently accepted. Keys are written in the canonical sorted order the encoder
produces:

```json
{
  "bounds": {
    "max": [1.0, 1.0, 0.0],
    "min": [-1.0, -1.0, 0.0]
  },
  "cooker": "phlosion-mesh-v1",
  "coordinate_system": "right-handed-y-up",
  "counts": {
    "indices": 6,
    "material_slots": 1,
    "submeshes": 1,
    "vertices": 4
  },
  "material_slots": ["surface"],
  "mesh_id": "fixture/mesh/board",
  "root_type": "Mesh",
  "schema_version": 1,
  "submeshes": [
    {"first_index": 0, "index_count": 6, "material_slot": 0}
  ],
  "target_profile": "portable",
  "topology": "triangles",
  "vertex_layout": "pntc-f32-v1"
}
```

`cooker` records the identity of the in-repo encoder and implies no external
cooker. `target_profile` is `portable`; other profiles are rejected until a
profile contract exists.

Decode checks the container version, integrity and root schema, rejects a wrong
or unsupported magic/version/flags/dependency set, requires the two known chunks
in order with zero flags and 16-byte alignment, and rejects unknown, duplicate or
missing chunks. Manifest counts must agree with the chunk byte counts, and
integers must be integers: fractions, negatives and values outside the checked
range are rejected before narrowing.

## Limits

`MeshAssetLimits` bounds the input document and the logical decoded counts. It is
not a total peak-memory bound: PHRC and manifest copies, the caller's buffers,
vector capacity and the document bytes after a load stay outside it.

| Limit | Default | Counts |
| --- | --- | --- |
| `maxEncodedBytes` | 64 MiB | Encoded `.phmesh` document bytes for encode, decode and load |
| `maxVertices` | 1,048,576 | Decoded vertices |
| `maxIndices` | 3,145,728 | Decoded indices |
| `maxSubmeshes` | 65,536 | Decoded submeshes |
| `maxMaterialSlots` | 4,096 | Decoded material slot labels |

Exceeding any limit fails the call and leaves the output unchanged.

## First consumer

`examples/public_asset_fixture` renders only from a validated snapshot. Its six
geometry resources are typed `.phmesh` documents that the scene archive requires
as dependency edges with explicit expected PHRC content hashes, so the fixture
never reads geometry from a private file record:

- each object has a stable logical mesh id (`fixture/mesh/<role>`) and a
  versioned registry id per content variant (`fixture/mesh/<role>-a`/`-b`);
- the scene document records the mesh asset id, the logical mesh id and the
  expected document hash, and the resolver maps those ids to `AssetKind::Phrc`;
- candidate validation decodes every mesh from the staged snapshot graph before
  activation, checks the declared identity, hash and structure, and rejects the
  candidate if a typed mesh is semantically invalid;
- presentation consumes submesh ranges and material slot labels: each declared
  slot must have exactly one binding (a missing or duplicated binding is
  rejected), and a binding supplies its range's texture and material parameters;
- the renderer-facing vertex adapter forwards every static attribute the schema
  carries, including the four tangent components;
- texture files, material parameters and motion metadata stay with the fixture
  for now.

## Verification

[`TestPhlosionMeshAsset.cpp`](../tests/TestPhlosionMeshAsset.cpp) is a CPU-only
contract that covers deterministic encode and round trip, a fixed literal
little-endian golden payload (the whole first 64-byte vertex plus the complete
12-byte index payload) that the encoder cannot drift along with, multi-submesh
and material slot preservation, bounds, provider loading errors, unchanged
output on every failure path, tight custom limits, valid multibyte and
1024-byte names, malformed UTF-8 names and manifests, and re-encoded malicious
`PHMS` documents with repaired integrity for wrong
magic/version/layout/topology/profile, unknown skin or LOD semantics, missing or
wrongly typed fields, integer fractions, negatives and narrowing, non-finite
attributes, bad bounds, indices, partitions and slots, and truncated or
non-container input.

The fixture contract (`tests/TestPublicAssetFixtureRendering.cpp`) tampers typed
mesh documents with repaired container integrity, proves that a mesh whose hash
constraint no longer matches fails the dependency graph, and includes a CPU
regression that a candidate failing typed mesh validation leaves the active A
snapshot untouched. It also drives a real two-submesh/two-slot candidate through
validation and activation, checks that the consumer maps each submesh to its own
slot binding, rejects duplicated and missing bindings while keeping A active,
and asserts the world-vertex adapter forwards all attributes including
`tangent.w`. No test in the engine suite needs a graphics context.

See [VERIFICATION.md](VERIFICATION.md) for the current suite scope and dated
evidence.

## Deferred work

Still out of scope: skeletons, skinning and animation, LODs and compression,
material payloads and slot bindings, importers and cook CLIs, production
`Model`/`.pacmdl` migration, streaming or ranged reads, `.phv` packaging, and any
renderer-side adoption beyond consumers that already draw indexed meshes.
