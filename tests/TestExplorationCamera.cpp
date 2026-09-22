#include "engine/camera/ExplorationCamera.h"
#include "engine/input/InputActions.h"

#include <glm/gtc/constants.hpp>
#include <array>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

using namespace engine::camera;
using namespace engine::collision;
using namespace engine::input;

namespace {
#define CHECK(c) do { if (!(c)) throw std::runtime_error(std::string(__func__) + ":" + std::to_string(__LINE__) + " " #c); } while (false)
constexpr float pi = glm::pi<float>();
bool near(float a, float b, float tolerance = .00002f) { return std::abs(a - b) <= tolerance; }
bool near(glm::vec2 a, glm::vec2 b, float tolerance = .00002f) { return glm::length(a - b) <= tolerance; }
bool near(glm::vec3 a, glm::vec3 b, float tolerance = .00002f) { return glm::length(a - b) <= tolerance; }
void same(const ExplorationCameraPose& a, const ExplorationCameraPose& b) {
    CHECK(a.pivot == b.pivot && a.position == b.position && a.forward == b.forward && a.up == b.up && a.lookTarget == b.lookTarget);
    CHECK(a.yawRadians == b.yawRadians && a.pitchRadians == b.pitchRadians);
    CHECK(a.desiredDistance == b.desiredDistance && a.actualDistance == b.actualDistance);
    CHECK(a.aspectRatio == b.aspectRatio && a.verticalFovDegrees == b.verticalFovDegrees);
    CHECK(a.nearPlane == b.nearPlane && a.farPlane == b.farPlane);
    CHECK(a.obstructed == b.obstructed && a.originBlocked == b.originBlocked);
    CHECK(a.obstruction.has_value() == b.obstruction.has_value());
    if (a.obstruction) {
        CHECK(a.obstruction->collider.id == b.obstruction->collider.id);
        CHECK(a.obstruction->collider.userData == b.obstruction->collider.userData);
        CHECK(a.obstruction->collider.layer == b.obstruction->collider.layer);
        CHECK(a.obstruction->collider.sensor == b.obstruction->collider.sensor);
        CHECK(a.obstruction->fraction == b.obstruction->fraction && a.obstruction->point == b.obstruction->point);
        CHECK(a.obstruction->normal == b.obstruction->normal && a.obstruction->initialOverlap == b.obstruction->initialOverlap);
    }
}
ExplorationCameraPose read(const ExplorationCameraRig& rig) {
    ExplorationCameraPose p;
    std::string error = "old";
    CHECK(rig.pose(p, &error) && error.empty());
    CHECK(near(glm::length(p.forward), 1) && near(glm::length(p.up), 1));
    CHECK(near(glm::dot(p.forward, p.up), 0));
    CHECK(near(p.lookTarget, p.position + p.forward));
    CHECK(p.yawRadians >= -pi && p.yawRadians < pi);
    return p;
}
ExplorationCameraSettings flat() {
    ExplorationCameraSettings s;
    s.pivotOffset = {0, 0, 0}; s.initialPitchRadians = 0;
    return s;
}
ExplorationCameraPose reset(ExplorationCameraRig& rig, glm::vec3 target = {0, 0, 0},
                            const CollisionWorld* world = nullptr, float aspect = 16.0f / 9) {
    ExplorationCameraPose p;
    std::string error = "old";
    if (!rig.reset(target, aspect, p, world, &error)) throw std::runtime_error(error);
    CHECK(error.empty()); same(p, read(rig)); return p;
}
ExplorationCameraPose step(ExplorationCameraRig& rig, const ExplorationCameraFrame& frame, const CollisionWorld* world = nullptr) {
    ExplorationCameraPose p;
    std::string error = "old";
    if (!rig.step(frame, p, world, &error)) throw std::runtime_error(error);
    CHECK(error.empty()); same(p, read(rig)); return p;
}
ColliderId box(CollisionWorld& world, glm::vec3 p, glm::vec3 half, std::uint32_t layer = 0,
               bool sensor = false, std::uint64_t userData = 0, float yaw = 0) {
    StaticColliderDesc d;
    d.position = p; d.layer = layer; d.sensor = sensor; d.userData = userData;
    d.rotation = glm::angleAxis(yaw, glm::vec3(0, 1, 0));
    ColliderId id; std::string error;
    if (!world.addBox(d, half, id, &error)) throw std::runtime_error(error);
    return id;
}
void clearEye(const CollisionWorld& world, const ExplorationCameraPose& p, const ExplorationCameraSettings& s) {
    std::vector<ColliderInfo> overlaps;
    CHECK(world.overlapCapsule({p.position, s.probeRadius, 0}, overlaps,
        {s.layerMask, SensorPolicy::Exclude, s.ignore}));
    CHECK(overlaps.empty());
}

void initialResetAndProjection() {
    ExplorationCameraSettings authored;
    ExplorationCameraRig rig(authored);
    authored.desiredDistance = 25; authored.pivotOffset.y = 50;
    CHECK(rig.settings().desiredDistance == 6 && rig.settings().pivotOffset.y == 1);
    const auto p = reset(rig, {2, 3, 4});
    CHECK(near(p.pivot, {2, 4, 4}));
    CHECK(near(p.position, {2, 4 + 6 * std::sin(.35f), 4 + 6 * std::cos(.35f)}));
    CHECK(p.desiredDistance == 6 && p.actualDistance == 6 && !p.obstructed && !p.originBlocked && !p.obstruction);
    CHECK(p.verticalFovDegrees == 45 && p.nearPlane == .1f && p.farPlane == 1000);
    ExplorationCameraFrame frame; frame.targetPosition = {10, 5, -3}; frame.deltaSeconds = .12f;
    const auto following = step(rig, frame);
    CHECK(following.yawRadians == 0 && following.pitchRadians == .35f);
    CHECK(near(following.pivot, {6, 5, .5f}));
    frame.lookDeltaRadians = {.8f, .2f}; frame.zoomDelta = 4;
    step(rig, frame);
    const auto cut = reset(rig, {-20, 7, 30}, nullptr, 21.0f / 9);
    CHECK(near(cut.pivot, {-20, 8, 30}) && cut.yawRadians == 0 && cut.pitchRadians == .35f);
    CHECK(cut.actualDistance == 6 && cut.desiredDistance == 6 && near(cut.aspectRatio, 21.0f / 9));

    // Independent pinhole projection: vertical span is unchanged at wider aspect,
    // horizontal span shrinks by aspect ratio; no renderer/matrix library involved.
    auto s = flat(); s.verticalFovDegrees = 60; s.desiredDistance = 10;
    ExplorationCameraRig framing(s);
    const double tangent = std::tan(std::numbers::pi / 6);
    for (const float aspect : {16.0f / 9, 21.0f / 9}) {
        const auto view = reset(framing, {}, nullptr, aspect);
        const auto project = [&](glm::vec3 point) {
            const glm::vec3 relative = point - view.position;
            const glm::vec3 right = glm::cross(view.forward, view.up);
            const double depth = glm::dot(relative, view.forward);
            CHECK(depth > 0);
            const double fovTangent = std::tan(static_cast<double>(view.verticalFovDegrees) * std::numbers::pi / 360);
            return glm::vec2(glm::dot(relative, right) / (depth * fovTangent * view.aspectRatio),
                glm::dot(relative, view.up) / (depth * fovTangent));
        };
        CHECK(near(project({0, 0, 0}), {0, 0}));
        CHECK(near(project({0, 2, 0}).y, static_cast<float>(2 / (10 * tangent))));
        CHECK(near(project({2, 0, 0}).x, static_cast<float>(2 / (10 * tangent * aspect))));
        CHECK(near(project({0, static_cast<float>(10 * tangent), 0}).y, 1));
    }
}

void controlsAndLimits() {
    auto s = flat(); s.initialYawRadians = 20 * pi + .25f;
    ExplorationCameraRig wrapped(s);
    CHECK(near(reset(wrapped).yawRadians, .25f, .00001f));
    for (float yaw : {-1e6f, -pi, pi, 1e6f}) {
        s.initialYawRadians = yaw; ExplorationCameraRig boundary(s);
        (void)reset(boundary);
    }
    s = flat(); s.minPitchRadians = -.8f; s.maxPitchRadians = .9f;
    s.minDistance = 1; s.maxDistance = 9;
    ExplorationCameraRig rig(s);
    ExplorationCameraFrame frame;
    frame.lookDeltaRadians = {pi / 2, .3f}; frame.lookRateRadiansPerSecond = {20, 20};
    frame.zoomDelta = 1; frame.zoomRate = 1000;
    const auto first = step(rig, frame);
    CHECK(near(first.yawRadians, pi / 2) && first.pitchRadians == .3f && first.desiredDistance == 7 && first.actualDistance == 7);
    CHECK(near(first.position, {7 * std::cos(.3f), 7 * std::sin(.3f), 0}));
    frame.lookDeltaRadians = {100, 100}; frame.zoomDelta = 1000;
    auto p = step(rig, frame); CHECK(p.pitchRadians == .9f && p.desiredDistance == 9 && p.actualDistance == 7);
    frame.lookDeltaRadians = {-100, -100}; frame.zoomDelta = -1000;
    p = step(rig, frame); CHECK(p.pitchRadians == -.8f && p.desiredDistance == 1 && p.actualDistance == 1);
    for (const float dt : {0.0f, .05f, .1f, .25f}) {
        ExplorationCameraRig rates;
        frame = {}; frame.deltaSeconds = dt;
        frame.lookDeltaRadians = {.2f, -.1f}; frame.lookRateRadiansPerSecond = {1, 2};
        frame.zoomDelta = .5f; frame.zoomRate = 3;
        p = step(rates, frame);
        CHECK(near(p.yawRadians, .2f + dt) && near(p.pitchRadians, .25f + 2 * dt));
        CHECK(near(p.desiredDistance, 6.5f + 3 * dt) && p.actualDistance == p.desiredDistance);
    }
    ExplorationCameraRig a, b;
    frame = {}; frame.lookRateRadiansPerSecond = {.8f, -.4f}; frame.zoomRate = 2;
    frame.deltaSeconds = .2f; const auto whole = step(a, frame);
    frame.deltaSeconds = .1f; step(b, frame); const auto split = step(b, frame);
    CHECK(near(whole.yawRadians, split.yawRadians) && near(whole.pitchRadians, split.pitchRadians));
    CHECK(near(whole.desiredDistance, split.desiredDistance));
}

void dampingAndRecovery() {
    auto s = flat(); s.followHalfLifeSeconds = .2f; s.recoveryHalfLifeSeconds = .2f;
    ExplorationCameraRig whole(s), split(s);
    reset(whole); reset(split);
    ExplorationCameraFrame frame; frame.targetPosition = {8, 4, -2}; frame.deltaSeconds = .2f;
    const auto full = step(whole, frame);
    CHECK(near(full.pivot, {4, 2, -1}));
    frame.deltaSeconds = .05f;
    for (int i = 0; i < 4; ++i) step(split, frame);
    CHECK(near(full.pivot, read(split).pivot));
    frame.deltaSeconds = 0; frame.targetPosition = {-100, 20, 30};
    CHECK(step(whole, frame).pivot == full.pivot);
    CHECK(reset(whole, frame.targetPosition).pivot == frame.targetPosition);
    s.followHalfLifeSeconds = 0; s.recoveryHalfLifeSeconds = 0;
    ExplorationCameraRig immediate(s); reset(immediate);
    CHECK(step(immediate, frame).pivot == glm::vec3(0));
    frame.deltaSeconds = .01f; CHECK(step(immediate, frame).pivot == frame.targetPosition);

    CollisionWorld world;
    auto wall = box(world, {0, 0, 3}, {5, 5, .1f});
    s = flat(); s.recoveryHalfLifeSeconds = .2f;
    ExplorationCameraRig recover(s), recoverSplit(s);
    const auto blocked = reset(recover, {}, &world);
    reset(recoverSplit, {}, &world);
    CHECK(near(blocked.actualDistance, 2.67f, .001f));
    CHECK(world.remove(wall));
    frame = {}; frame.deltaSeconds = .2f;
    const auto recovered = step(recover, frame, &world);
    CHECK(near(recovered.actualDistance, (blocked.actualDistance + 6) / 2));
    CHECK(!recovered.obstructed && !recovered.obstruction && !recovered.originBlocked);
    frame.deltaSeconds = .05f;
    for (int i = 0; i < 4; ++i) step(recoverSplit, frame, &world);
    CHECK(near(recovered.actualDistance, read(recoverSplit).actualDistance));
    const auto held = read(recover);
    frame.deltaSeconds = 0; CHECK(step(recover, frame, &world).actualDistance == held.actualDistance);
    wall = box(world, {0, 0, 1}, {5, 5, .1f});
    const auto inward = step(recover, frame, &world);
    CHECK(near(inward.actualDistance, .67f, .001f) && inward.obstructed);
    CHECK(world.remove(wall));
    CHECK(reset(recover, {}, &world).actualDistance == 6);
    wall = box(world, {0, 0, .5f}, {5, 5, .1f});
    CHECK(reset(recover, {}, &world).actualDistance < s.minDistance);
    CHECK(world.remove(wall));
    s.recoveryHalfLifeSeconds = 0; ExplorationCameraRig instant(s);
    wall = box(world, {0, 0, 3}, {5, 5, .1f}); reset(instant, {}, &world);
    CHECK(world.remove(wall)); frame.deltaSeconds = .01f;
    CHECK(step(instant, frame, &world).actualDistance == 6);
}

void realGeometryAndMetadata() {
    auto s = flat();
    for (const float radius : {.01f, .2f, 2.0f}) {
        CollisionWorld world; s.probeRadius = radius;
        const auto wall = box(world, {0, 0, 3}, {10, 10, .0001f}, 31, false, 0x123456789abcdef0ull);
        ExplorationCameraRig rig(s);
        const auto p = reset(rig, {}, &world);
        CHECK(p.obstructed && !p.originBlocked && p.obstruction);
        CHECK(near(p.actualDistance, 3 - .0001f - radius - .03f, .001f));
        CHECK(near(p.obstruction->fraction, (3 - .0001f - radius) / 6, .0002f));
        CHECK(near(p.obstruction->point.z, 2.9999f, .001f) && near(p.obstruction->normal, {0, 0, -1}, .001f));
        CHECK(p.obstruction->collider.id == wall && p.obstruction->collider.layer == 31);
        CHECK(p.obstruction->collider.userData == 0x123456789abcdef0ull && !p.obstruction->collider.sensor);
        clearEye(world, p, s);
        CHECK(world.remove(wall));
        // Published and stored snapshots retain value metadata after removal and a world change.
        same(read(rig), p);
        CHECK(reset(rig, {}, &world).actualDistance == 6);
    }
    s = flat();
    CollisionWorld rotated;
    box(rotated, {0, 0, 3}, {10, 10, .1f}, 0, false, 0, .6f);
    ExplorationCameraRig oblique(s);
    const auto p = reset(oblique, {}, &rotated);
    CHECK(near(p.actualDistance, 3 - (.1f + .2f) / std::cos(.6f) - .03f, .002f));
    CHECK(near(p.obstruction->normal, {-std::sin(.6f), 0, -std::cos(.6f)}, .002f));
    clearEye(rotated, p, s);

    // Transform a local sheet to a world plane whose intersection is z=3.
    const std::array<glm::vec3, 4> vertices{{{-10, -10, 0}, {-10, 10, 0}, {10, 10, 0}, {10, -10, 0}}};
    const std::array<std::uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
    for (float angle : {0.0f, .45f, pi}) {
        CollisionWorld mesh;
        StaticColliderDesc desc; desc.position = {0, 0, 3}; desc.rotation = glm::angleAxis(angle, glm::vec3(0, 1, 0)); desc.userData = 77;
        ColliderId id; CHECK(mesh.addTriangleMesh(desc, vertices, indices, id));
        ExplorationCameraRig meshRig(s); const auto hit = reset(meshRig, {}, &mesh);
        CHECK(hit.obstruction && hit.obstruction->collider.id == id && hit.obstruction->collider.userData == 77);
        CHECK(near(hit.actualDistance, 3 - .2f / std::abs(std::cos(angle)) - .03f, .002f));
        clearEye(mesh, hit, s);
    }
}

void overlapTouchingAndFilters() {
    auto s = flat(); s.probeRadius = .25f;
    CollisionWorld world;
    const auto sensor = box(world, {}, {1, 1, 1}, 0, true, 9);
    const auto layer = box(world, {0, 0, 2}, {5, 5, .25f}, 31, false, 31);
    const auto further = box(world, {0, 0, 4}, {5, 5, .25f}, 3, false, 3);
    ExplorationCameraRig all(s); auto p = reset(all, {}, &world);
    CHECK(p.obstruction->collider.id == layer && !p.originBlocked);
    s.layerMask = 1u << 3; ExplorationCameraRig mask(s); p = reset(mask, {}, &world);
    CHECK(p.obstruction->collider.id == further);
    s.layerMask = 0xffffffffu; s.ignore = layer; ExplorationCameraRig ignored(s);
    CHECK(reset(ignored, {}, &world).obstruction->collider.id == further);
    s.layerMask = 0; ExplorationCameraRig none(s); CHECK(!reset(none, {}, &world).obstructed);
    s.layerMask = 0xffffffffu; CHECK(world.remove(layer));
    ExplorationCameraRig stale(s); CHECK(reset(stale, {}, &world).obstruction->collider.id == further);
    CollisionWorld foreign; s.ignore = box(foreign, {}, {1, 1, 1}); ExplorationCameraRig foreignIgnore(s);
    CHECK(reset(foreignIgnore, {}, &world).obstruction->collider.id == further);
    CHECK(world.remove(further) && world.remove(sensor));
    s.ignore = {};
    auto solid = box(world, {}, {.1f, .1f, .1f});
    ExplorationCameraRig penetrating(s); p = reset(penetrating, {}, &world);
    CHECK(p.originBlocked && p.obstructed && p.actualDistance == 0 && p.position == p.pivot);
    CHECK(p.obstruction->initialOverlap && p.obstruction->collider.id == solid);
    CHECK(near(glm::length(p.lookTarget - p.position), 1));
    CHECK(world.remove(solid));
    solid = box(world, {0, 0, 1.25f}, {1, 1, 1});
    ExplorationCameraRig touching(s); p = reset(touching, {}, &world);
    CHECK(p.obstructed && p.actualDistance == 0 && !p.originBlocked);
    CHECK(p.obstruction->fraction == 0 && !p.obstruction->initialOverlap);
    CHECK(world.remove(solid));
    CHECK(reset(touching, {}, &world).actualDistance == 6);
    // The world is borrowed for one call; destroying it cannot invalidate a snapshot or rig.
    ExplorationCameraRig borrowed(s); ExplorationCameraPose snapshot;
    { CollisionWorld temporary; box(temporary, {0, 0, 3}, {1, 1, .1f}, 0, false, 444);
      snapshot = reset(borrowed, {}, &temporary); }
    CHECK(read(borrowed).obstruction->collider.userData == 444); same(read(borrowed), snapshot);
    CHECK(reset(borrowed).actualDistance == 6);
}

void settingsValidation() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const std::vector<float ExplorationCameraSettings::*> scalarMembers{
        &ExplorationCameraSettings::initialYawRadians, &ExplorationCameraSettings::initialPitchRadians,
        &ExplorationCameraSettings::minPitchRadians, &ExplorationCameraSettings::maxPitchRadians,
        &ExplorationCameraSettings::desiredDistance, &ExplorationCameraSettings::minDistance, &ExplorationCameraSettings::maxDistance,
        &ExplorationCameraSettings::followHalfLifeSeconds, &ExplorationCameraSettings::recoveryHalfLifeSeconds,
        &ExplorationCameraSettings::probeRadius, &ExplorationCameraSettings::safetyMargin,
        &ExplorationCameraSettings::verticalFovDegrees, &ExplorationCameraSettings::nearPlane, &ExplorationCameraSettings::farPlane};
    const auto rejects = [](const ExplorationCameraSettings& s) {
        bool threw = false; try { ExplorationCameraRig rig(s); } catch (const std::invalid_argument&) { threw = true; }
        CHECK(threw);
    };
    for (auto member : scalarMembers) for (float invalid : {nan, inf, -inf}) {
        ExplorationCameraSettings s; s.*member = invalid; rejects(s);
    }
    for (int axis = 0; axis < 3; ++axis) for (float invalid : {nan, inf, -100.01f, 100.01f}) {
        ExplorationCameraSettings s; s.pivotOffset[axis] = invalid; rejects(s);
    }
    const std::vector<std::pair<float ExplorationCameraSettings::*, float>> invalid{
        {&ExplorationCameraSettings::initialYawRadians, 1000001.0f}, {&ExplorationCameraSettings::initialYawRadians, -1000001.0f},
        {&ExplorationCameraSettings::initialPitchRadians, 1.41f}, {&ExplorationCameraSettings::initialPitchRadians, -1.41f},
        {&ExplorationCameraSettings::minPitchRadians, -1.451f}, {&ExplorationCameraSettings::minPitchRadians, .4f},
        {&ExplorationCameraSettings::maxPitchRadians, 1.451f}, {&ExplorationCameraSettings::maxPitchRadians, .3f},
        {&ExplorationCameraSettings::minDistance, .099f}, {&ExplorationCameraSettings::minDistance, 7.0f},
        {&ExplorationCameraSettings::maxDistance, 5.0f}, {&ExplorationCameraSettings::maxDistance, 1001.0f},
        {&ExplorationCameraSettings::desiredDistance, .49f}, {&ExplorationCameraSettings::desiredDistance, 31.0f},
        {&ExplorationCameraSettings::followHalfLifeSeconds, -.001f}, {&ExplorationCameraSettings::followHalfLifeSeconds, 10.01f},
        {&ExplorationCameraSettings::recoveryHalfLifeSeconds, -.001f}, {&ExplorationCameraSettings::recoveryHalfLifeSeconds, 10.01f},
        {&ExplorationCameraSettings::probeRadius, .009f}, {&ExplorationCameraSettings::probeRadius, 2.01f},
        {&ExplorationCameraSettings::safetyMargin, -.001f}, {&ExplorationCameraSettings::safetyMargin, .501f},
        {&ExplorationCameraSettings::verticalFovDegrees, .99f}, {&ExplorationCameraSettings::verticalFovDegrees, 150.01f},
        {&ExplorationCameraSettings::nearPlane, .0009f}, {&ExplorationCameraSettings::nearPlane, 10.01f},
        {&ExplorationCameraSettings::farPlane, .1f}, {&ExplorationCameraSettings::farPlane, 100001.0f}};
    for (auto [member, value] : invalid) { ExplorationCameraSettings s; s.*member = value; rejects(s); }
    auto s = flat(); s.pivotOffset = {-100, 100, 100}; s.initialYawRadians = 1e6f;
    s.minPitchRadians = -1.45f; s.maxPitchRadians = 1.45f; s.initialPitchRadians = -1.45f;
    s.minDistance = s.desiredDistance = s.maxDistance = .1f; s.probeRadius = .01f; s.safetyMargin = 0;
    s.followHalfLifeSeconds = 0; s.recoveryHalfLifeSeconds = 0; s.verticalFovDegrees = 1; s.nearPlane = .001f; s.farPlane = .002f;
    ExplorationCameraRig low(s); CHECK(reset(low).actualDistance == .1f);
    s.minDistance = s.desiredDistance = s.maxDistance = 1000; s.initialPitchRadians = 1.45f;
    s.probeRadius = 2; s.safetyMargin = .5f; s.followHalfLifeSeconds = 10; s.recoveryHalfLifeSeconds = 10;
    s.verticalFovDegrees = 150; s.nearPlane = 10; s.farPlane = 100000;
    ExplorationCameraRig high(s); CHECK(reset(high).actualDistance == 1000);
}

void frameValidationAndTransactions() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    auto s = flat(); s.followHalfLifeSeconds = 0;
    ExplorationCameraRig rig(s), control(s), uninitialized(s);
    ExplorationCameraPose sentinel; sentinel.position = {7, 8, 9}; sentinel.desiredDistance = 77;
    std::string error;
    auto output = sentinel;
    CHECK(!uninitialized.pose(output, &error) && !error.empty()); same(output, sentinel);
    CollisionWorld world; box(world, {0, 0, 3}, {3, 3, .1f});
    const auto before = reset(rig, {}, &world); reset(control, {}, &world);
    const auto rejects = [&](const ExplorationCameraFrame& frame) {
        output = sentinel; error.clear(); CHECK(!rig.step(frame, output, &world, &error) && !error.empty());
        same(output, sentinel); same(read(rig), before);
        CHECK(rig.settings().desiredDistance == s.desiredDistance && rig.settings().initialYawRadians == s.initialYawRadians);
        CHECK(!uninitialized.step(frame, output, &world, &error)); same(output, sentinel);
        CHECK(!uninitialized.pose(output)); same(output, sentinel);
    };
    for (int axis = 0; axis < 3; ++axis) for (float value : {nan, inf, -inf, -10000.1f, 10000.1f}) {
        ExplorationCameraFrame f; f.targetPosition[axis] = value; rejects(f);
    }
    const std::vector<std::pair<float ExplorationCameraFrame::*, std::vector<float>>> scalars{
        {&ExplorationCameraFrame::aspectRatio, {nan, inf, -inf, .249f, 8.001f}},
        {&ExplorationCameraFrame::deltaSeconds, {nan, inf, -inf, -.001f, .251f}},
        {&ExplorationCameraFrame::zoomDelta, {nan, inf, -inf, -1000.1f, 1000.1f}},
        {&ExplorationCameraFrame::zoomRate, {nan, inf, -inf, -1000.1f, 1000.1f}}};
    for (const auto& [member, values] : scalars) for (float value : values) {
        ExplorationCameraFrame f; f.*member = value; rejects(f);
    }
    for (int axis = 0; axis < 2; ++axis) {
        for (float value : {nan, inf, -inf, -100.01f, 100.01f}) {
            ExplorationCameraFrame f; f.lookDeltaRadians[axis] = value; rejects(f);
        }
        for (float value : {nan, inf, -inf, -20.01f, 20.01f}) {
            ExplorationCameraFrame f; f.lookRateRadiansPerSecond[axis] = value; rejects(f);
        }
    }
    // Valid controls are staged before the desired-eye bounds failure; none may leak.
    ExplorationCameraFrame invalid; invalid.targetPosition = {9999, 0, 0}; invalid.deltaSeconds = .2f;
    invalid.lookDeltaRadians = {pi / 2, .2f}; invalid.zoomDelta = 5;
    rejects(invalid);
    output = sentinel; CHECK(!rig.reset({0, 0, 10000}, 1, output, &world, &error)); same(output, sentinel); same(read(rig), before);
    CHECK(!rig.reset({}, .1f, output, &world, &error)); same(output, sentinel); same(read(rig), before);
    ExplorationCameraFrame next; next.deltaSeconds = .1f; next.targetPosition = {1, 0, 0}; next.lookDeltaRadians = {.1f, 0}; next.zoomDelta = .25f;
    same(step(rig, next, &world), step(control, next, &world));

    ExplorationCameraSettings offset; ExplorationCameraRig offsetRig(offset);
    CHECK(!offsetRig.reset({0, 10000, 0}, 1, output, nullptr, &error));
    same(output, sentinel);
    // Desired eye is in range, but position + unit-forward exceeds the envelope.
    auto tiny = flat(); tiny.desiredDistance = tiny.minDistance = tiny.maxDistance = .1f;
    ExplorationCameraRig lookBounds(tiny);
    CHECK(!lookBounds.reset({0, 0, -10000}, 1, output, nullptr, &error)); same(output, sentinel);
    CHECK(!lookBounds.pose(output)); same(output, sentinel);

    // Inclusive boundaries, including extreme valid controls, dt and aspect.
    for (float aspect : {.25f, 8.0f}) {
        ExplorationCameraRig boundary;
        ExplorationCameraFrame f; f.aspectRatio = aspect; f.deltaSeconds = .25f;
        f.lookDeltaRadians = {-100, 100}; f.lookRateRadiansPerSecond = {-20, 20}; f.zoomDelta = -1000; f.zoomRate = 1000;
        const auto p = step(boundary, f); CHECK(p.aspectRatio == aspect && p.pitchRadians == 1.4f && p.desiredDistance == .5f);
    }
    ExplorationCameraRig boundary(flat());
    CHECK(reset(boundary, {10000, 10000, 0}).pivot == glm::vec3(10000, 10000, 0));
    CHECK(reset(boundary, {-10000, -10000, 0}).pivot == glm::vec3(-10000, -10000, 0));
}

void relativeIntent() {
    glm::vec2 out;
    const auto expect = [&](glm::vec2 input, float yaw, glm::vec2 expected) {
        std::string error = "old"; CHECK(cameraRelativeMove(input, yaw, out, &error) && error.empty()); CHECK(near(out, expected));
    };
    expect({0, -1}, 0, {0, -1}); expect({1, 0}, 0, {1, 0}); expect({0, 1}, 0, {0, 1}); expect({-1, 0}, 0, {-1, 0});
    expect({0, -1}, pi / 2, {-1, 0}); expect({1, 0}, pi / 2, {0, -1});
    expect({0, -1}, -pi / 2, {1, 0}); expect({0, -1}, pi, {0, 1});
    expect({0, -1}, pi / 4, {-std::sqrt(.5f), -std::sqrt(.5f)});
    expect({.3f, .4f}, 0, {.3f, .4f}); expect({.3f, .4f}, pi / 2, {.4f, -.3f});
    expect({1, 1}, 0, {std::sqrt(.5f), std::sqrt(.5f)}); expect({0, 0}, 1e6f, {0, 0});
    const float huge = std::numeric_limits<float>::max();
    expect({huge, -huge}, 0, {std::sqrt(.5f), -std::sqrt(.5f)});
    expect({huge, 0}, pi / 2, {0, -1}); expect({-huge, huge}, pi / 2, {std::sqrt(.5f), std::sqrt(.5f)});
    const float tiny = std::numeric_limits<float>::min();
    CHECK(cameraRelativeMove({tiny, -tiny}, 0, out) && out == glm::vec2(tiny, -tiny));
    for (float yaw : {-1e6f, 1e6f}) {
        CHECK(cameraRelativeMove({.3f, -.4f}, yaw, out)); CHECK(near(glm::length(out), .5f));
    }
    const float nan = std::numeric_limits<float>::quiet_NaN(); const float inf = std::numeric_limits<float>::infinity();
    for (auto input : {glm::vec2(nan, 0), glm::vec2(0, nan), glm::vec2(inf, 0), glm::vec2(0, -inf)}) {
        out = {7, 8}; std::string error; CHECK(!cameraRelativeMove(input, 0, out, &error) && !error.empty()); CHECK(out == glm::vec2(7, 8));
    }
    for (float yaw : {nan, inf, -inf, 1000001.0f, -1000001.0f}) {
        out = {7, 8}; CHECK(!cameraRelativeMove({}, yaw, out)); CHECK(out == glm::vec2(7, 8));
    }
}

void ownershipAndIndependentInstances() {
    static_assert(!std::is_copy_constructible_v<ExplorationCameraRig> && !std::is_move_constructible_v<ExplorationCameraRig>);
    static_assert(!std::is_copy_assignable_v<ExplorationCameraRig> && !std::is_move_assignable_v<ExplorationCameraRig>);
    CollisionWorld world; box(world, {0, 0, 3}, {5, 5, .1f});
    ExplorationCameraRig owner(flat()); const auto before = reset(owner, {}, &world);
    std::exception_ptr failure;
    std::thread other([&] {
        try {
            ExplorationCameraPose output = before;
            const std::vector<std::function<void()>> calls{
                [&] { (void)owner.settings(); }, [&] { owner.pose(output); },
                [&] { owner.reset({}, 1, output); }, [&] { owner.step({}, output); }};
            for (const auto& call : calls) {
                bool threw = false; try { call(); } catch (const std::logic_error&) { threw = true; }
                CHECK(threw); same(output, before);
            }
            ExplorationCameraRig local(flat()); const auto localBefore = reset(local);
            ExplorationCameraFrame frame; frame.lookDeltaRadians = {.8f, .3f}; frame.zoomDelta = 4;
            for (bool resetCall : {false, true}) {
                output = before; bool threw = false;
                try { if (resetCall) local.reset({}, 1, output, &world); else local.step(frame, output, &world); }
                catch (const std::logic_error&) { threw = true; }
                CHECK(threw); same(output, before); same(read(local), localBefore);
            }
            CollisionWorld localWorld; box(localWorld, {0, 0, 2}, {5, 5, .1f});
            CHECK(near(reset(local, {}, &localWorld).actualDistance, 1.67f, .001f));
            CHECK(reset(local).actualDistance == 6);
        } catch (...) { failure = std::current_exception(); }
    });
    other.join(); if (failure) std::rethrow_exception(failure);
    same(read(owner), before);
    ExplorationCameraRig second(flat()); CHECK(reset(second).actualDistance == 6); same(read(owner), before);
}

struct SequenceResult { CharacterState character; ExplorationCameraPose camera; };
SequenceResult explorationSequence() {
    InputActions input;
    using Key = InputEvent::Key;
    CHECK(input.define({"move", ActionKind::Axis2D, {KeyBinding{Key::W, {0, -1}}, KeyBinding{Key::D, {1, 0}}}}));
    CHECK(input.define({"run", ActionKind::Button, {KeyBinding{Key::LShift}}}));
    CHECK(input.define({"orbit", ActionKind::Axis2D, {StickBinding{GamepadStick::Right}}}));
    ControllerId controller; CHECK(input.addController(controller) && input.selectController(controller));
    ControllerSnapshot snapshot; CHECK(input.submit(controller, snapshot));
    CollisionWorld world;
    box(world, {0, -.5f, 0}, {30, .5f, 30});
    const auto wall = box(world, {0, 2, -4}, {10, 3, .1f}, 0, false, 12);
    const auto boomWall = box(world, {0, 2, 3}, {10, 3, .1f}, 0, false, 34);
    CharacterSettings characterSettings;
    CharacterId id; CHECK(world.createCharacter(characterSettings, {0, .9f, 0}, id));
    CharacterState character; CHECK(world.characterState(id, character));
    auto cameraSettings = flat(); cameraSettings.pivotOffset = {0, 1, 0}; cameraSettings.followHalfLifeSeconds = 0;
    ExplorationCameraRig rig(cameraSettings); auto camera = reset(rig, character.center, &world);
    CHECK(camera.obstruction && camera.obstruction->collider.id == boomWall);
    const auto tick = [&] {
        input.advanceFrame();
        ActionState move, run, orbit; CHECK(input.state("move", move) && input.state("run", run) && input.state("orbit", orbit));
        CharacterInput intent; CHECK(cameraRelativeMove(move.value, camera.yawRadians, intent.move)); intent.run = run.down;
        CHECK(world.stepCharacter(id, intent, character));
        ExplorationCameraFrame frame; frame.targetPosition = character.center; frame.deltaSeconds = characterSettings.fixedDeltaSeconds;
        frame.lookRateRadiansPerSecond = orbit.value * (pi / 2);
        camera = step(rig, frame, &world);
        CHECK(near(camera.pivot, character.center + cameraSettings.pivotOffset));
        if (!camera.originBlocked) clearEye(world, camera, cameraSettings);
    };
    for (int i = 0; i < 20; ++i) tick();
    const auto start = character.center;
    CHECK(input.ingest(InputEvent::KeyDownEvent(Key::W)));
    for (int i = 0; i < 30; ++i) tick();
    CHECK(near(character.center.z, start.z - 1, .003f));
    CHECK(input.ingest(InputEvent::KeyDownEvent(Key::LShift)));
    for (int i = 0; i < 20; ++i) tick();
    CHECK(near(character.center.z, start.z - 2.5f, .003f));
    for (int i = 0; i < 60; ++i) tick();
    CHECK(character.center.z >= -3.551f && character.center.z < -3.5f);
    CHECK(std::abs(character.velocity.z) < .001f && character.groundState == CharacterGroundState::Grounded);
    CHECK(world.contains(wall));
    input.setFocused(false); tick();
    ActionState released; CHECK(input.state("move", released) && released.released && !released.down);
    CHECK(input.state("run", released) && released.released && !released.down);
    const auto stopped = character.center;
    for (int i = 0; i < 10; ++i) tick(); CHECK(near(character.center, stopped, .001f));
    input.setFocused(true); tick(); CHECK(input.state("move", released) && !released.down);
    // Controller rate rotates the rig while focus-released movement stays neutral.
    snapshot.axes[static_cast<std::size_t>(GamepadAxis::RightX)] = 1;
    CHECK(input.submit(controller, snapshot)); tick(); CHECK(camera.yawRadians == 0);
    snapshot = {}; CHECK(input.submit(controller, snapshot)); tick();
    snapshot.axes[static_cast<std::size_t>(GamepadAxis::RightX)] = 1;
    CHECK(input.submit(controller, snapshot));
    for (int i = 0; i < 60; ++i) tick();
    CHECK(near(camera.yawRadians, pi / 2, .00003f));
    snapshot = {}; CHECK(input.submit(controller, snapshot));
    CHECK(input.ingest(InputEvent::KeyDownEvent(Key::W)));
    for (int i = 0; i < 30; ++i) tick();
    CHECK(near(character.center.x, stopped.x - 1, .004f));
    CHECK(near(character.center.z, stopped.z, .004f));
    CHECK(input.ingest(InputEvent::KeyUpEvent(Key::W))); tick();
    // Explicit teleport/camera cut recomputes obstruction; then removal recovers analytically.
    CHECK(world.teleportCharacter(id, {0, .91f, 0})); CHECK(world.characterState(id, character));
    camera = reset(rig, character.center, &world);
    CHECK(camera.yawRadians == 0 && camera.desiredDistance == 6 && camera.obstruction->collider.id == boomWall);
    const float obstructedLength = camera.actualDistance;
    CHECK(world.remove(boomWall));
    for (int i = 0; i < 9; ++i) tick();
    CHECK(near(camera.actualDistance, (obstructedLength + 6) / 2, .0001f));
    CHECK(!camera.obstructed);
    camera = reset(rig, character.center, &world); CHECK(camera.actualDistance == 6);
    return {character, camera};
}
void repeatedComposition() {
    const auto a = explorationSequence(); const auto b = explorationSequence();
    CHECK(a.character.center == b.character.center && a.character.velocity == b.character.velocity);
    CHECK(a.character.groundState == b.character.groundState); same(a.camera, b.camera);
}
} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--wrong-thread-destruction") {
        auto* rig = new ExplorationCameraRig;
        std::thread wrong([&] {
            std::set_terminate([] { std::_Exit(73); });
            delete rig;
        });
        wrong.join();
        return 1;
    }
    const std::vector<std::pair<const char*, std::function<void()>>> groups{
        {"initial/reset framing and mathematical projection", initialResetAndProjection},
        {"controls, rates, deltas and authored limits", controlsAndLimits},
        {"analytic follow/recovery and immediate obstruction", dampingAndRecovery},
        {"real sphere/box/mesh geometry and metadata", realGeometryAndMetadata},
        {"penetration, touching, filters and borrowing", overlapTouchingAndFilters},
        {"settings validation and inclusive boundaries", settingsValidation},
        {"frame validation and transactional state", frameValidationAndTransactions},
        {"relative movement axes and robust analog math", relativeIntent},
        {"ownership and independent rigs/worlds", ownershipAndIndependentInstances},
        {"repeatable input/character/camera composition", repeatedComposition}};
    try {
        for (const auto& [name, run] : groups) { run(); std::cout << "PASS: " << name << '\n'; }
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
