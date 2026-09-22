# Static collision queries and character ownership

`Phlosion::Collision` is an opt-in static library backed by manifest-pinned
Jolt Physics 5.4.0 (MIT). Include `engine/collision/CollisionWorld.h` and link
`Phlosion::Collision`. Public types use C++20 and GLM; Jolt remains private to
the implementation. `Phlosion::Core` and the existing `Phlosion::Engine` link
set do not include collision. Top-level configuration still discovers the
engine's other dependencies, but collision consumers link only collision, GLM,
Jolt and standard platform/runtime libraries. Queries require no window,
graphics API, simulation update, job system, filesystem or game content.

The same opt-in world now owns fixed-step kinematic capsule characters. See
[the character-controller contract](CHARACTER_CONTROLLER.md) for movement,
settings, support, recovery and teleport. Characters are invisible to the static
query API and do not collide with one another.

```cpp
engine::collision::CollisionWorld world;
engine::collision::ColliderId wall;
engine::collision::StaticColliderDesc desc;
desc.position = {5, 1, 0};
desc.userData = 42;
std::string error;
if (!world.addBox(desc, {0.1f, 2, 3}, wall, &error)) {
    // Handle the diagnostic before using wall.
}
std::optional<engine::collision::Hit> hit;
if (world.sweepCapsule({{0, 1, 0}, 0.4f, 0.5f}, {10, 0, 0}, hit, {}, &error)
    && hit) {
    // hit->fraction is measured along the complete displacement {10, 0, 0}.
}
```

## Ownership and identity

Each noncopyable, nonmovable world belongs to the thread that constructs it.
All access, including const queries, removal and destruction, must occur there.
Wrong-thread calls throw `std::logic_error`; wrong-thread destruction violates
the destructor's noexcept contract and terminates. Separate worlds may coexist.
The implementation never advances simulation or creates worker threads.

The first world initializes a function-local process runtime. It registers
Jolt's allocator, diagnostic callbacks, factory and types once; repeated world
construction does not reinitialize them. Worlds release their bodies before
their physics system; the runtime unregisters types and destroys its own
factory at static shutdown, after worlds created through this API. It never
deletes an external factory. Do not use this service during static destruction
after the runtime itself has been destroyed.

This module requires exclusive management of Jolt globals in its linked module.
It rejects a preexisting factory or configured allocator with `std::logic_error`
instead of adopting or replacing it. Replacing its factory is detected when
constructing another world. External initialization, allocator/callback changes,
registration, shutdown and independently managed Jolt integrations are not
supported. Do not unload a module while its worlds are alive. This is not an
editor plugin ABI or a bridge between separate DLL copies of Jolt.

`ColliderId` is an opaque, comparable, nonzero 64-bit identity. IDs increase
across worlds sharing this module, never wrap and never return to the pool.
Zero, foreign and removed IDs fail `contains`/`remove` without affecting bodies.
`clear` removes characters before colliders and invalidates both kinds of ID in
the world. Collider removal refreshes live character contacts before destroying
the removed body. Removal recovers body capacity, not
identity values. Identity exhaustion raises `std::overflow_error`.

Descriptions carry rigid position and rotation, layer 0..31, sensor state and a
copied `uint64_t userData` value. User data has no pointer ownership semantics.
Finite nonzero quaternions are normalized using double-precision length; zero
and nonfinite quaternions fail. Boxes have exact half extents (no convex rounding).
Meshes copy and cook indexed triangles; input spans can be changed or freed
after insertion. Degenerate triangles, including triangles that collapse during
Jolt's mesh quantization, fail instead of being silently removed. There is no
scale, dynamic body, transform update or solid-volume interpretation for an
open triangle mesh.

## Validation and limits

| Setting | Default | Valid range |
| --- | --- | --- |
| `maxColliders` | 4096 | 1..65536 |
| `maxMeshVertices` per insertion | 1,000,000 | 3..1,000,000 |
| `maxMeshTriangles` per insertion | 1,000,000 | 1..1,000,000 |
| `maxCharacters` | 64 | 1..4096 |

Invalid constructor options throw `std::invalid_argument`. These are count
limits, not process-memory or peak-cooking-memory budgets. Allocation/resource
failure follows the allocator/library's C++ failure behavior; the API does not
promise recovery from out-of-memory conditions.

The supported numeric envelope is deliberately finite: each position, local
mesh vertex, ray/capsule center and displacement component must be in
[-1,000,000, 1,000,000], and segment endpoints must remain in that range.
Box half extents and capsule radius must be in [0.0001, 1,000,000]. Capsule
cylinder half-height is in [0, 1,000,000]; zero constructs a sphere. Segment
length must be at least 0.0001. NaN, infinity and values outside these limits
fail before Jolt is called. Meshes require at least three vertices, at least
one triangle, an index count divisible by three, and all indices in range.
Floating-point accuracy decreases with coordinate/scale; these bounds prevent
unsupported inputs, not guarantee submillimeter accuracy throughout that range.

Malformed input, capacity exhaustion and shape-cooking rejection return false
and set the optional diagnostic. Failed additions preserve `out` and existing
colliders. Failed queries preserve the previous hit/vector. Successful calls
clear the diagnostic and publish new results, including an empty optional or
vector on a miss. Query return status indicates validity, not whether a hit
occurred. `remove` returns false for missing handles. Queries do not change
world content.

## Queries and contact conventions

All three query families apply the same `QueryFilter`: a 32-bit layer mask,
sensor policy (Exclude by default, Include, or Only), and one ignored ID.
Bit 31 is supported. A zero mask is a valid empty query. An ignored stale,
foreign or zero ID has no effect. Unknown sensor enum values are invalid.

- `rayCast` uses `origin + displacement * fraction`, including the endpoint.
  It returns the nearest hit in [0,1], copied collider metadata, world point
  and a unit normal. Boxes are solid: starting inside yields fraction zero
  with the origin as point and the nearest box face's outward normal; equal
  face distances have unspecified selection. Mesh ray normals follow authored
  winding, including rays hitting the back side; a sheet has no unique exterior.
- `sweepCapsule` translates an upright Y-axis capsule without rotation. Its
  nearest hit includes the collider contact point and unit normal pointing
  out of the contacted collider toward the capsule. For mesh back sides the
  normal faces the capsule. Positive initial penetration reports fraction zero
  and `initialOverlap=true`. Touching alone need not set that flag. Ties at zero
  may select the deepest contact. Thin obstacles use continuous shape casting.
- `overlapCapsule` uses precise shape/triangle contacts, returns each collider
  once and sorts by opaque ID. It does not substitute AABB overlap for geometry.

Meshes are double-sided for every query. Ray/sweep equal-distance tie ordering
is unspecified. Normals are normalized, with +Y as a finite fallback if a
degenerate solver axis has no direction. Capsule GJK collision tolerance is
0.0001 world units; Jolt penetration tolerance is 0.0001 relative squared-depth
change. Overlap's maximum separation distance is zero (no proximity expansion),
but touching/near-touching classification still has solver/rounding tolerance.
All triangle edges participate in capsule queries. This is query information,
not a character sliding/step/slope/grounding policy. Repeatability is tested on
this host; cross-platform bitwise determinism is not promised.

Sensors are static queryable shapes. Inclusion does not create trigger
enter/exit events, character response, interaction rules or a simulation state
machine. Trigger events, camera and editor/runtime integration are later work;
fixed-step character movement is documented separately above.

## Qualified build and verification

The 2026-09-20 Windows x64 qualification uses VS 2026/MSVC 14.51, the existing
vcpkg baseline `b7ba15730cadac50ce5092223efc92972c0afc5b`, and Jolt 5.4.0#0.
The installed Jolt library is static, single precision, with no debug renderer,
samples or profiler. Its actual Ninja compile command/export does **not** add
`/arch:AVX*` or `JPH_USE_AVX*`/SSE4 definitions. The header selects baseline SSE
for x64 (MSVC's x64 SSE2 baseline); this resolved build does not require AVX2.
Cached `USE_AVX2=ON` alone does not establish an instruction requirement: that
upstream MSVC CMake branch depends on `CMAKE_VS_PLATFORM_NAME`, unset in this
vcpkg Ninja build. Reinspect actual flags when changing compiler, triplet,
generator or dependency version. Other CPU/OS configurations are unqualified.

`PhlosionCollisionTests` links only `Phlosion::Collision`, inherits engine
warning settings, and is registered as `PhlosionCollisionContracts`. Eight
test groups cover literal analytic contacts, both mesh sides, thin-wall and
endpoint casts, precise overlaps, filtering/metadata, lifecycle/capacity,
malformed input/output preservation, configuration boundaries and a repeated
floor/wall/ramp/sensor script. A translated and rotated mesh has independently
calculated world-space ray/capsule fractions, points and normals, plus overlap
and nearby-miss checks. Thread tests reject wrong-thread reads/queries, preserve
query output, and create/use/destroy an independent world on another thread
while the first remains alive and usable on its owner. They do not create a
graphics context.

```powershell
cmake -S . -B build-presentation
cmake --build build-presentation --config Debug --parallel 8
ctest --test-dir build-presentation -C Debug --output-on-failure
```

The local evidence and actual executable/link dependencies are recorded in
`artifacts/m2-a/` and the M2-A worker report. No renderer or visual parity claim
is made by these CPU tests.
