#include "SceneLifecycleGuard.h"
#include "SceneFixtureLifecycle.h"

#include <array>
#include <functional>
#include <limits>
#include <string>
#include <vector>

namespace {
using Json = nlohmann::json;
Json counts(std::uint64_t scenes, std::uint64_t probes) {
    return {{"geometry_buffers", 2u * (scenes + probes)}, {"textures", 5u * (scenes + probes)},
        {"material_descriptors", scenes + probes}, {"texture_descriptors", 5u * (scenes + probes)},
        {"payload_bytes", scenes * 100u + probes * 40u},
        {"api_allocation_bytes", scenes * 128u + probes * 64u}, {"api_allocation_estimated", false}};
}
Json evidence() {
    Json result{{"schema", "phlosion-scene-lifecycle-v1"}, {"cycles", 12}, {"warmup_frames", 36},
        {"cancelled_candidates", 12}, {"retained_draws", 12}, {"pending_observed", true},
        {"frame_samples", Json::array()}, {"releases", Json::array()}, {"loads", Json::array()},
        {"final_live_scopes", 0}, {"final_retained", counts(0, 0)}, {"final_decoded_bytes", 0}};
    for (int frame = 1; frame <= 42; ++frame) {
        const auto cycle = static_cast<std::uint64_t>(std::min((frame - 1) / 3, 12));
        const bool collected = frame >= 40;
        Json row{{"frame", frame}, {"live_scopes", collected ? 0 : 2},
            {"pending_scopes", frame == 39 ? 2 : 0}, {"allocation_failures", 0},
            {"allocated", counts(cycle + 1u, 1)},
            {"reclaimed", counts(collected ? 13u : cycle, collected ? 1u : 0u)},
            {"retained", counts(collected ? 0u : 1u, collected ? 0u : 1u)},
            {"candidate_state", frame <= 36 && frame % 3 == 2 ? 1 : 0},
            {"session_decoded_bytes", (frame <= 36 && frame % 3 == 0) || frame >= 39 ? 0 : 100}};
        if (frame <= 38) {
            row["asset_scope"] = cycle + 2u;
            row["probe_scope"] = 1;
            row["asset_resources"] = counts(1, 0);
            row["probe_resources"] = counts(0, 1);
            row["asset_identity"] = "identity-" + std::to_string(cycle);
        }
        result["frame_samples"].push_back(std::move(row));
    }
    for (std::uint64_t cycle = 0; cycle < 13u; ++cycle) {
        result["loads"].push_back({{"id", "source-" + std::to_string(cycle)},
            {"frame", cycle * 3u + 1u}, {"identity", "identity-" + std::to_string(cycle)},
            {"encoded_bytes", 1000}, {"decoded_bytes", 100}, {"decode_seconds", 0.01}});
        result["releases"].push_back({{"scope", cycle + 2u}, {"requested_frame", cycle * 3u + 3u},
                                     {"collected_frame", cycle * 3u + 4u}});
    }
    result["releases"].push_back({{"scope", 1}, {"requested_frame", 39}, {"collected_frame", 40}});
    return result;
}
}

bool test_scene_lifecycle_contract(std::string& outFail) {
    const auto pristine = evidence();
    if (!engine::scene_fixture::verification::validateSceneLifecycle(pristine, true, outFail)) return false;
    const std::vector<std::function<void(Json&)>> defects{
        [](Json& j) { j.erase("frame_samples"); },
        [](Json& j) { j["frame_samples"][2]["frame"] = 2; },
        [](Json& j) { j["frame_samples"][3]["asset_scope"] = 2; },
        [](Json& j) { j["frame_samples"][4]["probe_scope"] = 77; },
        [](Json& j) { j["frame_samples"][2]["session_decoded_bytes"] = 100; },
        [](Json& j) { j["frame_samples"][1]["candidate_state"] = 0; },
        [](Json& j) { j["frame_samples"][4]["retained"]["payload_bytes"] = 240; },
        [](Json& j) { j["frame_samples"][4]["allocated"]["payload_bytes"] = -1; },
        [](Json& j) { j["frame_samples"][4]["allocated"]["payload_bytes"] = 340.0; },
        [](Json& j) { j["frame_samples"][4]["asset_resources"]["payload_bytes"] = std::numeric_limits<std::uint64_t>::max(); },
        [](Json& j) { j["releases"][0]["collected_frame"] = 5; },
        [](Json& j) { j["releases"].erase(j["releases"].begin()); },
        [](Json& j) { j["loads"][1]["identity"] = "identity-0"; },
        [](Json& j) { j["loads"][1]["decode_seconds"] = -1; },
        [](Json& j) { j["loads"][1]["identity"] = "different-scene"; },
        [](Json& j) { j["final_live_scopes"] = 1; },
        [](Json& j) { j["final_decoded_bytes"] = 100; },
        [](Json& j) { j["cancelled_candidates"] = 0; },
        [](Json& j) { j["frame_samples"][30]["allocation_failures"] = 1; },
        [](Json& j) { j["final_retained"]["payload_bytes"] = 100; },
    };
    for (std::size_t n = 0; n < defects.size(); ++n) {
        auto broken = pristine;
        defects[n](broken);
        std::string error;
        if (engine::scene_fixture::verification::validateSceneLifecycle(broken, true, error) || error.empty()) {
            outFail = "lifecycle guard accepted defect " + std::to_string(n);
            return false;
        }
    }
    outFail.clear();
    return true;
}
