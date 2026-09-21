#include "engine/collision/CollisionWorld.h"

#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>

using namespace engine::collision;

namespace {
#define CHECK(condition) do { if (!(condition)) throw std::runtime_error(std::string(__func__) + ":" + std::to_string(__LINE__) + " " #condition); } while (false)
bool near(float a, float b, float epsilon = 0.001f) { return std::abs(a - b) <= epsilon; }
bool near(glm::vec3 a, glm::vec3 b, float epsilon = 0.001f) { return glm::length(a - b) <= epsilon; }
ColliderId box(CollisionWorld& world, glm::vec3 position, glm::vec3 extents,
               std::uint32_t layer = 0, bool sensor = false, std::uint64_t data = 0) {
    ColliderId result;
    std::string error;
    StaticColliderDesc desc;
    desc.position = position;
    desc.layer = layer;
    desc.sensor = sensor;
    desc.userData = data;
    if (!world.addBox(desc, extents, result, &error)) throw std::runtime_error(error);
    return result;
}
const std::array<glm::vec3, 4> floorVertices{{{-4, 0, -4}, {-4, 0, 4}, {4, 0, 4}, {4, 0, -4}}};
const std::array<std::uint32_t, 6> quadIndices{{0, 1, 2, 0, 2, 3}};
ColliderId mesh(CollisionWorld& world, std::span<const glm::vec3> vertices,
                std::span<const std::uint32_t> indices = quadIndices) {
    ColliderId result;
    std::string error;
    if (!world.addTriangleMesh({}, vertices, indices, result, &error)) throw std::runtime_error(error);
    return result;
}
Hit ray(const CollisionWorld& world, glm::vec3 origin, glm::vec3 displacement, const QueryFilter& filter = {}) {
    std::optional<Hit> result;
    CHECK(world.rayCast(origin, displacement, result, filter));
    CHECK(result.has_value());
    CHECK(near(glm::length(result->normal), 1));
    CHECK(result->fraction >= 0 && result->fraction <= 1);
    return *result;
}
Hit sweep(const CollisionWorld& world, Capsule capsule, glm::vec3 displacement, const QueryFilter& filter = {}) {
    std::optional<Hit> result;
    CHECK(world.sweepCapsule(capsule, displacement, result, filter));
    CHECK(result.has_value());
    CHECK(near(glm::length(result->normal), 1));
    CHECK(result->fraction >= 0 && result->fraction <= 1);
    return *result;
}
void emptyAndBoxes() {
    CollisionWorld world;
    std::optional<Hit> hit = Hit{};
    std::vector<ColliderInfo> overlaps(1);
    std::string error = "old";
    CHECK(world.rayCast({0, 0, 0}, {10, 0, 0}, hit, {}, &error) && !hit && error.empty());
    hit = Hit{};
    CHECK(world.sweepCapsule({}, {10, 0, 0}, hit) && !hit);
    CHECK(world.overlapCapsule({}, overlaps) && overlaps.empty());
    box(world, {8, 0, 0}, {1, 2, 2});
    const auto wall = box(world, {5, 0, 0}, {0.05f, 2, 2}, 0, false, 987);
    const auto r = ray(world, {0, 0, 0}, {10, 0, 0});
    CHECK(r.collider.id == wall && r.collider.userData == 987 && !r.collider.sensor);
    CHECK(near(r.fraction, 0.495f) && near(r.point, {4.95f, 0, 0}) && near(r.normal, {-1, 0, 0}));
    CHECK(near(ray(world, {0, 0, 0}, {4.95f, 0, 0}).fraction, 1));
    const auto s = sweep(world, {}, {10, 0, 0});
    CHECK(s.collider.id == wall && near(s.fraction, 0.445f) && near(s.normal, {-1, 0, 0}));
    CHECK(near(s.point.x, 4.95f) && !s.initialOverlap);
    CHECK(near(sweep(world, {}, {4.45f, 0, 0}).fraction, 1));
    CHECK(near(sweep(world, {{0, 0, 0}, 0.5f, 0}, {10, 0, 0}).fraction, 0.445f));
    hit = r;
    CHECK(world.rayCast({0, 5, 0}, {10, 0, 0}, hit) && !hit);
    hit = s;
    CHECK(world.sweepCapsule({{0, 5, 0}, 0.5f, 0.5f}, {10, 0, 0}, hit) && !hit);

    CollisionWorld rotated;
    StaticColliderDesc desc;
    desc.position = {2, 0, 1};
    desc.rotation = glm::angleAxis(glm::quarter_pi<float>(), glm::vec3(0, 1, 0)) * 3.0f;
    ColliderId id;
    CHECK(rotated.addBox(desc, {1, 2, 0.5f}, id));
    const auto q = glm::normalize(desc.rotation);
    const auto start = desc.position + q * glm::vec3(-3, 0, 0);
    const auto displacement = q * glm::vec3(6, 0, 0);
    const auto rotatedHit = ray(rotated, start, displacement);
    CHECK(near(rotatedHit.fraction, 1.0f / 3.0f) && near(rotatedHit.normal, q * glm::vec3(-1, 0, 0)));
    const auto rotatedSweep = sweep(rotated, {start, 0.5f, 0}, displacement);
    CHECK(near(rotatedSweep.fraction, 0.25f) && near(rotatedSweep.normal, q * glm::vec3(-1, 0, 0)));
}
void meshesAndPrecision() {
    CollisionWorld floor;
    const auto floorId = mesh(floor, floorVertices);
    auto hit = ray(floor, {0, 2, 0}, {0, -4, 0});
    CHECK(hit.collider.id == floorId && near(hit.fraction, 0.5f) && near(hit.normal, {0, 1, 0}));
    hit = ray(floor, {0, -2, 0}, {0, 4, 0});
    CHECK(near(hit.fraction, 0.5f) && near(hit.normal, {0, 1, 0})); // Winding normal, including back side.
    CHECK(near(ray(floor, {0, 2, 0}, {0, -2, 0}).fraction, 1));
    hit = sweep(floor, {{0, 3, 0}, 0.5f, 0.5f}, {0, -4, 0});
    CHECK(near(hit.fraction, 0.5f) && near(hit.normal, {0, 1, 0}) && near(hit.point.y, 0));
    hit = sweep(floor, {{0, -3, 0}, 0.5f, 0.5f}, {0, 4, 0});
    CHECK(near(hit.fraction, 0.5f) && near(hit.normal, {0, -1, 0}));
    CHECK(near(sweep(floor, {{0, 3, 0}, 0.5f, 0.5f}, {0, -2, 0}).fraction, 1));
    std::vector<ColliderInfo> overlap;
    CHECK(floor.overlapCapsule({{0, 0.2f, 0}, 0.5f, 0.5f}, overlap));
    CHECK(overlap.size() == 1 && overlap.front().id == floorId);
    CHECK(floor.overlapCapsule({{0, -0.2f, 0}, 0.5f, 0.5f}, overlap) && overlap.size() == 1);
    hit = sweep(floor, {{0, 0.2f, 0}, 0.5f, 0.5f}, {0, 3, 0});
    CHECK(hit.initialOverlap && hit.fraction == 0);

    CollisionWorld ramp;
    const std::array<glm::vec3, 4> rampVertices{{{-2, -2, -3}, {-2, -2, 3}, {2, 2, 3}, {2, 2, -3}}};
    mesh(ramp, rampVertices);
    const glm::vec3 normal = glm::normalize(glm::vec3(-1, 1, 0));
    hit = ray(ramp, {0, 3, 0}, {0, -6, 0});
    CHECK(near(hit.fraction, 0.5f) && near(hit.normal, normal));
    hit = sweep(ramp, {{0, 4, 0}, 0.5f, 0.5f}, {0, -6, 0});
    CHECK(near(hit.fraction, (3.5f - std::sqrt(0.5f)) / 6.0f) && near(hit.normal, normal));
    CHECK(near(hit.point.x, hit.point.y));

    CollisionWorld transformed;
    StaticColliderDesc transform;
    transform.position = {3, 2, -1};
    transform.rotation = glm::angleAxis(glm::quarter_pi<float>(), glm::vec3(0, 0, 1));
    ColliderId transformedId;
    CHECK(transformed.addTriangleMesh(transform, floorVertices, quadIndices, transformedId));
    // A 45-degree Z rotation maps the local +Y plane normal to (-sqrt(1/2), sqrt(1/2), 0).
    const glm::vec3 planeNormal(-std::sqrt(0.5f), std::sqrt(0.5f), 0);
    const glm::vec3 start = transform.position + 3.0f * planeNormal;
    const glm::vec3 displacement = -6.0f * planeNormal;
    hit = ray(transformed, start, displacement);
    CHECK(hit.collider.id == transformedId && near(hit.fraction, 0.5f));
    CHECK(near(hit.point, transform.position) && near(hit.normal, planeNormal));
    // An upright capsule reaches this plane when center distance is r + h * normal.y.
    const float supportDistance = 0.5f + 0.5f * std::sqrt(0.5f);
    hit = sweep(transformed, {start, 0.5f, 0.5f}, displacement);
    CHECK(hit.collider.id == transformedId && !hit.initialOverlap);
    CHECK(near(hit.fraction, (3.0f - supportDistance) / 6.0f));
    CHECK(near(hit.point, transform.position + glm::vec3(-0.25f, -0.25f, 0)));
    CHECK(near(hit.normal, planeNormal));
    CHECK(transformed.overlapCapsule({transform.position + 0.75f * planeNormal, 0.5f, 0.5f}, overlap));
    CHECK(overlap.size() == 1 && overlap.front().id == transformedId);
    CHECK(transformed.overlapCapsule({transform.position + planeNormal, 0.5f, 0.5f}, overlap) && overlap.empty());

    CollisionWorld corner;
    box(corner, {0, 0, 0}, {1, 1, 1});
    CHECK(corner.overlapCapsule({{1.8f, 0, 1.8f}, 1, 0}, overlap) && overlap.empty());
    CHECK(corner.overlapCapsule({{1.6f, 0, 1.6f}, 1, 0}, overlap) && overlap.size() == 1);
    hit = sweep(corner, {{0.9f, 0, 0}, 0.5f, 0}, {3, 0, 0});
    CHECK(hit.initialOverlap && hit.fraction == 0 && near(hit.normal, {1, 0, 0}));
    hit = ray(corner, {0.9f, 0, 0}, {3, 0, 0});
    CHECK(hit.fraction == 0 && near(hit.normal, {1, 0, 0}) && near(hit.point, {0.9f, 0, 0}));

    CollisionWorld triangle;
    const std::array<glm::vec3, 3> triangleVertices{{{0, 0, 0}, {0, 0, 4}, {4, 0, 0}}};
    const std::array<std::uint32_t, 3> triangleIndices{{0, 1, 2}};
    mesh(triangle, triangleVertices, triangleIndices);
    CHECK(triangle.overlapCapsule({{3, 0, 3}, 0.2f, 0}, overlap) && overlap.empty());
    CHECK(triangle.overlapCapsule({{1, 0.1f, 1}, 0.2f, 0}, overlap) && overlap.size() == 1);
}
void filtering() {
    CollisionWorld world;
    const std::array ids{
        box(world, {3, 0, 0}, {0.5f, 2, 2}, 0, false, 10),
        box(world, {3, 0, 0}, {0.5f, 2, 2}, 31, false, 20),
        box(world, {3, 0, 0}, {0.5f, 2, 2}, 0, true, 30),
        box(world, {3, 0, 0}, {0.5f, 2, 2}, 31, true, 40)};
    CollisionWorld other;
    const auto foreign = box(other, {0, 0, 0}, {1, 1, 1});
    for (auto policy : {SensorPolicy::Exclude, SensorPolicy::Include, SensorPolicy::Only}) {
        for (auto mask : {0u, 1u, 0x80000000u, 0x80000001u}) {
            for (auto ignored : {ColliderId{}, ids[0], ids[3], foreign}) {
                QueryFilter filter{mask, policy, ignored};
                std::vector<ColliderId> expected;
                for (std::size_t i = 0; i < ids.size(); ++i) {
                    const bool sensor = i >= 2;
                    const auto layer = i % 2 == 0 ? 0u : 31u;
                    if (ids[i] != ignored && (mask & (1u << layer)) != 0
                        && (policy == SensorPolicy::Include || sensor == (policy == SensorPolicy::Only))) expected.push_back(ids[i]);
                }
                std::optional<Hit> r, s;
                std::vector<ColliderInfo> o;
                CHECK(world.rayCast({0, 0, 0}, {6, 0, 0}, r, filter));
                CHECK(world.sweepCapsule({}, {6, 0, 0}, s, filter));
                CHECK(world.overlapCapsule({{3, 0, 0}, 0.5f, 0.5f}, o, filter));
                CHECK(r.has_value() == !expected.empty() && s.has_value() == !expected.empty());
                CHECK(o.size() == expected.size());
                for (std::size_t i = 0; i < o.size(); ++i) CHECK(o[i].id == expected[i]);
                auto checkMetadata = [&](const ColliderInfo& info) {
                    std::size_t index = 0;
                    while (index < ids.size() && ids[index] != info.id) ++index;
                    CHECK(index < ids.size());
                    CHECK(info.userData == (index + 1) * 10 && info.sensor == (index >= 2));
                    CHECK(info.layer == (index % 2 == 0 ? 0u : 31u));
                    CHECK(std::find(expected.begin(), expected.end(), info.id) != expected.end());
                };
                for (const auto& info : o) checkMetadata(info);
                if (r) checkMetadata(r->collider);
                if (s) checkMetadata(s->collider);
            }
        }
    }
}
void lifecycle() {
    static_assert(!std::is_copy_constructible_v<CollisionWorld> && !std::is_move_constructible_v<CollisionWorld>);
    ColliderId last;
    for (int iteration = 0; iteration < 20; ++iteration) {
        CollisionWorld world({1, 4, 2});
        CollisionWorld second;
        const auto id = box(world, {3, 0, 0}, {1, 1, 1});
        const auto foreign = box(second, {3, 0, 0}, {1, 1, 1});
        CHECK(id > last && foreign > id && world.colliderCount() == 1);
        CHECK(!world.contains({}) && !world.remove({}) && !world.contains(foreign) && !world.remove(foreign));
        CHECK(!second.contains(id) && !second.remove(id));
        ColliderId sentinel = foreign;
        std::string error;
        CHECK(!world.addBox({}, {1, 1, 1}, sentinel, &error) && sentinel == foreign && !error.empty());
        CHECK(ray(world, {0, 0, 0}, {6, 0, 0}).collider.id == id);
        CHECK(world.remove(id) && !world.remove(id) && !world.contains(id) && world.colliderCount() == 0);
        const auto replacement = box(world, {3, 0, 0}, {1, 1, 1});
        CHECK(replacement > foreign);
        world.clear();
        world.clear();
        CHECK(!world.contains(replacement) && !world.remove(replacement) && world.colliderCount() == 0);
        std::optional<Hit> miss;
        CHECK(world.rayCast({0, 0, 0}, {6, 0, 0}, miss) && !miss);
        last = replacement;
    }
    CollisionWorld world({2, 4, 2});
    ColliderId id;
    {
        std::vector<glm::vec3> vertices(floorVertices.begin(), floorVertices.end());
        std::vector<std::uint32_t> indices(quadIndices.begin(), quadIndices.end());
        CHECK(world.addTriangleMesh({}, vertices, indices, id));
        vertices.assign(4, glm::vec3(99));
        indices.assign(6, 99);
    }
    CHECK(ray(world, {0, 2, 0}, {0, -4, 0}).collider.id == id);
    const auto second = box(world, {10, 0, 0}, {1, 1, 1});
    CHECK(world.colliderCount() == 2 && second > id);
    CHECK(world.remove(id));
    mesh(world, floorVertices);
    CHECK(world.colliderCount() == 2);
}
void threadOwnership() {
    CollisionWorld world;
    const auto ownerId = box(world, {3, 0, 0}, {1, 1, 1});
    const Hit sentinel = ray(world, {0, 0, 0}, {6, 0, 0});
    ColliderId workerId;
    std::exception_ptr workerFailure;
    std::thread worker([&] {
        try {
            bool rejectedRead = false;
            try { (void)world.colliderCount(); }
            catch (const std::logic_error&) { rejectedRead = true; }
            CHECK(rejectedRead);
            std::optional<Hit> output = sentinel;
            bool rejectedQuery = false;
            try { (void)world.rayCast({0, 0, 0}, {6, 0, 0}, output); }
            catch (const std::logic_error&) { rejectedQuery = true; }
            CHECK(rejectedQuery && output && output->collider.id == ownerId);
            CHECK(output->fraction == sentinel.fraction && output->point == sentinel.point);

            // This world is constructed, used and destroyed on its own thread
            // while the original world remains alive and idle on its owner.
            CollisionWorld independent;
            workerId = box(independent, {3, 0, 0}, {1, 1, 1});
            CHECK(workerId > ownerId);
            CHECK(!independent.contains(ownerId) && !independent.remove(ownerId));
            CHECK(ray(independent, {0, 0, 0}, {6, 0, 0}).collider.id == workerId);
            CHECK(independent.colliderCount() == 1);
        } catch (...) { workerFailure = std::current_exception(); }
    });
    worker.join();
    if (workerFailure) std::rethrow_exception(workerFailure);
    CHECK(world.colliderCount() == 1 && world.contains(ownerId));
    CHECK(!world.contains(workerId) && !world.remove(workerId));
    CHECK(ray(world, {0, 0, 0}, {6, 0, 0}).collider.id == ownerId);
    CHECK(near(sweep(world, {}, {6, 0, 0}).fraction, 0.25f));
}
void invalidInputs() {
    CollisionWorld world({2, 4, 2});
    const auto valid = box(world, {3, 0, 0}, {1, 1, 1});
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    ColliderId output = valid;
    std::string error;
    auto rejected = [&](bool success) {
        CHECK(!success && output == valid && !error.empty() && world.colliderCount() == 1);
        CHECK(ray(world, {0, 0, 0}, {6, 0, 0}).collider.id == valid);
        error.clear();
    };
    for (float value : {0.0f, -1.0f, nan, inf, 1'000'001.0f, 0.00001f})
        rejected(world.addBox({}, {value, 1, 1}, output, &error));
    for (float value : {nan, inf, 1'000'001.0f}) {
        StaticColliderDesc desc;
        desc.position.y = value;
        rejected(world.addBox(desc, {1, 1, 1}, output, &error));
        auto vertices = floorVertices;
        vertices[0].z = value;
        rejected(world.addTriangleMesh({}, vertices, quadIndices, output, &error));
    }
    for (auto rotation : {glm::quat(0, 0, 0, 0), glm::quat(nan, 0, 0, 0), glm::quat(1, inf, 0, 0)}) {
        StaticColliderDesc desc;
        desc.rotation = rotation;
        rejected(world.addBox(desc, {1, 1, 1}, output, &error));
        rejected(world.addTriangleMesh(desc, floorVertices, quadIndices, output, &error));
    }
    StaticColliderDesc desc;
    desc.layer = 32;
    rejected(world.addBox(desc, {1, 1, 1}, output, &error));
    rejected(world.addTriangleMesh(desc, floorVertices, quadIndices, output, &error));
    rejected(world.addTriangleMesh({}, {}, {}, output, &error));
    rejected(world.addTriangleMesh({}, floorVertices, {}, output, &error));
    const std::array<std::uint32_t, 4> nonTriangle{{0, 1, 2, 3}};
    rejected(world.addTriangleMesh({}, floorVertices, nonTriangle, output, &error));
    const std::array<std::uint32_t, 3> badIndex{{0, 1, 4}}, duplicate{{0, 0, 1}};
    rejected(world.addTriangleMesh({}, floorVertices, badIndex, output, &error));
    rejected(world.addTriangleMesh({}, floorVertices, duplicate, output, &error));
    const std::array<glm::vec3, 3> collinear{{{0, 0, 0}, {1, 1, 1}, {2, 2, 2}}};
    const std::array<std::uint32_t, 3> tri{{0, 1, 2}};
    rejected(world.addTriangleMesh({}, collinear, tri, output, &error));
    const std::array<glm::vec3, 5> excessVertices{};
    rejected(world.addTriangleMesh({}, excessVertices, tri, output, &error));
    const std::array<std::uint32_t, 9> excessTriangles{{0, 1, 2, 0, 2, 3, 0, 1, 2}};
    rejected(world.addTriangleMesh({}, floorVertices, excessTriangles, output, &error));

    const Hit sentinel = ray(world, {0, 0, 0}, {6, 0, 0});
    std::optional<Hit> hit = sentinel;
    std::vector<ColliderInfo> overlap{sentinel.collider};
    auto queryRejected = [&](bool success) {
        CHECK(!success && !error.empty());
        CHECK(hit && hit->collider.id == valid && hit->fraction == sentinel.fraction && hit->point == sentinel.point);
        CHECK(overlap.size() == 1 && overlap[0].id == valid && world.colliderCount() == 1);
        error.clear();
    };
    for (auto displacement : {glm::vec3(0), glm::vec3(nan, 0, 0), glm::vec3(inf, 0, 0), glm::vec3(2'000'000, 0, 0)}) {
        queryRejected(world.rayCast({0, 0, 0}, displacement, hit, {}, &error));
        queryRejected(world.sweepCapsule({}, displacement, hit, {}, &error));
    }
    for (float value : {nan, inf, 1'000'001.0f}) {
        queryRejected(world.rayCast({value, 0, 0}, {1, 0, 0}, hit, {}, &error));
        Capsule c;
        c.center.x = value;
        queryRejected(world.sweepCapsule(c, {1, 0, 0}, hit, {}, &error));
        queryRejected(world.overlapCapsule(c, overlap, {}, &error));
    }
    for (float value : {0.0f, -1.0f, nan, inf}) {
        Capsule c;
        c.radius = value;
        queryRejected(world.sweepCapsule(c, {1, 0, 0}, hit, {}, &error));
        queryRejected(world.overlapCapsule(c, overlap, {}, &error));
    }
    for (float value : {-1.0f, nan, inf}) {
        Capsule c;
        c.cylinderHalfHeight = value;
        queryRejected(world.sweepCapsule(c, {1, 0, 0}, hit, {}, &error));
        queryRejected(world.overlapCapsule(c, overlap, {}, &error));
    }
    QueryFilter bad;
    bad.sensors = static_cast<SensorPolicy>(99);
    queryRejected(world.rayCast({0, 0, 0}, {6, 0, 0}, hit, bad, &error));
    queryRejected(world.sweepCapsule({}, {6, 0, 0}, hit, bad, &error));
    queryRejected(world.overlapCapsule({}, overlap, bad, &error));
    CHECK(ray(world, {0, 0, 0}, {6, 0, 0}).collider.id == valid);
}
void configurationBounds() {
    for (auto options : {CollisionWorldOptions{0, 3, 1}, {65537, 3, 1}, {1, 2, 1}, {1, 1'000'001, 1}, {1, 3, 0}, {1, 3, 1'000'001}}) {
        bool threw = false;
        try { CollisionWorld world(options); } catch (const std::invalid_argument&) { threw = true; }
        CHECK(threw);
    }
    CollisionWorld lower({1, 3, 1});
    const std::array<glm::vec3, 3> vertices{{{0, 0, 0}, {0, 0, 2}, {2, 0, 0}}};
    const std::array<std::uint32_t, 3> indices{{0, 1, 2}};
    mesh(lower, vertices, indices);
    CollisionWorld upper({65536, 1'000'000, 1'000'000});
    CHECK(upper.colliderCount() == 0);
    box(upper, {0, 0, 0}, {1, 1, 1});
    CollisionWorld numeric;
    ColliderId id;
    StaticColliderDesc desc;
    desc.rotation = {std::numeric_limits<float>::max(), 0, 0, 0};
    CHECK(numeric.addBox(desc, {0.0001f, 1, 1}, id));
    numeric.clear();
    desc.rotation = {std::numeric_limits<float>::denorm_min(), 0, 0, 0};
    desc.position.x = 1'000'000;
    CHECK(numeric.addBox(desc, {1, 1, 1}, id));
}
std::vector<float> script() {
    CollisionWorld world;
    mesh(world, floorVertices);
    box(world, {5, 1, 0}, {0.05f, 2, 3});
    box(world, {2, 1, 0}, {0.5f, 1, 1}, 31, true, 123);
    const std::array<glm::vec3, 4> ramp{{{-2, 0, 5}, {-2, 0, 7}, {2, 2, 7}, {2, 2, 5}}};
    mesh(world, ramp);
    std::vector<float> result;
    for (int step = 0; step < 12; ++step) {
        const float x = -1.5f + static_cast<float>(step) * 0.25f;
        for (auto hit : {ray(world, {x, 4, 0}, {0, -6, 0}), ray(world, {x, 4, 6}, {0, -6, 0}),
                         sweep(world, {{x, 1.5f, 0}, 0.25f, 0.5f}, {10, 0, 0}),
                         ray(world, {0, 1, 0}, {4, 0, 0}, {0x80000000u, SensorPolicy::Only, {}})}) {
            result.insert(result.end(), {hit.fraction, hit.point.x, hit.point.y, hit.point.z,
                                       hit.normal.x, hit.normal.y, hit.normal.z, static_cast<float>(hit.collider.userData)});
        }
        std::vector<ColliderInfo> overlap;
        CHECK(world.overlapCapsule({{2, 1, 0}, 0.5f, 0.5f}, overlap, {0xffffffffu, SensorPolicy::Include, {}}));
        result.push_back(static_cast<float>(overlap.size()));
    }
    return result;
}
void repeatability() {
    const auto baseline = script();
    for (int i = 0; i < 5; ++i) CHECK(script() == baseline);
}
} // namespace

int main() {
    const std::array<std::pair<const char*, std::function<void()>>, 8> tests{{
        {"empty, boxes, nearest and endpoints", emptyAndBoxes},
        {"meshes, ramp, penetration and precise overlap", meshesAndPrecision},
        {"all query filters and metadata", filtering},
        {"identity, lifecycle, capacity and owned geometry", lifecycle},
        {"owner-thread rejection and independent threaded world", threadOwnership},
        {"invalid input and output preservation", invalidInputs},
        {"configuration and exact bounds", configurationBounds},
        {"authored sequence repeatability", repeatability}}};
    bool passed = true;
    for (const auto& [name, run] : tests) {
        try { run(); std::cout << "[PASS] " << name << '\n'; }
        catch (const std::exception& e) { passed = false; std::cerr << "[FAIL] " << name << ": " << e.what() << '\n'; }
    }
    return passed ? 0 : 1;
}
