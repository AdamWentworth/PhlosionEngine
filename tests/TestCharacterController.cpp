#include "engine/collision/CollisionWorld.h"

#include <glm/gtc/constants.hpp>
#include <array>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <type_traits>

using namespace engine::collision;

namespace {
#define CHECK(condition) do { if (!(condition)) throw std::runtime_error(std::string(__func__) + ":" + std::to_string(__LINE__) + " " #condition); } while (false)
bool near(float a, float b, float tolerance = 0.002f) { return std::abs(a - b) <= tolerance; }
bool near(glm::vec3 a, glm::vec3 b, float tolerance = 0.002f) { return glm::length(a - b) <= tolerance; }
bool finite(glm::vec3 a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }
void same(const CharacterState& a, const CharacterState& b) {
    CHECK(a.center == b.center && a.velocity == b.velocity && a.groundState == b.groundState);
    CHECK(a.groundPoint == b.groundPoint && a.groundNormal == b.groundNormal);
    CHECK(a.support.has_value() == b.support.has_value());
    if (a.support) CHECK(a.support->id == b.support->id && a.support->userData == b.support->userData);
}
ColliderId box(CollisionWorld& w, glm::vec3 p, glm::vec3 h, std::uint32_t layer = 0,
               bool sensor = false, std::uint64_t data = 0) {
    StaticColliderDesc desc;
    desc.position = p; desc.layer = layer; desc.sensor = sensor; desc.userData = data;
    ColliderId id;
    std::string error;
    if (!w.addBox(desc, h, id, &error)) throw std::runtime_error(error);
    return id;
}
ColliderId floor(CollisionWorld& w) { return box(w, {0, -0.5f, 0}, {100, 0.5f, 100}); }
ColliderId ramp(CollisionWorld& w, float slope, const StaticColliderDesc& desc = {}) {
    const std::array<glm::vec3, 4> vertices{{{0, 0, -4}, {0, 0, 4}, {10, 10 * slope, 4}, {10, 10 * slope, -4}}};
    const std::array<std::uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
    ColliderId id;
    CHECK(w.addTriangleMesh(desc, vertices, indices, id));
    return id;
}
CharacterId character(CollisionWorld& w, glm::vec3 p = {0, 0.9f, 0}, const CharacterSettings& s = {}) {
    CharacterId id;
    std::string error;
    if (!w.createCharacter(s, p, id, &error)) throw std::runtime_error(error);
    CHECK(id);
    return id;
}
CharacterState state(const CollisionWorld& w, CharacterId id) {
    CharacterState s;
    CHECK(w.characterState(id, s));
    CHECK(finite(s.center) && finite(s.velocity) && finite(s.groundPoint) && finite(s.groundNormal));
    CHECK(near(glm::length(s.groundNormal), 1));
    return s;
}
CharacterState ticks(CollisionWorld& w, CharacterId id, int count, CharacterInput input = {}) {
    auto s = state(w, id);
    for (int i = 0; i < count; ++i) {
        std::string error;
        if (!w.stepCharacter(id, input, s, &error)) throw std::runtime_error(error);
        CHECK(finite(s.center) && finite(s.velocity) && finite(s.groundPoint) && finite(s.groundNormal));
    }
    return s;
}
void freeMovementAndFixedTicks() {
    CollisionWorld w;
    CharacterSettings s; s.gravity = 0;
    const auto walk = character(w, {0, 5, 0}, s);
    CHECK(near(ticks(w, walk, 60, {{1, 0}, false}).center, {2, 5, 0}));
    CHECK(near(ticks(w, walk, 60, {{0, 1}, true}).center, {2, 5, 4.5f}));
    CHECK(near(ticks(w, walk, 60, {{0.3f, 0.4f}, false}).center, {2.6f, 5, 5.3f}));
    CHECK(near(ticks(w, walk, 60, {{1, 1}, false}).center,
        {2.6f + std::sqrt(2.0f), 5, 5.3f + std::sqrt(2.0f)}));
    const auto before = state(w, walk);
    CHECK(near(ticks(w, walk, 30).center, before.center));
    CHECK(near(state(w, walk).velocity, {0, 0, 0}));
    const auto extreme = character(w, {0, 0, 0}, s);
    const float huge = std::numeric_limits<float>::max();
    CHECK(near(ticks(w, extreme, 60, {{huge, huge}, false}).center, {std::sqrt(2.0f), 0, std::sqrt(2.0f)}));
    const auto a = character(w, {0, 10, 0});
    const auto b = character(w, {0, 10, 0});
    for (int frame = 0; frame < 60; ++frame) ticks(w, a, 2, {{0.2f, 0.7f}, true});
    for (int frame = 0; frame < 24; ++frame) ticks(w, b, 5, {{0.2f, 0.7f}, true});
    same(state(w, a), state(w, b));
    std::optional<Hit> hit;
    CHECK(w.rayCast({-5, 5, 0}, {20, 0, 0}, hit) && !hit);
}
void gravityLandingAndMetadata() {
    CollisionWorld w;
    CharacterSettings s; s.terminalFallSpeed = 3;
    const auto id = character(w, {0, 20, 0}, s);
    const auto first = ticks(w, id, 1);
    CHECK(near(first.velocity.y, -9.81f / 60));
    CHECK(near(first.center.y, 20 - 9.81f / 3600));
    const auto falling = ticks(w, id, 60);
    CHECK(near(falling.velocity.y, -3) && falling.groundState == CharacterGroundState::InAir);
    const auto ground = box(w, {0, -0.5f, 0}, {20, 0.5f, 20}, 31, false, 12345);
    const auto landed = ticks(w, id, 500);
    CHECK(landed.groundState == CharacterGroundState::Grounded);
    CHECK(landed.support && landed.support->id == ground && landed.support->userData == 12345);
    CHECK(landed.support->layer == 31 && !landed.support->sensor);
    CHECK(near(landed.center.y, 0.905f, 0.01f) && near(landed.groundPoint.y, 0));
    CHECK(near(landed.groundNormal, {0, 1, 0}) && near(landed.velocity.y, 0));
    CHECK(near(ticks(w, id, 240).center, landed.center));
}
void wallsCornersAndSweeps() {
    CollisionWorld w;
    floor(w);
    box(w, {2, 2, 0}, {0.01f, 3, 10});
    const auto id = character(w);
    const auto slid = ticks(w, id, 120, {{1, 1}, false});
    CHECK(slid.center.x <= 2 - 0.01f - 0.35f + 0.002f && slid.center.x > 1.5f);
    CHECK(slid.center.z > 2.7f && near(slid.velocity.x, 0));
    box(w, {0, 2, 4}, {10, 3, 0.01f});
    const auto corner = ticks(w, id, 180, {{1, 1}, true});
    CHECK(corner.center.x < 1.65f && corner.center.z < 3.65f && corner.center.z > 3.5f);
    CollisionWorld thin;
    CharacterSettings fast; fast.walkSpeed = fast.runSpeed = 100; fast.gravity = 0; fast.fixedDeltaSeconds = 1.0f / 15;
    box(thin, {2, 0, 0}, {0.0001f, 5, 5});
    const auto runner = character(thin, {0, 0, 0}, fast);
    const auto stopped = ticks(thin, runner, 1, {{1, 0}, true});
    CHECK(stopped.center.x <= 1.6501f && stopped.center.x > 1.6f);
    // A transformed triangle wall with analytically known world plane x=3.
    CollisionWorld meshWorld;
    StaticColliderDesc desc; desc.position = {3, 0, 0};
    desc.rotation = glm::angleAxis(-glm::half_pi<float>(), glm::vec3(0, 0, 1));
    ramp(meshWorld, 0, desc);
    const auto meshRunner = character(meshWorld, {0, -3, 0}, fast);
    CHECK(ticks(meshWorld, meshRunner, 1, {{1, 0}, true}).center.x <= 2.6501f);
}
void slopesAndCeilings() {
    CollisionWorld w;
    const auto surface = ramp(w, 0.5f);
    const float initialY = 0.5f + 0.55f + 0.35f * std::sqrt(1.25f);
    const auto id = character(w, {1, initialY, 0});
    auto up = ticks(w, id, 120, {{1, 0}, false});
    CHECK(up.center.x > 3 && up.center.y > initialY + 1);
    CHECK(up.groundState == CharacterGroundState::Grounded && up.support->id == surface);
    CHECK(near(up.center.y, 0.5f * up.center.x + 0.55f + 0.35f * std::sqrt(1.25f), 0.02f));
    CHECK(near(up.groundNormal, glm::normalize(glm::vec3(-0.5f, 1, 0)), 0.01f));
    const auto down = ticks(w, id, 60, {{-1, 0}, false});
    CHECK(down.center.x < up.center.x - 1 && down.center.y < up.center.y - 0.5f);
    CHECK(down.groundState == CharacterGroundState::Grounded);
    CollisionWorld steep;
    ramp(steep, 2);
    const auto steepId = character(steep, {3, 6 + 0.55f + 0.35f * std::sqrt(5.0f), 0});
    const auto start = state(steep, steepId);
    CHECK(start.groundState != CharacterGroundState::Grounded);
    const auto reject = ticks(steep, steepId, 90, {{1, 0}, false});
    CHECK(reject.center.x <= 3.02f && reject.center.y < start.center.y);
    CollisionWorld ceiling;
    ramp(ceiling, 0.5f);
    box(ceiling, {5, 3, 0}, {5, 0.1f, 4});
    const auto blocked = character(ceiling, {1, initialY, 0});
    const auto stop = ticks(ceiling, blocked, 180, {{1, 0}, false});
    CHECK(stop.center.y + 0.9f <= 2.902f && stop.center.x < 2.5f);
}
CharacterState stepScene(float height, float stepHeight, bool ceiling = false) {
    CollisionWorld w; floor(w);
    box(w, {3, height / 2, 0}, {2, height / 2, 3});
    if (ceiling) box(w, {3, 1.95f, 0}, {2, 0.05f, 3});
    CharacterSettings s; s.radius = 0.1f; s.cylinderHalfHeight = 0.8f; s.stepHeight = stepHeight;
    const auto id = character(w, {0, 0.9f, 0}, s);
    return ticks(w, id, 120, {{1, 0}, false});
}
void stepsFloorFollowingAndLedges() {
    const auto low = stepScene(0.25f, 0.3f);
    CHECK(low.center.x > 3 && near(low.center.y, 1.155f, 0.015f));
    CHECK(stepScene(0.5f, 0.3f).center.x < 1);
    CHECK(stepScene(0.25f, 0).center.x < 1);
    CHECK(stepScene(0.25f, 0.3f, true).center.x < 1);
    // Walk off a high ledge; snap cannot bridge the two-unit gap.
    CollisionWorld ledge;
    floor(ledge);
    box(ledge, {-2, 1, 0}, {2, 1, 3});
    const auto id = character(ledge, {-1, 2.9f, 0});
    auto off = ticks(ledge, id, 55, {{1, 0}, false});
    CHECK(off.center.x > 0.7f && off.center.y < 2.8f && off.center.y > 1.0f);
    CHECK(off.groundState == CharacterGroundState::InAir && off.velocity.y < 0);
    CHECK(ticks(ledge, id, 90).groundState == CharacterGroundState::Grounded);
    // With gravity disabled, only the explicit floor-follow policy can descend.
    for (const float snap : {0.0f, 0.15f}) {
        CollisionWorld descent;
        ramp(descent, 0.5f);
        CharacterSettings s; s.gravity = 0; s.floorSnapDistance = snap;
        const auto walker = character(descent, {5, 2.5f + 0.55f + 0.35f * std::sqrt(1.25f), 0}, s);
        const auto result = ticks(descent, walker, 60, {{-1, 0}, false});
        if (snap > 0) CHECK(result.center.y < 2.7f && result.groundState == CharacterGroundState::Grounded);
        else CHECK(result.center.y > 3.4f && result.groundState == CharacterGroundState::InAir);
    }
}
void overlapRecoveryAndTeleport() {
    CollisionWorld w;
    const auto ground = floor(w);
    const auto id = character(w, {0, 0.87f, 0});
    const auto recovered = ticks(w, id, 60);
    CHECK(recovered.center.y >= 0.899f && recovered.center.y < 0.92f);
    ticks(w, id, 30, {{1, 0}, true});
    auto before = state(w, id);
    std::string error;
    CHECK(!w.teleportCharacter(id, {0, 0.7f, 0}, &error) && !error.empty());
    same(before, state(w, id));
    CHECK(w.teleportCharacter(id, {3, 0.9f, 0}, &error) && error.empty());
    auto moved = state(w, id);
    CHECK(near(moved.center, {3, 0.9f, 0}) && near(moved.velocity, {0, 0, 0}));
    CHECK(moved.groundState == CharacterGroundState::Grounded && moved.support->id == ground);
    CHECK(w.teleportCharacter(id, {3, 0.89995f, 0})); // Within the stated 0.0001 penetration tolerance.
    CHECK(!w.teleportCharacter(id, {3, 0.8997f, 0}));
    CHECK(w.teleportCharacter(id, {4, 4, 0}));
    CHECK(state(w, id).groundState == CharacterGroundState::InAir && !state(w, id).support);
    // Precise destination testing must allow capsule/AABB corner near misses.
    box(w, {0, 4, 0}, {1, 1, 1});
    CHECK(w.teleportCharacter(id, {1.3f, 4, 1.3f}));
    CHECK(!w.teleportCharacter(id, {1.2f, 4, 1.2f}));
    CollisionWorld trapped; floor(trapped);
    box(trapped, {0, 1.7f, 0}, {4, 0.2f, 4});
    const auto prisoner = character(trapped, {0, 0.8f, 0});
    ticks(trapped, prisoner, 120, {{1, 0}, false});
    CHECK(trapped.teleportCharacter(prisoner, {10, 0.9f, 0}));
    CHECK(ticks(trapped, prisoner, 30).groundState == CharacterGroundState::Grounded);
}
void characterBlockingAndLifecycle() {
    CollisionWorld w;
    floor(w);
    CharacterSettings blocking;
    blocking.collideWithCharacters = true;
    const auto walker = character(w, {-2, 0.9f, 0}, blocking);
    const auto npc = character(w, {0, 0.9f, 0}, blocking);
    const auto stopped = ticks(w, walker, 120, {{1, 0}, false});
    CHECK(stopped.center.x < -0.69f && stopped.center.x > -0.75f);
    CHECK(near(stopped.center.z, 0) && near(state(w, npc).center, {0, 0.9f, 0}, 0.02f));
    CHECK(stopped.groundState == CharacterGroundState::Grounded);
    CHECK(near(ticks(w, walker, 30, {{1, 0}, true}).center, stopped.center, 0.02f));
    CHECK(!w.teleportCharacter(walker, {-0.2f, 0.9f, 0}));
    CHECK(near(state(w, walker).center, stopped.center));
    CHECK(w.teleportCharacter(walker, {-2, 0.9f, 0}));
    const auto around = ticks(w, walker, 120, {{1, 1}, false});
    CHECK(around.center.x > 0 && around.center.z > 1);
    CHECK(w.removeCharacter(npc));
    CHECK(ticks(w, walker, 120, {{-1, 0}, false}).center.x < -1);
    w.clear();
    CHECK(w.characterCount() == 0);
    floor(w);
    const auto newWalker = character(w, {-2, 0.9f, 0}, blocking);
    CHECK(ticks(w, newWalker, 120, {{1, 0}, false}).center.x > 1);
    CollisionWorld legacy;
    floor(legacy);
    const auto unblocked = character(legacy, {-2, 0.9f, 0});
    character(legacy, {0, 0.9f, 0});
    CHECK(ticks(legacy, unblocked, 120, {{1, 0}, false}).center.x > 1);
    CollisionWorld mixed;
    floor(mixed);
    character(mixed, {0, 0.9f, 0});
    const auto optedIn = character(mixed, {-2, 0.9f, 0}, blocking);
    CHECK(ticks(mixed, optedIn, 120, {{1, 0}, false}).center.x < -0.69f);
}
void filteringAndSupportRemoval() {
    CollisionWorld w;
    const auto ground = box(w, {0, -0.5f, 0}, {20, 0.5f, 20}, 31, false, 77);
    box(w, {1, 1, 0}, {0.2f, 3, 10}, 0, true);
    CharacterSettings s; s.layerMask = 1u << 31;
    const auto id = character(w, {0, 0.9f, 0}, s);
    CHECK(ticks(w, id, 60, {{1, 0}, false}).center.x > 1.9f);
    CHECK(state(w, id).support->id == ground);
    CHECK(w.remove(ground));
    CHECK(!state(w, id).support && state(w, id).groundState == CharacterGroundState::InAir);
    const auto replacement = box(w, {0, -4.5f, 0}, {20, 0.5f, 20}, 31, false, 88);
    CHECK(replacement != ground);
    CHECK(ticks(w, id, 120).support->id == replacement);
    s.ignore = replacement;
    const auto ignored = character(w, {0, -3.1f, 0}, s);
    CHECK(ticks(w, ignored, 60).center.y < -4);
    CHECK(w.teleportCharacter(ignored, {0, -4.5f, 0}));
    s.ignore = {}; s.layerMask = 0;
    const auto masked = character(w, {0, 0.9f, 0}, s);
    CHECK(ticks(w, masked, 120).center.y < -5);
    CHECK(w.teleportCharacter(masked, {0, -4.5f, 0}));
    s.layerMask = 0xffffffffu;
    const auto sensorWalker = character(w, {0, 0.9f, 0}, s);
    CHECK(ticks(w, sensorWalker, 60, {{1, 0}, false}).center.x > 1.9f);
    CHECK(w.teleportCharacter(sensorWalker, {1, 0.9f, 0}));
    s.ignore = ground; // Removed IDs do not hide a replacement collider in the reused slot.
    const auto staleIgnore = character(w, {0, -3.1f, 0}, s);
    CHECK(ticks(w, staleIgnore, 30).support->id == replacement);
    CollisionWorld sensors;
    box(sensors, {0, -0.5f, 0}, {20, 0.5f, 20}, 0, true);
    const auto falling = character(sensors);
    CHECK(ticks(sensors, falling, 60).center.y < -1);
}
void lifecycleCapacityAndThreads() {
    static_assert(!std::is_convertible_v<ColliderId, CharacterId>);
    CollisionWorldOptions options; options.maxCharacters = 1;
    CollisionWorld w(options), foreign;
    const auto id = character(w);
    auto sentinel = state(w, id); sentinel.center = {11, 22, 33};
    auto output = sentinel;
    std::string error;
    CHECK(!w.characterState({}, output, &error) && !error.empty()); same(output, sentinel);
    CHECK(!foreign.stepCharacter(id, {}, output, &error)); same(output, sentinel);
    CHECK(!foreign.teleportCharacter(id, {0, 0, 0}) && !foreign.removeCharacter(id));
    CHECK(!w.stepCharacter({}, {}, output) && !w.teleportCharacter({}, {0, 0, 0}) && !w.removeCharacter({}));
    same(output, sentinel);
    auto outId = id;
    CHECK(!w.createCharacter({}, {0, 0, 0}, outId, &error) && outId == id);
    int rejected = 0;
    std::exception_ptr threadError;
    std::thread thread([&] {
        try {
            const std::array<std::function<void()>, 7> calls{{
                [&] { w.characterState(id, output); }, [&] { w.stepCharacter(id, {}, output); },
                [&] { w.teleportCharacter(id, {2, 2, 2}); }, [&] { w.removeCharacter(id); },
                [&] { w.createCharacter({}, {0, 0, 0}, outId); }, [&] { w.characterCount(); }, [&] { w.clear(); }
            }};
            for (const auto& call : calls) { try { call(); } catch (const std::logic_error&) { ++rejected; } }
            CollisionWorld independent; floor(independent);
            const auto other = character(independent);
            CHECK(ticks(independent, other, 60).groundState == CharacterGroundState::Grounded);
        } catch (...) { threadError = std::current_exception(); }
    });
    thread.join();
    if (threadError) std::rethrow_exception(threadError);
    CHECK(rejected == 7 && w.characterCount() == 1); same(output, sentinel);
    CHECK(w.removeCharacter(id) && !w.removeCharacter(id));
    CHECK(!w.stepCharacter(id, {}, output)); same(output, sentinel);
    const auto replacement = character(w); CHECK(replacement != id);
    const auto collider = floor(w);
    w.clear(); CHECK(w.characterCount() == 0 && w.colliderCount() == 0 && !w.contains(collider));
    CHECK(!w.characterState(replacement, output));
    for (int i = 0; i < 100; ++i) {
        CollisionWorld repeat; floor(repeat);
        const auto next = character(repeat);
        CHECK(next != id && next != replacement);
        CHECK(ticks(repeat, next, 2).groundState == CharacterGroundState::Grounded);
    }
}
void validationAndBounds() {
    CollisionWorld w;
    floor(w);
    const auto id = character(w);
    const auto before = state(w, id);
    auto sentinel = before; sentinel.center = {9, 8, 7};
    auto output = sentinel;
    std::string error;
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    const std::array<float CharacterSettings::*, 10> fields{{&CharacterSettings::radius, &CharacterSettings::cylinderHalfHeight,
        &CharacterSettings::walkSpeed, &CharacterSettings::runSpeed, &CharacterSettings::gravity,
        &CharacterSettings::terminalFallSpeed, &CharacterSettings::maxSlopeDegrees, &CharacterSettings::stepHeight,
        &CharacterSettings::floorSnapDistance, &CharacterSettings::fixedDeltaSeconds}};
    for (const auto field : fields) for (const float value : {nan, inf, -inf, -1.0f}) {
        CharacterSettings s; s.*field = value;
        auto outId = id;
        CHECK(!w.createCharacter(s, {0, 0, 0}, outId, &error) && outId == id && !error.empty());
    }
    for (const float value : {nan, inf, -inf}) {
        CHECK(!w.stepCharacter(id, {{value, 0}, false}, output, &error) && !error.empty());
        same(output, sentinel); same(state(w, id), before);
        CHECK(!w.teleportCharacter(id, {0, value, 0}, &error)); same(state(w, id), before);
        CHECK(!w.stepCharacter(id, {{0, value}, false}, output, &error)); same(output, sentinel);
    }
    CharacterSettings s; s.runSpeed = 1;
    auto outId = id;
    CHECK(!w.createCharacter(s, {0, 0, 0}, outId));
    const std::array<float, 10> above{{5.01f, 10.01f, 100.01f, 100.01f, 100.01f, 100.01f, 89.01f, 2.01f, 2.01f, 1.0f}};
    for (std::size_t i = 0; i < fields.size(); ++i) {
        s = {}; s.*fields[i] = above[i];
        CHECK(!w.createCharacter(s, {0, 0, 0}, outId));
    }
    for (const float slope : {0.0f, 0.99f, 90.0f}) {
        s = {}; s.maxSlopeDegrees = slope;
        CHECK(!w.createCharacter(s, {0, 0, 0}, outId));
    }
    for (const auto [field, value] : std::array<std::pair<float CharacterSettings::*, float>, 3>{{
        {&CharacterSettings::radius, 0.049f}, {&CharacterSettings::terminalFallSpeed, 0.009f},
        {&CharacterSettings::fixedDeltaSeconds, 1.0f / 241}}}) {
        s = {}; s.*field = value; CHECK(!w.createCharacter(s, {0, 0, 0}, outId));
    }
    s = {}; s.radius = 0.05f; s.cylinderHalfHeight = 0; s.walkSpeed = s.runSpeed = 0;
    s.gravity = 0; s.terminalFallSpeed = 0.01f; s.maxSlopeDegrees = 1;
    s.stepHeight = s.floorSnapDistance = 0; s.fixedDeltaSeconds = 1.0f / 240;
    const auto small = character(w, {-10000, -10000, -10000}, s);
    CHECK(near(ticks(w, small, 1).center, {-10000, -10000, -10000}));
    s.radius = 5; s.cylinderHalfHeight = 10; s.walkSpeed = s.runSpeed = 100;
    s.gravity = s.terminalFallSpeed = 100; s.maxSlopeDegrees = 89;
    s.stepHeight = s.floorSnapDistance = 2; s.fixedDeltaSeconds = 1.0f / 15;
    const auto large = character(w, {10000, 10000, 10000}, s);
    CHECK(!w.stepCharacter(large, {{1, 0}, false}, output, &error)); same(output, sentinel);
    CHECK(state(w, large).center == glm::vec3(10000));
    CHECK(!w.teleportCharacter(id, {10001, 0, 0}, &error));
    CHECK(!w.createCharacter({}, {std::numeric_limits<float>::max(), 0, 0}, outId, &error));
    CollisionWorld sphere; floor(sphere);
    s = {}; s.cylinderHalfHeight = 0;
    const auto ball = character(sphere, {0, 2, 0}, s);
    CHECK(near(ticks(sphere, ball, 120).center.y, 0.355f, 0.01f));
    for (const auto maxCharacters : {0u, 4097u}) {
        CollisionWorldOptions options; options.maxCharacters = maxCharacters;
        bool rejected = false;
        try { CollisionWorld invalid(options); } catch (const std::invalid_argument&) { rejected = true; }
        CHECK(rejected);
    }
    CollisionWorldOptions options; options.maxCharacters = 4096;
    CollisionWorld maximum(options);
    for (int i = 0; i < 4096; ++i) character(maximum);
    CHECK(maximum.characterCount() == 4096);
    CHECK(!maximum.createCharacter({}, {0, 0, 0}, outId));
    maximum.clear(); CHECK(maximum.characterCount() == 0);
    character(maximum);
    // A recovery displacement can exceed the envelope even when the requested endpoint is valid.
    CollisionWorld boundary;
    box(boundary, {9999, 0, 0}, {1, 5, 5});
    s = {}; s.gravity = 0;
    const auto trapped = character(boundary, {9999.99f, 0, 0}, s);
    const auto saved = state(boundary, trapped);
    CHECK(!boundary.stepCharacter(trapped, {}, output, &error));
    same(output, sentinel); same(state(boundary, trapped), saved);
    CHECK(boundary.teleportCharacter(trapped, {9990, 0, 0}));
}
void repeatedAuthoredSequence() {
    CollisionWorld w;
    floor(w);
    box(w, {3, 1, 0}, {0.1f, 2, 3});
    box(w, {0, 0.1f, 3}, {3, 0.1f, 1});
    box(w, {0, 2, 2}, {3, 2, 0.1f}, 0, true);
    const auto a = character(w), b = character(w);
    const std::array<CharacterInput, 4> script{{{{1, 1}, false}, {{-1, 0}, true}, {{0, 1}, false}, {{0, 0}, false}}};
    for (const auto input : script) for (int i = 0; i < 90; ++i) {
        const auto first = ticks(w, a, 1, input);
        const auto second = ticks(w, b, 1, input);
        same(first, second);
    }
    CHECK(w.teleportCharacter(a, {0, 0.9f, 0}) && w.teleportCharacter(b, {0, 0.9f, 0}));
    same(state(w, a), state(w, b));
}
} // namespace

int main() {
    const std::array<std::pair<const char*, std::function<void()>>, 11> tests{{
        {"free movement and fixed ticks", freeMovementAndFixedTicks}, {"gravity landing metadata", gravityLandingAndMetadata},
        {"walls corners sweeps", wallsCornersAndSweeps}, {"slopes ceilings", slopesAndCeilings},
        {"steps following ledges", stepsFloorFollowingAndLedges}, {"recovery teleport", overlapRecoveryAndTeleport},
        {"character blocking lifecycle", characterBlockingAndLifecycle},
        {"filtering removal", filteringAndSupportRemoval}, {"lifecycle threads", lifecycleCapacityAndThreads},
        {"validation bounds", validationAndBounds}, {"repeated authored sequence", repeatedAuthoredSequence}
    }};
    int failures = 0;
    for (const auto& [name, test] : tests) {
        try { test(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& e) { ++failures; std::cerr << "FAIL " << name << ": " << e.what() << '\n'; }
    }
    return failures == 0 ? 0 : 1;
}
