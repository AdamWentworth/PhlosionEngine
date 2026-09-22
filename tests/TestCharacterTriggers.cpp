#include "engine/collision/CharacterTriggers.h"
#include "engine/collision/CollisionWorld.h"

#include <glm/gtc/constants.hpp>
#include <array>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <type_traits>

using namespace engine::collision;

namespace {
#define CHECK(condition) do { if (!(condition)) throw std::runtime_error(std::string(__func__) + ":" + std::to_string(__LINE__) + " " #condition); } while (false)
using Kind = TriggerEventKind;

void sameInfo(const ColliderInfo& a, const ColliderInfo& b) {
    CHECK(a.id == b.id && a.userData == b.userData && a.layer == b.layer && a.sensor == b.sensor);
}
void expect(const CharacterTriggerSample& sample, std::span<const ColliderInfo> overlaps,
            std::span<const TriggerEvent> events) {
    CHECK(sample.overlaps.size() == overlaps.size() && sample.events.size() == events.size());
    for (std::size_t i = 0; i < overlaps.size(); ++i) {
        sameInfo(sample.overlaps[i], overlaps[i]);
        CHECK(sample.overlaps[i].sensor);
        if (i) CHECK(sample.overlaps[i - 1].id < sample.overlaps[i].id);
    }
    for (std::size_t i = 0; i < events.size(); ++i) {
        CHECK(sample.events[i].kind == events[i].kind);
        sameInfo(sample.events[i].collider, events[i].collider);
        CHECK(sample.events[i].collider.sensor);
        if (i) CHECK(sample.events[i - 1].collider.id < sample.events[i].collider.id);
    }
}
void expect(const CharacterTriggerSample& sample, std::initializer_list<ColliderInfo> overlaps,
            std::initializer_list<TriggerEvent> events) {
    expect(sample, std::span(overlaps.begin(), overlaps.size()), std::span(events.begin(), events.size()));
}
void sameSample(const CharacterTriggerSample& a, const CharacterTriggerSample& b) { expect(a, b.overlaps, b.events); }
void sameState(const CharacterState& a, const CharacterState& b) {
    CHECK(a.center == b.center && a.velocity == b.velocity && a.groundState == b.groundState);
    CHECK(a.groundPoint == b.groundPoint && a.groundNormal == b.groundNormal);
    CHECK(a.support.has_value() == b.support.has_value());
    if (a.support) sameInfo(*a.support, *b.support);
}
CharacterState state(const CollisionWorld& w, CharacterId id) {
    CharacterState result; CHECK(w.characterState(id, result)); return result;
}
CharacterSettings floating() { CharacterSettings s; s.gravity = 0; return s; }
CharacterId character(CollisionWorld& w, glm::vec3 center = {0, 0, 0}, const CharacterSettings& s = floating()) {
    CharacterId id; CHECK(w.createCharacter(s, center, id)); return id;
}
ColliderInfo box(CollisionWorld& w, glm::vec3 center = {0, 0, 0}, glm::vec3 half = {1, 2, 1},
                 std::uint32_t layer = 0, bool sensor = true, std::uint64_t data = 42,
                 glm::quat rotation = {1, 0, 0, 0}) {
    StaticColliderDesc desc; desc.position = center; desc.rotation = rotation;
    desc.layer = layer; desc.sensor = sensor; desc.userData = data;
    ColliderId id; CHECK(w.addBox(desc, half, id));
    return {id, data, layer, sensor};
}
ColliderInfo mesh(CollisionWorld& w, const StaticColliderDesc& desc) {
    const std::array<glm::vec3, 4> vertices{{{-2, 0, -2}, {-2, 0, 2}, {2, 0, 2}, {2, 0, -2}}};
    const std::array<std::uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
    ColliderId id; CHECK(w.addTriangleMesh(desc, vertices, indices, id));
    return {id, desc.userData, desc.layer, desc.sensor};
}
CharacterTriggerSample sample(CollisionWorld& w, CharacterId id, const TriggerFilter& filter = {}) {
    CharacterTriggerSample result;
    std::string error = "previous diagnostic";
    CHECK(w.sampleCharacterTriggers(id, result, filter, &error) && error.empty());
    return result;
}
void ticks(CollisionWorld& w, CharacterId id, int count, CharacterInput input = {{1, 0}, false}) {
    CharacterState output;
    for (int i = 0; i < count; ++i) CHECK(w.stepCharacter(id, input, output));
}

void membershipAndOrdering() {
    CollisionWorld w;
    const auto id = character(w);
    expect(sample(w, id), {}, {});
    const auto first = box(w);
    const auto second = box(w, {0.2f, 0, 0}, {1, 1, 1}, 7, true, 999);
    expect(sample(w, id), {first, second}, {{Kind::Enter, first}, {Kind::Enter, second}});
    expect(sample(w, id), {first, second}, {{Kind::Stay, first}, {Kind::Stay, second}});
    ticks(w, id, 60);
    expect(sample(w, id), {}, {{Kind::Exit, first}, {Kind::Exit, second}});
    expect(sample(w, id), {}, {});
    ticks(w, id, 60, {{-1, 0}, false});
    expect(sample(w, id), {first, second}, {{Kind::Enter, first}, {Kind::Enter, second}});
    const auto third = box(w, {3, 0, 0}, {1, 1, 1}, 31, true, 123456);
    CHECK(w.teleportCharacter(id, {2.5f, 0, 0}));
    expect(sample(w, id), {third}, {{Kind::Exit, first}, {Kind::Exit, second}, {Kind::Enter, third}});
    // Events sort by ID across kinds, including Enter before an older current Exit.
    CHECK(w.teleportCharacter(id, {0, 0, 0}));
    expect(sample(w, id), {first, second}, {{Kind::Enter, first}, {Kind::Enter, second}, {Kind::Exit, third}});
}

void preciseShapesAndSurfaces() {
    CollisionWorld w;
    const auto sensor = box(w, {0, 0, 0}, {1, 1, 1});
    const auto id = character(w, {1.3f, 0, 1.3f});
    expect(sample(w, id), {}, {}); // AABBs overlap, circles at the box corner do not.
    CHECK(w.teleportCharacter(id, {1.2f, 0, 1.2f}));
    expect(sample(w, id), {sensor}, {{Kind::Enter, sensor}});
    CHECK(w.teleportCharacter(id, {1.353f, 0, 0}));
    expect(sample(w, id), {}, {{Kind::Exit, sensor}}); // No 0.005 controller padding.
    box(w, {1.353f, 0, 0}, {0.1f, 0.1f, 0.1f}, 0, false);
    character(w, {1.353f, 0, 0});
    expect(sample(w, id), {}, {}); // Neither solid colliders nor characters are triggers.

    CollisionWorld rotated;
    const auto rotation = glm::angleAxis(glm::quarter_pi<float>(), glm::vec3(0, 1, 0));
    const glm::vec3 translation{4, 1, -3};
    const auto rotatedSensor = box(rotated, translation, {2, 2, 0.1f}, 2, true, 71, rotation);
    const auto rotatedId = character(rotated, translation + rotation * glm::vec3(1.8f, 0, 0));
    expect(sample(rotated, rotatedId), {rotatedSensor}, {{Kind::Enter, rotatedSensor}});
    CHECK(rotated.teleportCharacter(rotatedId, translation + rotation * glm::vec3(0, 0, 0.46f)));
    expect(sample(rotated, rotatedId), {}, {{Kind::Exit, rotatedSensor}});

    CollisionWorld sheets;
    StaticColliderDesc desc; desc.position = {3, 1, 2}; desc.sensor = true; desc.layer = 31; desc.userData = 987;
    desc.rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0, 0, 1));
    const auto sheet = mesh(sheets, desc); // World-space plane x=3, two triangles sharing an edge.
    auto s = floating(); s.cylinderHalfHeight = 0;
    const auto ball = character(sheets, {3.2f, 1, 2}, s);
    expect(sample(sheets, ball), {sheet}, {{Kind::Enter, sheet}});
    CHECK(sheets.teleportCharacter(ball, {2.8f, 1, 2}));
    expect(sample(sheets, ball), {sheet}, {{Kind::Stay, sheet}}); // Both mesh sides, one event per mesh.
    CHECK(sheets.teleportCharacter(ball, {2.64f, 1, 2}));
    expect(sample(sheets, ball), {}, {{Kind::Exit, sheet}});
    CHECK(sheets.teleportCharacter(ball, {3.36f, 1, 2}));
    expect(sample(sheets, ball), {}, {});
    const auto capsule = character(sheets, {3, 1, 2});
    expect(sample(sheets, capsule), {sheet}, {{Kind::Enter, sheet}});
}

void filtersAndSnapshotOwnership() {
    CollisionWorld w, other;
    const auto low = box(w, {}, {1, 2, 1}, 0, true, 15);
    const auto high = box(w, {}, {1, 2, 1}, 31, true, 0xffffffffffffffffull);
    const auto foreign = box(other);
    auto s = floating(); s.layerMask = 0; s.ignore = high.id;
    const auto id = character(w, {}, s);
    auto output = sample(w, id); // Movement filtering must not affect trigger sampling.
    expect(output, {low, high}, {{Kind::Enter, low}, {Kind::Enter, high}});
    output.overlaps[0].userData = 888;
    output.events[1].collider.layer = 17;
    output.events.clear(); output.overlaps.clear();
    expect(sample(w, id, {1u << 31, {}}), {high}, {{Kind::Exit, low}, {Kind::Stay, high}});
    expect(sample(w, id, {0, {}}), {}, {{Kind::Exit, high}});
    expect(sample(w, id, {0, {}}), {}, {});
    expect(sample(w, id, {0xffffffffu, high.id}), {low}, {{Kind::Enter, low}});
    expect(sample(w, id, {0xffffffffu, foreign.id}), {low, high}, {{Kind::Stay, low}, {Kind::Enter, high}});
    const auto removed = box(w, {50, 0, 0}); CHECK(w.remove(removed.id));
    expect(sample(w, id, {0xffffffffu, removed.id}), {low, high}, {{Kind::Stay, low}, {Kind::Stay, high}});
    // Trigger filter changes cannot alter blocking movement, which uses its own mask/ignore.
    box(w, {1, 0, 0}, {0.1f, 2, 2}, 0, false);
    const auto blocked = character(w);
    sample(w, blocked, {0, {}});
    ticks(w, blocked, 60);
    CHECK(state(w, blocked).center.x < 0.56f);
    ticks(w, id, 60); CHECK(state(w, id).center.x > 1.9f);
}

void teleportFailuresAndSamplingPurity() {
    CollisionWorld w;
    const auto first = box(w);
    const auto second = box(w, {4, 0, 0});
    const auto solid = box(w, {8, 0, 0}, {1, 1, 1}, 0, false, 73);
    const auto floor = box(w, {0, -1.4f, 0}, {20, 0.5f, 20}, 0, false, 92);
    (void)floor;
    const auto id = character(w, {-4, 0, 0});
    expect(sample(w, id), {}, {});
    CHECK(w.teleportCharacter(id, {12, 0, 0}));
    expect(sample(w, id), {}, {}); // Teleported across both sensors without endpoint membership.
    CHECK(w.teleportCharacter(id, {0, 0, 0}));
    expect(sample(w, id), {first}, {{Kind::Enter, first}});
    const auto before = state(w, id);
    const auto count = w.colliderCount();
    const auto characterCount = w.characterCount();
    expect(sample(w, id), {first}, {{Kind::Stay, first}});
    sameState(before, state(w, id)); CHECK(w.colliderCount() == count && w.characterCount() == characterCount);
    std::optional<Hit> hit;
    CHECK(w.rayCast({8, 3, 0}, {0, -5, 0}, hit) && hit && hit->collider.id == solid.id);
    std::string error;
    CHECK(!w.teleportCharacter(id, {8, 0, 0}, &error) && !error.empty());
    CHECK(!w.teleportCharacter(id, {std::numeric_limits<float>::infinity(), 0, 0}, &error));
    CharacterState output;
    CHECK(!w.stepCharacter(id, {{std::numeric_limits<float>::quiet_NaN(), 0}, false}, output, &error));
    sameState(before, state(w, id));
    expect(sample(w, id), {first}, {{Kind::Stay, first}});
    CHECK(w.teleportCharacter(id, {4, 0, 0}));
    expect(sample(w, id), {second}, {{Kind::Exit, first}, {Kind::Enter, second}});
    CHECK(w.teleportCharacter(id, {0, 0, 0}));
    CHECK(w.teleportCharacter(id, {4, 0, 0}));
    expect(sample(w, id), {second}, {{Kind::Stay, second}}); // Net endpoint, not queued teleport events.
    CHECK(w.teleportCharacter(id, {12, 0, 0}));
    expect(sample(w, id), {}, {{Kind::Exit, second}});
    // A failed envelope tick preserves the established sensor membership too.
    const auto edgeSensor = box(w, {10000, 0, 0});
    const auto edge = character(w, {10000, 0, 0});
    expect(sample(w, edge), {edgeSensor}, {{Kind::Enter, edgeSensor}});
    CHECK(!w.stepCharacter(edge, {{1, 0}, false}, output, &error));
    expect(sample(w, edge), {edgeSensor}, {{Kind::Stay, edgeSensor}});
}

void lifecycleRemovalAndDenseMembership() {
    CollisionWorldOptions options; options.maxColliders = 16; options.maxCharacters = 2;
    CollisionWorld w(options);
    const auto first = character(w), independent = character(w);
    std::vector<ColliderInfo> old;
    std::vector<TriggerEvent> expected;
    for (std::uint32_t i = 0; i < options.maxColliders; ++i) {
        old.push_back(box(w, {}, {1, 2, 1}, i, true, 100 + i));
        expected.push_back({Kind::Enter, old.back()});
    }
    const auto ownedSnapshot = sample(w, first);
    expect(ownedSnapshot, old, expected);
    expect(sample(w, independent), old, expected); // Histories are per character.
    for (const auto& sensor : old) CHECK(w.remove(sensor.id));
    std::vector<ColliderInfo> replacement;
    expected.clear();
    for (const auto& sensor : old) expected.push_back({Kind::Exit, sensor});
    for (std::uint32_t i = 0; i < options.maxColliders; ++i) {
        replacement.push_back(box(w, {}, {1, 2, 1}, 31 - i, true, 900 + i));
        CHECK(replacement.back().id != old[i].id);
        expected.push_back({Kind::Enter, replacement.back()});
    }
    expect(sample(w, first), replacement, expected); // 32 events, despite a 16-collider capacity.
    expect(sample(w, independent), replacement, expected); // Sampling the first did not advance another history.
    expected.clear();
    for (const auto& sensor : replacement) expected.push_back({Kind::Stay, sensor});
    expect(sample(w, first), replacement, expected); // Removed snapshots do not accumulate after reconciliation.
    expected.clear();
    for (const auto& sensor : old) expected.push_back({Kind::Enter, sensor});
    expect(ownedSnapshot, old, expected); // Output survives sensor destruction and later samples.
    CHECK(w.removeCharacter(first));
    const auto reused = character(w); CHECK(reused != first);
    expected.clear();
    for (const auto& sensor : replacement) expected.push_back({Kind::Enter, sensor});
    expect(sample(w, reused), replacement, expected); // No inherited membership after slot reuse.
    w.clear(); CHECK(w.characterCount() == 0 && w.colliderCount() == 0);
    const auto afterClear = character(w);
    expect(sample(w, afterClear), {}, {});
    const auto unsampledSensor = box(w);
    const auto unsampled = character(w);
    CHECK(w.remove(unsampledSensor.id));
    expect(sample(w, unsampled), {}, {}); // Creation inside a sensor does not queue an Enter or Exit.
    for (int i = 0; i < 30; ++i) {
        CollisionWorld lifetime;
        const auto sensor = box(lifetime);
        const auto id = character(lifetime);
        expect(sample(lifetime, id), {sensor}, {{Kind::Enter, sensor}});
        CHECK(lifetime.remove(sensor.id));
        expect(sample(lifetime, id), {}, {{Kind::Exit, sensor}});
        expect(sample(lifetime, id), {}, {});
    }
}

void invalidIdsAndOwnerThreads() {
    static_assert(std::is_nothrow_move_assignable_v<CharacterTriggerSample>);
    CollisionWorld w, foreign;
    const auto sensor = box(w);
    const auto id = character(w), foreignId = character(foreign);
    expect(sample(w, id), {sensor}, {{Kind::Enter, sensor}});
    const CharacterTriggerSample sentinel{{sensor}, {{Kind::Exit, sensor}}};
    auto output = sentinel;
    std::string error;
    for (const auto unknown : {CharacterId{}, foreignId}) {
        CHECK(!w.sampleCharacterTriggers(unknown, output, {}, &error) && !error.empty());
        sameSample(output, sentinel);
    }
    expect(sample(w, id), {sensor}, {{Kind::Stay, sensor}});
    int rejected = 0;
    std::exception_ptr threadError;
    std::thread worker([&] {
        try {
            try { w.sampleCharacterTriggers(id, output, {0, {}}, &error); }
            catch (const std::logic_error&) { ++rejected; }
            CollisionWorld own;
            const auto ownSensor = box(own);
            const auto ownCharacter = character(own);
            expect(sample(own, ownCharacter), {ownSensor}, {{Kind::Enter, ownSensor}});
            expect(sample(own, ownCharacter), {ownSensor}, {{Kind::Stay, ownSensor}});
        } catch (...) { threadError = std::current_exception(); }
    });
    worker.join(); if (threadError) std::rethrow_exception(threadError);
    CHECK(rejected == 1); sameSample(output, sentinel);
    expect(sample(w, id), {sensor}, {{Kind::Stay, sensor}});
    CHECK(w.removeCharacter(id));
    CHECK(!w.sampleCharacterTriggers(id, output, {}, &error)); sameSample(output, sentinel);
    const auto next = character(w);
    expect(sample(w, next), {sensor}, {{Kind::Enter, sensor}});
    w.clear();
    CHECK(!w.sampleCharacterTriggers(next, output, {}, &error)); sameSample(output, sentinel);
    expect(sample(foreign, foreignId), {}, {});
}

void discreteCrossingAndRepeatability() {
    CollisionWorld thin;
    const auto sensor = box(thin, {0, 0, 0}, {0.0001f, 2, 2});
    auto fast = floating(); fast.walkSpeed = fast.runSpeed = 100; fast.fixedDeltaSeconds = 1.0f / 15;
    const auto id = character(thin, {-2, 0, 0}, fast);
    expect(sample(thin, id), {}, {});
    ticks(thin, id, 1);
    CHECK(state(thin, id).center.x > 4);
    expect(sample(thin, id), {}, {}); // Crossed the whole sensor between endpoints: deliberately no swept event.
    CHECK(thin.teleportCharacter(id, {0, 0, 0}));
    expect(sample(thin, id), {sensor}, {{Kind::Enter, sensor}});

    // Independently authored expected IDs are mapped per world, not copied from a first run.
    for (int repeat = 0; repeat < 5; ++repeat) {
        CollisionWorld w;
        const auto a = box(w, {0, 0, 0}, {1, 2, 1}, 0, true, 101);
        const auto b = box(w, {3, 0, 0}, {1, 2, 1}, 31, true, 102);
        const auto player = character(w);
        expect(sample(w, player), {a}, {{Kind::Enter, a}});
        ticks(w, player, 30);
        expect(sample(w, player), {a}, {{Kind::Stay, a}});
        ticks(w, player, 60);
        expect(sample(w, player), {b}, {{Kind::Exit, a}, {Kind::Enter, b}});
        expect(sample(w, player, {1, {}}), {}, {{Kind::Exit, b}});
        CHECK(w.teleportCharacter(player, {0, 0, 0}));
        expect(sample(w, player), {a}, {{Kind::Enter, a}});
        CHECK(w.remove(a.id));
        expect(sample(w, player), {}, {{Kind::Exit, a}});
        expect(sample(w, player), {}, {});
    }
}
} // namespace

int main() {
    const std::array<std::pair<const char*, std::function<void()>>, 7> tests{{
        {"membership ordering", membershipAndOrdering}, {"precise shapes surfaces", preciseShapesAndSurfaces},
        {"filters snapshot ownership", filtersAndSnapshotOwnership}, {"teleport failures sampling purity", teleportFailuresAndSamplingPurity},
        {"lifecycle dense membership", lifecycleRemovalAndDenseMembership}, {"invalid IDs owner threads", invalidIdsAndOwnerThreads},
        {"discrete crossing repeatability", discreteCrossingAndRepeatability}
    }};
    int failures = 0;
    for (const auto& [name, test] : tests) {
        try { test(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& e) { ++failures; std::cerr << "FAIL " << name << ": " << e.what() << '\n'; }
    }
    return failures == 0 ? 0 : 1;
}
