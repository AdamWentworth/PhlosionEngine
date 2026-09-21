#include "engine/render/WorldResourceScopeState.h"

#include <limits>
#include <string>
#include <vector>

bool test_world_resource_scope_contract(std::string& outFail) {
    using namespace engine::render;
    const auto require = [&](bool value, const char* message) {
        if (!value) outFail = message;
        return value;
    };
    WorldResourceScopeState first;
    WorldResourceScopeState second;
    const auto a = first.create(120);
    const auto b = first.create(240);
    const auto foreign = second.create(120);
    if (!require(a && b > a && foreign > b, "Scope handles must be unique across renderer instances") ||
        !require(!first.begin(0) && !first.begin(foreign), "Zero/foreign handles must be rejected") ||
        !require(first.begin(a) && !first.begin(b), "Scopes must reject nesting")) return false;
    const auto aKey = first.key("same mesh");
    WorldResourceCounts geometry;
    geometry.geometryBuffers = 2;
    geometry.payloadBytes = 100;
    geometry.apiAllocationBytes = 131072;
    if (!require(first.admit(100), "Initial allocation must fit")) return false;
    first.commit(geometry);
    if (!require(first.admit(20) && !first.admit(21), "Payload cap must include retained allocations")) return false;
    WorldResourceScopeStats query;
    if (!require(first.query(a, query) && query.status == WorldResourceScopeStatus::BudgetExceeded &&
        query.retained.payloadBytes == 100, "Budget failure must preserve actual allocation counts")) return false;
    if (!require(first.end() && !first.end() && first.begin(b), "End must balance one active scope") ||
        !require(first.key("same mesh") != aKey, "Identical cache keys must be scope independent") ||
        !require(first.release(b) && !first.release(b) && !first.admitted(), "Release must close active admission")) return false;
    first.collect(b);
    if (!require(!first.query(b, query) && !first.admitted(), "Collected scopes must disappear without making stale active draws unowned")) return false;
    first.end();
    if (!require(first.key("same mesh") == "same mesh", "Legacy identity must remain unchanged") ||
        !require(first.release(a), "Failed scopes must still be releasable")) return false;
    auto totals = first.stats();
    if (!require(totals.pendingReleaseScopes == 1 && totals.pendingRelease.geometryBuffers == 2 &&
        totals.pendingRelease.payloadBytes == 100, "Pending counters must retain native resources")) return false;
    first.fail(a, "Completion failed", WorldResourceScopeStatus::GpuWaitFailed);
    first.collect(a);
    totals = first.stats();
    if (!require(totals.liveScopes == 0 && totals.retained.payloadBytes == 0 &&
        totals.allocated.payloadBytes == 100 && totals.reclaimed.payloadBytes == 100,
        "Collection must reclaim metadata and retain allocation/reclamation history")) return false;

    const auto rebuilt = second.create(120);
    if (!require(second.begin(rebuilt) && second.admit(100), "Pipeline rebuild fixture admission")) return false;
    second.commit(geometry);
    second.end();
    second.reclaim(rebuilt, geometry);
    if (!require(second.query(rebuilt, query) && query.status == WorldResourceScopeStatus::Ready &&
        query.retained.geometryBuffers == 0 && query.retained.payloadBytes == 0,
        "Native cache invalidation must debit resources while keeping the scope live")) return false;
    if (!require(second.begin(rebuilt) && second.admit(100), "Invalidated payload capacity must be reusable")) return false;
    second.commit(geometry);
    second.end();
    second.release(rebuilt);
    second.fail(rebuilt, "Test completion failure", WorldResourceScopeStatus::GpuWaitFailed);
    totals = second.stats();
    if (!require(second.query(rebuilt, query) && query.releasePending &&
        query.status == WorldResourceScopeStatus::GpuWaitFailed &&
        totals.retained.payloadBytes == 100 && totals.pendingRelease.payloadBytes == 100 &&
        totals.allocated.payloadBytes - totals.reclaimed.payloadBytes == totals.retained.payloadBytes,
        "Failed completion must keep rebuilt resources pending and preserve allocation history")) return false;
    second.collect(rebuilt);
    totals = second.stats();
    if (!require(totals.allocated.payloadBytes == 200 && totals.reclaimed.payloadBytes == 200 &&
        totals.retained.payloadBytes == 0, "Rebuilt resources must be reclaimed exactly once")) return false;

    std::vector<WorldResourceScopeId> ids;
    for (std::uint64_t i = 0; i < WorldResourceScopeState::maxScopes; ++i) ids.push_back(first.create(0));
    if (!require(first.create(1) == 0, "Live scope count must be bounded")) return false;
    first.release(ids.front());
    if (!require(first.create(1) == 0, "Pending scopes must count towards admission limits")) return false;
    first.collect(ids.front());
    if (!require(first.create(1) != 0, "Collected metadata capacity must be reusable")) return false;
    std::uint64_t bytes = 0;
    if (!require(worldGeometryPayload(3, 112, 3, bytes) && bytes == 348 &&
        !worldGeometryPayload((std::numeric_limits<std::size_t>::max)(), 112, 3, bytes),
        "Geometry byte accounting must check arithmetic") ||
        !require(worldTexturePayload(4, 2, 3, bytes) && bytes == 44 &&
        !worldTexturePayload(0, 2, 1, bytes) && !worldTexturePayload(4, 2, 33, bytes),
        "Texture accounting must include all retained mip levels")) return false;
    return true;
}
