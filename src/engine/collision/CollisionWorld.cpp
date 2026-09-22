#include "CollisionWorld.h"

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/StateRecorderImpl.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <limits>
#include <map>
#include <stdexcept>
#include <thread>

namespace engine::collision {
namespace {
constexpr float maxCoordinate = 1'000'000.0f;
constexpr float minDimension = 0.0001f;
constexpr float collisionTolerance = 0.0001f;
constexpr float characterPadding = 0.005f;
constexpr float maxCharacterCoordinate = 10'000.0f;

bool fail(std::string* error, const char* message) {
    if (error) *error = message;
    return false;
}
void succeed(std::string* error) { if (error) error->clear(); }
bool bounded(float value) { return std::isfinite(value) && std::abs(value) <= maxCoordinate; }
bool bounded(const glm::vec3& value) { return bounded(value.x) && bounded(value.y) && bounded(value.z); }
bool dimension(float value) { return bounded(value) && value >= minDimension; }
bool range(float value, float low, float high) { return std::isfinite(value) && value >= low && value <= high; }
bool characterPosition(const glm::vec3& p) {
    return range(p.x, -maxCharacterCoordinate, maxCharacterCoordinate)
        && range(p.y, -maxCharacterCoordinate, maxCharacterCoordinate)
        && range(p.z, -maxCharacterCoordinate, maxCharacterCoordinate);
}
bool validCharacter(const CharacterSettings& s, const glm::vec3& center, std::string* error) {
    if (!characterPosition(center) || !range(s.radius, 0.05f, 5.0f)
        || !range(s.cylinderHalfHeight, 0.0f, 10.0f)
        || !range(s.walkSpeed, 0.0f, 100.0f) || !range(s.runSpeed, s.walkSpeed, 100.0f)
        || !range(s.gravity, 0.0f, 100.0f) || !range(s.terminalFallSpeed, 0.01f, 100.0f)
        || !range(s.maxSlopeDegrees, 1.0f, 89.0f) || !range(s.stepHeight, 0.0f, 2.0f)
        || !range(s.floorSnapDistance, 0.0f, 2.0f)
        || !range(s.fixedDeltaSeconds, 1.0f / 240.0f, 1.0f / 15.0f))
        return fail(error, "Character settings/center outside documented finite ranges (CHARACTER_CONTROLLER.md)");
    return true;
}
JPH::Vec3 vec(const glm::vec3& v) { return {v.x, v.y, v.z}; }
glm::vec3 vec(JPH::Vec3Arg v) { return {v.GetX(), v.GetY(), v.GetZ()}; }
glm::vec3 unit(const glm::vec3& v) {
    const double length = glm::length(glm::dvec3(v));
    return length > 0.0 && std::isfinite(length)
        ? glm::vec3(glm::dvec3(v) / length) : glm::vec3(0.0f, 1.0f, 0.0f);
}
bool validDesc(const StaticColliderDesc& desc, JPH::Quat& rotation, std::string* error) {
    const glm::dquat q(desc.rotation);
    const double length = glm::length(q);
    if (!bounded(desc.position) || !std::isfinite(length) || length == 0.0 || desc.layer > 31)
        return fail(error, "Invalid collider position, quaternion or layer (expected 0..31)");
    const glm::quat normalized(q / length);
    rotation = JPH::Quat(normalized.x, normalized.y, normalized.z, normalized.w);
    return true;
}
bool validCapsule(const Capsule& capsule, std::string* error) {
    if (!bounded(capsule.center) || !dimension(capsule.radius)
        || !bounded(capsule.cylinderHalfHeight) || capsule.cylinderHalfHeight < 0.0f)
        return fail(error, "Invalid capsule center, radius or cylinder half-height");
    return true;
}
bool validDisplacement(const glm::vec3& origin, const glm::vec3& displacement, std::string* error) {
    if (!bounded(origin) || !bounded(displacement) || !bounded(origin + displacement)
        || glm::length(glm::dvec3(displacement)) < minDimension)
        return fail(error, "Invalid segment: finite bounded endpoints and displacement length >= 0.0001 required");
    return true;
}
bool validFilter(const QueryFilter& filter, std::string* error) {
    switch (filter.sensors) {
    case SensorPolicy::Exclude: case SensorPolicy::Include: case SensorPolicy::Only: return true;
    }
    return fail(error, "Invalid sensor policy");
}

// The function-local runtime is constructed before any world and destroyed
// after its worlds (including static-storage worlds). No simulation workers run.
class Runtime {
public:
    Runtime() {
        if (JPH::Factory::sInstance != nullptr)
            throw std::logic_error("CollisionWorld requires exclusive ownership of Jolt globals");
#ifndef JPH_DISABLE_CUSTOM_ALLOCATOR
        if (JPH::Allocate || JPH::Reallocate || JPH::Free || JPH::AlignedAllocate || JPH::AlignedFree)
            throw std::logic_error("CollisionWorld cannot adopt externally configured Jolt allocators");
#endif
        JPH::RegisterDefaultAllocator();
        JPH::Trace = [](const char* format, ...) {
            va_list args;
            va_start(args, format);
            std::vfprintf(stderr, format, args);
            va_end(args);
            std::fputc('\n', stderr);
        };
#ifdef JPH_ENABLE_ASSERTS
        JPH::AssertFailed = [](const char* expression, const char* message, const char* file, JPH::uint line) {
            std::fprintf(stderr, "Jolt assertion: %s (%s) at %s:%u\n", expression, message ? message : "", file, line);
            return true;
        };
#endif
        factory_ = std::make_unique<JPH::Factory>();
        JPH::Factory::sInstance = factory_.get();
        JPH::RegisterTypes();
    }
    ~Runtime() {
        if (JPH::Factory::sInstance == factory_.get()) {
            JPH::UnregisterTypes();
            JPH::Factory::sInstance = nullptr;
        }
    }
    void verify() const {
        if (JPH::Factory::sInstance != factory_.get())
            throw std::logic_error("External code replaced CollisionWorld's Jolt factory");
    }
private:
    std::unique_ptr<JPH::Factory> factory_;
};
void ensureRuntime() {
    static Runtime runtime;
    runtime.verify();
}
std::uint64_t nextIdentity() {
    static std::atomic<std::uint64_t> next{1};
    auto candidate = next.load(std::memory_order_relaxed);
    for (;;) {
        if (candidate == std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error("CollisionWorld identity space exhausted");
        if (next.compare_exchange_weak(candidate, candidate + 1, std::memory_order_relaxed)) return candidate;
    }
}
class BroadLayers final : public JPH::BroadPhaseLayerInterface {
    JPH::uint GetNumBroadPhaseLayers() const override { return 1; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer) const override { return JPH::BroadPhaseLayer(0); }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer) const override { return "Static"; }
#endif
};
class BodyQueryFilter final : public JPH::BodyFilter {
public:
    BodyQueryFilter(const QueryFilter& filter, std::uint64_t ignored) : filter_(filter), ignored_(ignored) {}
    bool ShouldCollideLocked(const JPH::Body& body) const override {
        return body.GetUserData() != ignored_
            && (filter_.layerMask & (std::uint32_t{1} << body.GetObjectLayer())) != 0
            && (filter_.sensors == SensorPolicy::Include
                || (body.IsSensor() == (filter_.sensors == SensorPolicy::Only)));
    }
private:
    const QueryFilter& filter_;
    std::uint64_t ignored_;
};
} // namespace

struct CollisionWorld::Impl {
    struct Entry { JPH::BodyID body; ColliderInfo info; };
    struct CharacterEntry {
        CharacterSettings settings;
        std::unique_ptr<JPH::CharacterVirtual> solver;
        glm::vec3 velocity{0.0f};
        float fallVelocity = 0.0f;
    };
    CollisionWorldOptions options;
    std::thread::id owner = std::this_thread::get_id();
    BroadLayers broadLayers;
    JPH::ObjectVsBroadPhaseLayerFilter broadFilter;
    JPH::ObjectLayerPairFilter pairFilter;
    JPH::PhysicsSystem physics;
    std::map<ColliderId, Entry> entries;
    JPH::TempAllocatorMalloc characterAllocator;
    std::map<CharacterId, CharacterEntry> characters;

    explicit Impl(const CollisionWorldOptions& settings) : options(settings) {
        // Jolt's unused simulation caches still require at least four buckets.
        physics.Init(options.maxColliders, 0, 4, 4, broadLayers, broadFilter, pairFilter);
    }
    void checkOwner() const {
        if (owner != std::this_thread::get_id()) throw std::logic_error("CollisionWorld is an owner-thread API");
    }
    bool capacity(std::string* error) const {
        return entries.size() < options.maxColliders || fail(error, "Collider capacity reached");
    }
    bool insert(const StaticColliderDesc& desc, JPH::QuatArg rotation,
                const JPH::Shape* shape, ColliderId& out, std::string* error) {
        const ColliderId id(nextIdentity());
        // Allocate metadata before adding a body so allocation failure cannot orphan it.
        const auto it = entries.emplace(id, Entry{JPH::BodyID(), {id, desc.userData, desc.layer, desc.sensor}}).first;
        JPH::BodyCreationSettings settings(shape, JPH::RVec3(vec(desc.position)), rotation,
                                           JPH::EMotionType::Static, static_cast<JPH::ObjectLayer>(desc.layer));
        settings.mIsSensor = desc.sensor;
        settings.mUserData = id.value_;
        auto& bodies = physics.GetBodyInterface();
        JPH::Body* body = bodies.CreateBody(settings);
        if (!body) {
            entries.erase(it);
            return fail(error, "Jolt body capacity reached");
        }
        it->second.body = body->GetID();
        bodies.AddBody(body->GetID(), JPH::EActivation::DontActivate);
        out = id;
        succeed(error);
        return true;
    }
    ColliderInfo info(JPH::BodyID body) const {
        const auto id = ColliderId(physics.GetBodyInterface().GetUserData(body));
        return entries.at(id).info;
    }
    void refresh(CharacterEntry& entry) {
        const QueryFilter filter{entry.settings.layerMask, SensorPolicy::Exclude, entry.settings.ignore};
        const BodyQueryFilter bodyFilter(filter, filter.ignore.value_);
        entry.solver->RefreshContacts({}, {}, bodyFilter, {}, characterAllocator);
    }
    CharacterState state(const CharacterEntry& entry) const {
        const auto& solver = *entry.solver;
        CharacterState result;
        result.center = vec(solver.GetPosition());
        result.velocity = entry.velocity;
        switch (solver.GetGroundState()) {
        case JPH::CharacterBase::EGroundState::OnGround: result.groundState = CharacterGroundState::Grounded; break;
        case JPH::CharacterBase::EGroundState::OnSteepGround: result.groundState = CharacterGroundState::Steep; break;
        case JPH::CharacterBase::EGroundState::NotSupported: result.groundState = CharacterGroundState::Unsupported; break;
        case JPH::CharacterBase::EGroundState::InAir: break;
        }
        // Resolve copied identity rather than dereferencing a potentially removed Jolt body.
        auto support = entries.find(ColliderId(solver.GetGroundUserData()));
        // Jolt's rollback format deliberately omits user data. Recover it from live metadata,
        // comparing full body IDs (including their generation) without touching a body handle.
        if (support == entries.end() && !solver.GetGroundBodyID().IsInvalid())
            support = std::find_if(entries.begin(), entries.end(), [&](const auto& item) {
                return item.second.body == solver.GetGroundBodyID();
            });
        if (result.groundState != CharacterGroundState::InAir && support != entries.end()
            && support->second.body == solver.GetGroundBodyID()) {
            result.support = support->second.info;
            result.groundPoint = vec(solver.GetGroundPosition());
            result.groundNormal = unit(vec(solver.GetGroundNormal()));
        } else {
            result.groundState = CharacterGroundState::InAir;
        }
        return result;
    }
};

CollisionWorld::CollisionWorld(const CollisionWorldOptions& options) {
    if (options.maxColliders == 0 || options.maxColliders > 65536
        || options.maxMeshVertices < 3 || options.maxMeshVertices > 1'000'000
        || options.maxMeshTriangles == 0 || options.maxMeshTriangles > 1'000'000
        || options.maxCharacters == 0 || options.maxCharacters > 4096)
        throw std::invalid_argument("CollisionWorld limits: colliders 1..65536, mesh vertices 3..1000000, triangles 1..1000000, characters 1..4096");
    ensureRuntime();
    impl_ = std::make_unique<Impl>(options);
}
CollisionWorld::~CollisionWorld() { clear(); }

bool CollisionWorld::addBox(const StaticColliderDesc& desc, const glm::vec3& halfExtents,
                            ColliderId& out, std::string* error) {
    impl_->checkOwner();
    JPH::Quat rotation;
    if (!validDesc(desc, rotation, error) || !impl_->capacity(error)) return false;
    if (!dimension(halfExtents.x) || !dimension(halfExtents.y) || !dimension(halfExtents.z))
        return fail(error, "Box half extents must be finite and in [0.0001, 1000000]");
    const auto shape = JPH::BoxShapeSettings(vec(halfExtents), 0.0f).Create();
    if (shape.HasError()) return fail(error, shape.GetError().c_str());
    return impl_->insert(desc, rotation, shape.Get(), out, error);
}

bool CollisionWorld::addTriangleMesh(const StaticColliderDesc& desc,
                                     std::span<const glm::vec3> vertices,
                                     std::span<const std::uint32_t> indices,
                                     ColliderId& out, std::string* error) {
    impl_->checkOwner();
    JPH::Quat rotation;
    if (!validDesc(desc, rotation, error) || !impl_->capacity(error)) return false;
    if (vertices.size() < 3 || vertices.size() > impl_->options.maxMeshVertices
        || indices.empty() || indices.size() % 3 != 0 || indices.size() / 3 > impl_->options.maxMeshTriangles)
        return fail(error, "Mesh requires indexed triangles within configured vertex/triangle limits");
    JPH::MeshShapeSettings settings;
    settings.mTriangleVertices.reserve(vertices.size());
    settings.mIndexedTriangles.reserve(indices.size() / 3);
    for (const auto& vertex : vertices) {
        if (!bounded(vertex)) return fail(error, "Mesh vertex must be finite and within coordinate limits");
        settings.mTriangleVertices.emplace_back(vertex.x, vertex.y, vertex.z);
    }
    for (std::size_t i = 0; i < indices.size(); i += 3) {
        if (indices[i] >= vertices.size() || indices[i + 1] >= vertices.size() || indices[i + 2] >= vertices.size())
            return fail(error, "Mesh triangle index is out of range");
        JPH::IndexedTriangle triangle(indices[i], indices[i + 1], indices[i + 2], 0);
        if (triangle.IsDegenerate(settings.mTriangleVertices)) return fail(error, "Mesh triangle is degenerate");
        settings.mIndexedTriangles.push_back(triangle);
    }
    // Deliberately do not Sanitize: invalid/quantization-degenerate geometry
    // must fail, never silently disappear from the caller's collision mesh.
    const auto shape = settings.Create();
    if (shape.HasError()) return fail(error, shape.GetError().c_str());
    return impl_->insert(desc, rotation, shape.Get(), out, error);
}

bool CollisionWorld::remove(ColliderId id) {
    impl_->checkOwner();
    const auto it = impl_->entries.find(id);
    if (it == impl_->entries.end()) return false;
    auto& bodies = impl_->physics.GetBodyInterface();
    bodies.RemoveBody(it->second.body);
    // Drop all cached contacts while the removed body still exists but is no longer queryable.
    for (auto& [characterId, character] : impl_->characters) {
        (void)characterId;
        impl_->refresh(character);
    }
    bodies.DestroyBody(it->second.body);
    impl_->entries.erase(it);
    return true;
}
void CollisionWorld::clear() {
    impl_->checkOwner();
    impl_->characters.clear();
    while (!impl_->entries.empty()) remove(impl_->entries.begin()->first);
}
bool CollisionWorld::contains(ColliderId id) const {
    impl_->checkOwner();
    return impl_->entries.contains(id);
}
std::size_t CollisionWorld::colliderCount() const {
    impl_->checkOwner();
    return impl_->entries.size();
}

bool CollisionWorld::createCharacter(const CharacterSettings& settings, const glm::vec3& center,
                                     CharacterId& out, std::string* error) {
    impl_->checkOwner();
    if (!validCharacter(settings, center, error)) return false;
    if (impl_->characters.size() >= impl_->options.maxCharacters) return fail(error, "Character capacity reached");
    const auto shape = JPH::CapsuleShapeSettings(settings.cylinderHalfHeight, settings.radius).Create();
    if (shape.HasError()) return fail(error, shape.GetError().c_str());
    JPH::CharacterVirtualSettings config;
    config.mShape = shape.Get();
    config.mMaxSlopeAngle = JPH::DegreesToRadians(settings.maxSlopeDegrees);
    config.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), settings.cylinderHalfHeight);
    config.mCharacterPadding = characterPadding;
    // Jolt adds padding along up to its shape transform. Cancel that offset to retain Capsule's center convention.
    config.mShapeOffset = JPH::Vec3(0.0f, -characterPadding, 0.0f);
    config.mCollisionTolerance = collisionTolerance;
    config.mEnhancedInternalEdgeRemoval = true;
    const CharacterId id(nextIdentity());
    Impl::CharacterEntry entry{settings, std::make_unique<JPH::CharacterVirtual>(
        &config, JPH::RVec3(vec(center)), JPH::Quat::sIdentity(), &impl_->physics)};
    impl_->refresh(entry);
    impl_->characters.emplace(id, std::move(entry));
    out = id;
    succeed(error);
    return true;
}

bool CollisionWorld::removeCharacter(CharacterId id) {
    impl_->checkOwner();
    return impl_->characters.erase(id) != 0;
}

std::size_t CollisionWorld::characterCount() const {
    impl_->checkOwner();
    return impl_->characters.size();
}

bool CollisionWorld::characterState(CharacterId id, CharacterState& out, std::string* error) const {
    impl_->checkOwner();
    const auto it = impl_->characters.find(id);
    if (it == impl_->characters.end()) return fail(error, "Unknown character ID");
    out = impl_->state(it->second);
    succeed(error);
    return true;
}

bool CollisionWorld::stepCharacter(CharacterId id, const CharacterInput& input, CharacterState& out, std::string* error) {
    impl_->checkOwner();
    const auto it = impl_->characters.find(id);
    if (it == impl_->characters.end()) return fail(error, "Unknown character ID");
    if (!std::isfinite(input.move.x) || !std::isfinite(input.move.y)) return fail(error, "Character movement must be finite");
    auto& entry = it->second;
    auto& solver = *entry.solver;
    const auto& s = entry.settings;
    const auto before = impl_->state(entry);
    const glm::dvec2 direction = glm::dvec2(input.move) / std::max(1.0, glm::length(glm::dvec2(input.move)));
    const float speed = input.run ? s.runSpeed : s.walkSpeed;
    const float vertical = before.groundState == CharacterGroundState::Grounded ? 0.0f : entry.fallVelocity;
    glm::vec3 velocity(static_cast<float>(direction.x * speed),
        std::max(-s.terminalFallSpeed, vertical - s.gravity * s.fixedDeltaSeconds),
        static_cast<float>(direction.y * speed));
    const bool grounded = before.groundState == CharacterGroundState::Grounded;
    const auto endpoint = before.center + velocity * s.fixedDeltaSeconds;
    if (!characterPosition(endpoint)
        || (grounded && (!characterPosition(endpoint + glm::vec3(0, s.stepHeight, 0))
            || !characterPosition(endpoint - glm::vec3(0, s.floorSnapDistance, 0)))))
        return fail(error, "Character tick endpoint exceeds coordinate envelope");
    JPH::StateRecorderImpl saved;
    solver.SaveState(saved);
    solver.SetLinearVelocity(vec(velocity));
    const QueryFilter filter{s.layerMask, SensorPolicy::Exclude, s.ignore};
    const BodyQueryFilter bodyFilter(filter, filter.ignore.value_);
    JPH::CharacterVirtual::ExtendedUpdateSettings update;
    update.mStickToFloorStepDown = JPH::Vec3(0.0f, grounded ? -s.floorSnapDistance : 0.0f, 0.0f);
    update.mWalkStairsStepUp = JPH::Vec3(0.0f, grounded ? s.stepHeight : 0.0f, 0.0f);
    solver.ExtendedUpdate(s.fixedDeltaSeconds, JPH::Vec3(0.0f, -s.gravity, 0.0f),
        update, {}, {}, bodyFilter, {}, impl_->characterAllocator);
    auto result = impl_->state(entry);
    result.velocity = (result.center - before.center) / s.fixedDeltaSeconds;
    if (!characterPosition(result.center) || !bounded(result.velocity) || !bounded(result.groundPoint)) {
        saved.Rewind();
        solver.RestoreState(saved);
        return fail(error, "Character solver result exceeds finite numeric envelope; tick was restored");
    }
    entry.velocity = result.velocity;
    entry.fallVelocity = result.groundState == CharacterGroundState::Grounded ? 0.0f : velocity.y;
    out = result;
    succeed(error);
    return true;
}

bool CollisionWorld::teleportCharacter(CharacterId id, const glm::vec3& center, std::string* error) {
    impl_->checkOwner();
    const auto it = impl_->characters.find(id);
    if (it == impl_->characters.end()) return fail(error, "Unknown character ID");
    if (!characterPosition(center)) return fail(error, "Character destination exceeds finite coordinate envelope");
    auto& entry = it->second;
    const QueryFilter filter{entry.settings.layerMask, SensorPolicy::Exclude, entry.settings.ignore};
    const BodyQueryFilter bodyFilter(filter, filter.ignore.value_);
    JPH::CollideShapeSettings settings;
    settings.mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;
    settings.mCollisionTolerance = collisionTolerance;
    settings.mActiveEdgeMode = JPH::EActiveEdgeMode::CollideWithAll;
    JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> contacts;
    impl_->physics.GetNarrowPhaseQuery().CollideShape(entry.solver->GetShape(), JPH::Vec3::sReplicate(1.0f),
        JPH::RMat44::sTranslation(JPH::RVec3(vec(center))), settings, JPH::RVec3(vec(center)),
        contacts, {}, {}, bodyFilter);
    for (const auto& hit : contacts.mHits)
        if (hit.mPenetrationDepth > collisionTolerance)
            return fail(error, "Character destination penetrates blocking geometry");
    entry.solver->SetPosition(JPH::RVec3(vec(center)));
    entry.solver->SetLinearVelocity(JPH::Vec3::sZero());
    entry.velocity = glm::vec3(0.0f);
    entry.fallVelocity = 0.0f;
    impl_->refresh(entry);
    succeed(error);
    return true;
}

bool CollisionWorld::rayCast(const glm::vec3& origin, const glm::vec3& displacement,
                             std::optional<Hit>& out, const QueryFilter& filter, std::string* error) const {
    impl_->checkOwner();
    if (!validDisplacement(origin, displacement, error) || !validFilter(filter, error)) return false;
    JPH::RayCastSettings settings;
    settings.mBackFaceModeTriangles = JPH::EBackFaceMode::CollideWithBackFaces;
    JPH::ClosestHitCollisionCollector<JPH::CastRayCollector> collector;
    const BodyQueryFilter bodyFilter(filter, filter.ignore.value_);
    const JPH::RRayCast ray(JPH::RVec3(vec(origin)), vec(displacement));
    impl_->physics.GetNarrowPhaseQuery().CastRay(ray, settings, collector, {}, {}, bodyFilter);
    std::optional<Hit> result;
    if (collector.HadHit()) {
        const auto& hit = collector.mHit;
        Hit value;
        value.collider = impl_->info(hit.mBodyID);
        value.fraction = std::clamp(hit.mFraction, 0.0f, 1.0f);
        value.point = origin + displacement * value.fraction;
        const JPH::BodyLockRead lock(impl_->physics.GetBodyLockInterface(), hit.mBodyID);
        value.normal = unit(vec(lock.GetBody().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, JPH::RVec3(vec(value.point)))));
        result = value;
    }
    out = result;
    succeed(error);
    return true;
}

bool CollisionWorld::sweepCapsule(const Capsule& capsule, const glm::vec3& displacement,
                                  std::optional<Hit>& out, const QueryFilter& filter, std::string* error) const {
    impl_->checkOwner();
    if (!validCapsule(capsule, error) || !validDisplacement(capsule.center, displacement, error)
        || !validFilter(filter, error)) return false;
    const auto shape = JPH::CapsuleShapeSettings(capsule.cylinderHalfHeight, capsule.radius).Create();
    if (shape.HasError()) return fail(error, shape.GetError().c_str());
    JPH::ShapeCastSettings settings;
    settings.SetBackFaceMode(JPH::EBackFaceMode::CollideWithBackFaces);
    settings.mReturnDeepestPoint = true;
    settings.mCollisionTolerance = collisionTolerance;
    settings.mActiveEdgeMode = JPH::EActiveEdgeMode::CollideWithAll;
    JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
    const BodyQueryFilter bodyFilter(filter, filter.ignore.value_);
    const auto transform = JPH::RMat44::sTranslation(JPH::RVec3(vec(capsule.center)));
    const JPH::RShapeCast cast(shape.Get(), JPH::Vec3::sReplicate(1.0f), transform, vec(displacement));
    impl_->physics.GetNarrowPhaseQuery().CastShape(cast, settings, JPH::RVec3::sZero(), collector, {}, {}, bodyFilter);
    std::optional<Hit> result;
    if (collector.HadHit()) {
        const auto& hit = collector.mHit;
        result = Hit{impl_->info(hit.mBodyID2), std::clamp(hit.mFraction, 0.0f, 1.0f),
                     vec(hit.mContactPointOn2), unit(vec(-hit.mPenetrationAxis)),
                     hit.mFraction <= 0.0f && hit.mPenetrationDepth > 0.0f};
    }
    out = result;
    succeed(error);
    return true;
}

bool CollisionWorld::overlapCapsule(const Capsule& capsule, std::vector<ColliderInfo>& out,
                                    const QueryFilter& filter, std::string* error) const {
    impl_->checkOwner();
    if (!validCapsule(capsule, error) || !validFilter(filter, error)) return false;
    const auto shape = JPH::CapsuleShapeSettings(capsule.cylinderHalfHeight, capsule.radius).Create();
    if (shape.HasError()) return fail(error, shape.GetError().c_str());
    JPH::CollideShapeSettings settings;
    settings.mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;
    settings.mCollisionTolerance = collisionTolerance;
    settings.mActiveEdgeMode = JPH::EActiveEdgeMode::CollideWithAll;
    JPH::ClosestHitPerBodyCollisionCollector<JPH::CollideShapeCollector> collector;
    const BodyQueryFilter bodyFilter(filter, filter.ignore.value_);
    impl_->physics.GetNarrowPhaseQuery().CollideShape(shape.Get(), JPH::Vec3::sReplicate(1.0f),
        JPH::RMat44::sTranslation(JPH::RVec3(vec(capsule.center))), settings, JPH::RVec3::sZero(),
        collector, {}, {}, bodyFilter);
    std::vector<ColliderInfo> result;
    result.reserve(collector.mHits.size());
    for (const auto& hit : collector.mHits) result.push_back(impl_->info(hit.mBodyID2));
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    result.erase(std::unique(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.id == b.id; }), result.end());
    out = std::move(result);
    succeed(error);
    return true;
}

} // namespace engine::collision
