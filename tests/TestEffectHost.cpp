#include "engine/vfx/runtime/EffectHost.h"

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

using namespace vfx::runtime;

namespace {

#define CHECK(c) do { if (!(c)) throw std::runtime_error(std::string(__func__) + ":" + std::to_string(__LINE__) + " " #c); } while (false)

struct State {
    int updates = 0;
    int stops = 0;
    glm::mat4 lastWorld{1.0f};
};

class Probe final : public HostedEffect {
public:
    explicit Probe(State& state) : state_(state) {}
    void update(float, const glm::mat4& world) noexcept override {
        ++state_.updates;
        state_.lastWorld = world;
    }
    void stop() noexcept override { ++state_.stops; }
private:
    State& state_;
};

glm::mat4 translated(float x) {
    return glm::translate(glm::mat4(1.0f), glm::vec3(x, 0, 0));
}

void spawnAttachDetachAndExpire() {
    EffectHost host;
    State state;
    EffectId id = 0;
    CHECK(host.spawn(std::make_unique<Probe>(state), translated(2), 0.3f, id));
    CHECK(id != 0);
    CHECK(host.update(0.1f, {}));
    CHECK(state.updates == 1 && state.lastWorld[3][0] == 2.0f);
    CHECK(host.attach(id, 42, translated(3)));
    const std::array anchors{EffectAnchor{42, translated(10)}};
    CHECK(host.update(0.1f, anchors));
    CHECK(state.updates == 2 && state.lastWorld[3][0] == 13.0f);
    CHECK(host.detach(id));
    CHECK(host.update(0.05f, {}));
    CHECK(state.lastWorld[3][0] == 13.0f);
    CHECK(host.update(0.06f, {}));
    CHECK(state.stops == 1 && host.activeCount() == 0);
    CHECK(!host.stop(id));
}

void orphanCancelAndReset() {
    EffectHost host;
    State orphan, canceled, reset;
    EffectId first = 0, second = 0, third = 0;
    CHECK(host.spawn(std::make_unique<Probe>(orphan), {}, 1.0f, first));
    CHECK(host.attach(first, 19, {}));
    CHECK(host.update(0.0f, {}));
    CHECK(orphan.stops == 1 && host.activeCount() == 0);
    CHECK(host.spawn(std::make_unique<Probe>(canceled), {}, 1.0f, second));
    CHECK(host.stop(second));
    CHECK(canceled.stops == 1);
    CHECK(host.spawn(std::make_unique<Probe>(reset), {}, 1.0f, third));
    CHECK(first < second && second < third);
    host.stopAll();
    host.stopAll();
    CHECK(reset.stops == 1 && host.activeCount() == 0);
    CHECK(!host.stop(third));
}

void malformedFramePreservesEffects() {
    EffectHost host;
    State state;
    EffectId id = 0;
    CHECK(host.spawn(std::make_unique<Probe>(state), {}, 0.2f, id));
    const std::array duplicate{EffectAnchor{1, {}}, EffectAnchor{1, {}}};
    CHECK(!host.update(0.1f, duplicate));
    CHECK(!host.update(-0.1f, {}));
    CHECK(state.updates == 0 && state.stops == 0 && host.activeCount() == 1);
    EffectId untouched = id;
    auto invalid = glm::mat4(1.0f);
    invalid[0][0] = (std::numeric_limits<float>::quiet_NaN)();
    CHECK(!host.spawn(std::make_unique<Probe>(state), invalid, 1.0f, untouched));
    CHECK(untouched == id && host.activeCount() == 1);
    CHECK(!host.attach(id, 0, {}));
    CHECK(!host.attach(id, 4, invalid));
    CHECK(host.update(0.1f, {}));
    CHECK(state.updates == 1);
    CHECK(host.update(0.1f, {}));
    CHECK(state.stops == 1);
}

void ownerThread() {
    EffectHost host;
    bool rejected = false;
    std::thread other([&] {
        try { (void)host.activeCount(); }
        catch (const std::logic_error&) { rejected = true; }
    });
    other.join();
    CHECK(rejected);
}

} // namespace

int main() {
    try {
        spawnAttachDetachAndExpire();
        orphanCancelAndReset();
        malformedFramePreservesEffects();
        ownerThread();
        std::cout << "Effect host contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
