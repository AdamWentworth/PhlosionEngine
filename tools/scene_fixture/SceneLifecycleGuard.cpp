#include "SceneLifecycleGuard.h"
#include "SceneFixtureLifecycle.h"

#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace engine::scene_fixture::verification {
namespace {
using Json = nlohmann::json;
constexpr std::array<const char*, 6> fields{"geometry_buffers", "textures",
    "material_descriptors", "texture_descriptors", "payload_bytes", "api_allocation_bytes"};
void require(bool valid, const std::string& error) { if (!valid) throw std::runtime_error(error); }
std::uint64_t number(const Json& object, const char* name) {
    const auto& value = object.at(name);
    require(value.is_number_integer(), std::string("non-integer lifecycle field: ") + name);
    if (value.is_number_unsigned()) return value.get<std::uint64_t>();
    const auto signedValue = value.get<std::int64_t>();
    require(signedValue >= 0, std::string("negative lifecycle field: ") + name);
    return static_cast<std::uint64_t>(signedValue);
}
void validateCounts(const Json& value) {
    for (const auto* field : fields) (void)number(value, field);
    require(value.at("api_allocation_estimated").is_boolean(), "missing GPU allocation accounting kind");
}
}

bool validateSceneLifecycle(const nlohmann::json& evidence, bool unloaded, std::string& error) {
    try {
        require(evidence.at("schema") == "phlosion-scene-lifecycle-v1", "wrong lifecycle schema");
        require(number(evidence, "cycles") == kLifecycleCycles &&
                number(evidence, "warmup_frames") == kLifecycleWarmupFrames &&
                number(evidence, "cancelled_candidates") == kLifecycleCycles &&
                number(evidence, "retained_draws") == kLifecycleCycles &&
                evidence.at("pending_observed") == true, "incomplete asynchronous lifecycle cycles");
        const auto& samples = evidence.at("frame_samples");
        require(samples.is_array() && samples.size() == kLifecycleWarmupFrames + 6u,
                "lifecycle samples do not reach the actual capture frame");
        std::set<std::uint64_t> scopes;
        std::uint64_t probeScope = 0u;
        for (std::size_t n = 0; n < samples.size(); ++n) {
            const auto& row = samples[n];
            require(number(row, "frame") == n + 1u, "lifecycle frames are missing, duplicated or reordered");
            require(number(row, "allocation_failures") == 0u, "native managed allocation failed");
            for (const auto* key : {"retained", "allocated", "reclaimed"}) validateCounts(row.at(key));
            for (const auto* field : fields) {
                const auto allocated = number(row.at("allocated"), field);
                const auto reclaimed = number(row.at("reclaimed"), field);
                require(allocated >= reclaimed && allocated - reclaimed == number(row.at("retained"), field),
                        "native allocation/reclamation balance differs");
            }
            if (n < kLifecycleWarmupFrames) {
                require(number(row, "live_scopes") == 2u && number(row, "pending_scopes") == 0u,
                        "cycle retained a released scene scope");
                const auto scene = number(row, "asset_scope");
                const auto probe = number(row, "probe_scope");
                require(scene != 0u && scene != probe, "scene and probe do not have independent scopes");
                if (n == 0u) probeScope = probe;
                require(probe == probeScope, "unrelated probe scope was replaced");
                if (n % 3u == 0u) require(scopes.insert(scene).second, "GPU scope ID reused across cycles");
                else require(scene == number(samples[n - 1u], "asset_scope"), "cycle scope changed before release");
                for (const auto* key : {"asset_resources", "probe_resources"}) {
                    validateCounts(row.at(key));
                    require(number(row.at(key), "geometry_buffers") > 0u && number(row.at(key), "textures") > 0u &&
                            number(row.at(key), "payload_bytes") > 0u, "scope did not own native resources");
                }
                for (const auto* field : fields) {
                    const auto sceneCount = number(row.at("asset_resources"), field);
                    const auto probeCount = number(row.at("probe_resources"), field);
                    require(sceneCount <= std::numeric_limits<std::uint64_t>::max() - probeCount &&
                            sceneCount + probeCount == number(row.at("retained"), field),
                            "native scope totals include unowned or unreleased resources");
                    // Same authored resource sizes each cycle, despite distinct
                    // content/generations and alternating material variants.
                    require(number(row.at("retained"), field) == number(samples[0].at("retained"), field),
                            "native resource residency grows across equal-size cycles");
                }
                if (n % 3u == 1u) require(number(row, "candidate_state") == 1u,
                    "pending replacement was not observed while the active scene rendered");
                if (n % 3u == 2u) require(number(row, "session_decoded_bytes") == 0u,
                    "retained scene was not drawn after session unload");
            }
        }
        const auto& releases = evidence.at("releases");
        require(releases.is_array() && releases.size() >= kLifecycleCycles, "missing native release observations");
        std::set<std::uint64_t> released;
        for (const auto& row : releases) {
            const auto requested = number(row, "requested_frame");
            require(requested < kLifecycleWarmupFrames + 6u && number(row, "collected_frame") == requested + 1u &&
                    released.insert(number(row, "scope")).second,
                    "GPU scope release was late, duplicated or unobserved");
        }
        for (auto id : scopes) require(released.contains(id), "cycle scope was never reclaimed");
        const auto& loads = evidence.at("loads");
        require(loads.is_array() && loads.size() == kLifecycleCycles + 1u, "missing measured archive loads");
        std::set<std::string> identities;
        for (std::size_t n = 0; n < loads.size(); ++n) {
            const auto& row = loads[n];
            require(number(row, "frame") == n * 3u + 1u && number(row, "encoded_bytes") > 0u &&
                    number(row, "decoded_bytes") > 0u, "invalid measured archive load");
            require(row.at("decode_seconds").is_number() &&
                    std::isfinite(row.at("decode_seconds").get<double>()) &&
                    row.at("decode_seconds").get<double>() >= 0.0, "invalid load duration");
            require(identities.insert(row.at("identity").get<std::string>()).second,
                    "lifecycle did not exercise distinct source generations");
            require(row.at("identity") == samples[n * 3u].at("asset_identity"),
                    "measured source does not match the scene that rendered");
        }
        validateCounts(evidence.at("final_retained"));
        require(number(evidence, "final_live_scopes") == (unloaded ? 0u : 2u), "wrong final scope ownership");
        require(evidence.at("final_retained") == samples.back().at("retained"), "capture resource totals are stale");
        if (unloaded) {
            for (const auto* field : fields) require(number(evidence.at("final_retained"), field) == 0u,
                "GPU resources survived unload while host remained alive");
            require(number(evidence, "final_decoded_bytes") == 0u, "CPU scene survived session unload");
        } else require(number(evidence, "final_decoded_bytes") > 0u, "loaded scene has no decoded accounting");
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = std::string("lifecycle evidence rejected: ") + exception.what();
        return false;
    }
}
} // namespace engine::scene_fixture::verification
