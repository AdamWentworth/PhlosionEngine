#pragma once

#include <memory>
#include <string>
#include <nlohmann/json.hpp>

class IRenderBackend;
namespace engine::scene_fixture {
struct FixtureSession;
struct FixtureLifecycleState;
inline constexpr int kLifecycleCycles = 12;
inline constexpr int kLifecycleWarmupFrames = kLifecycleCycles * 3;
bool prepareFixtureLifecycle(FixtureSession&, const std::string& directory, std::string*);
bool advanceFixtureLifecycle(FixtureSession&, int frame, std::string*);
bool beginFixtureLifecycleDraw(IRenderBackend&, FixtureSession&, std::string*);
bool finishFixtureLifecycleDraw(IRenderBackend&, FixtureSession&, std::string*);
nlohmann::json fixtureLifecycleEvidence(const FixtureSession&, const IRenderBackend&);
}
