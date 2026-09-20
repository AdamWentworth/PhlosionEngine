# M1-B: dependency-aware asset loading

Date: 2026-09-20
Status: M1-B accepted by Astra; see CHECKPOINT.md for evidence and limits
Coordinator: Astra; implementation: native `astra_flash_builder`.

## Objective and baseline

Extend the accepted M1-A byte reader with a reusable CPU asset-graph session.
Load a root asset and its required transitive dependencies, verify PHRC integrity
and dependency hashes, and publish a complete result with explicit ownership.
Demonstrate a dependency-only prefab referencing a scene plus a raw asset.
This advances M1; production scene integration and rendered qualification remain
separate work. No Pokemon semantics, private assets, renderer changes or new deps.

Repository: `D:/Projects/Phlosion/PhlosionEngine`, clean main at
`e2ec9266e21672c8b2bfe6655ce0a26a8e8f3d92` before this planning document.
The roadmap and M1-A implementation have been committed at the user's request.
No commit or push is part of this worker assignment.

Existing contracts:
- `src/engine/assets/AsyncAssetLoader.h/.cpp`: background byte reader, explicit
  request lifetime, cancellation/release suppress late publication, reusable
  admission until requestStop. See `docs/ASYNC_ASSET_LOADING.md`.
- `src/engine/core/IAssetStore.h`: synchronous whole-vector reads, no size query.
- `src/engine/assets/phlosion/PhlosionResourceContainer.h/.cpp`: PHRC dependency
  IDs, required flag, expected hash; decode checks container/chunk integrity but
  does not resolve dependencies or validate profile-specific schemas.
- `PhlosionSceneArchive.h/.cpp`: real scene/prefab encoders and archive stores;
  prefab can have dependencies without embedded files.
- `docs/ENGINE_BOUNDARIES.md`, `docs/VERIFICATION.md`, and the accepted M1-A
  PLAN/REPORT supply ownership and current test conventions. Read progressively.

## Ownership

You are not alone in the workspace. Preserve other edits and accommodate them.
Astra owns this PLAN, CHECKPOINT, roadmap and docs index. Flash owns only:

- new `src/engine/assets/phlosion/PhlosionAssetGraphLoader.h/.cpp`;
- new `tests/TestPhlosionAssetGraphLoader.cpp`;
- `CMakeLists.txt` and `tests/TestMain.cpp` for source/test registration;
- new `docs/ASSET_DEPENDENCY_LOADING.md`;
- `docs/VERIFICATION.md` for the new contract scope/count;
- `docs/agent-work/cyberpunk-m1-dependencies/REPORT.md` for evidence.

Do not alter existing loader behavior, IAssetStore, archive formats, renderers,
editor/game paths, dependency manifests or prior tests. Ask Astra if an existing
defect requires expanding this scope. No commits/staging, resets, private asset
access, recursive agents, or orchestration-skill invocation by the worker.

## Public API and architecture decisions

Namespace `engine::assets::phlosion`. Use these names and contracts. Ordinary
include/PImpl details and private graph traversal are implementation discretion.

```cpp
enum class AssetKind { Phrc, Bytes };
struct AssetLocation { std::string virtualPath; AssetKind kind = AssetKind::Phrc; };
using AssetResolver = std::function<bool(const std::string& assetId,
    AssetLocation& outLocation, std::string* outError)>;
enum class AssetGraphState { Idle, Loading, Ready, Failed, Cancelled };
struct AssetGraphOptions {
    std::size_t maxAssets = 256;
    std::size_t maxDependencyEdges = 4096;
    std::size_t maxEncodedBytes = 64 * 1024 * 1024;
};
struct LoadedAsset {
    std::string virtualPath;
    AssetKind kind = AssetKind::Phrc;
    std::vector<std::uint8_t> bytes;
    std::uint64_t contentHash = 0;
};
struct SkippedOptionalDependency {
    std::string parentAssetId;
    phrc::Dependency dependency;
};
struct AssetGraphResult {
    AssetGraphState state = AssetGraphState::Idle;
    std::string rootAssetId;
    std::map<std::string, LoadedAsset> assets; // keyed by opaque asset ID
    std::vector<SkippedOptionalDependency> skippedOptionalDependencies;
    std::string error;
};
class PhlosionAssetGraphLoader {
public:
    explicit PhlosionAssetGraphLoader(std::shared_ptr<const engine::IAssetStore>,
                                     AssetResolver, AssetGraphOptions = {});
    ~PhlosionAssetGraphLoader();
    bool start(std::string rootAssetId, std::string* outError = nullptr);
    void pump();
    AssetGraphState state() const;
    bool cancel();
    bool take(AssetGraphResult& outResult);
    void reset();
    std::size_t loadedBytes() const;
};
```

The session is noncopyable/nonmovable. All graph methods and the resolver run on
one owner thread; no reentrant resolver calls into this session. It owns an
AsyncAssetLoader configured for one admitted request and the graph's byte cap.
Only provider reads run in the background. This avoids adding thread safety to
PHRC decode or caller registries. Provider lifetime/concurrency and destructor
join restrictions from M1-A apply unchanged.

1. Constructor rejects null store, empty resolver or maxAssets==0 with
   invalid_argument. Zero edges and zero bytes are valid limits.
2. `start` only accepts from Idle, rejects empty ID or any other state with a
   diagnostic and leaves an existing session intact. It initializes Loading;
   resolver and reads begin through pump, not start. IDs are opaque: no inferred
   asset-ID/path equivalence or filename-extension detection.
3. `pump` never waits for I/O. It resolves/schedules work, or consumes at most
   one completed asset read and decodes it on the caller. It may schedule the
   next read in that call. No frame-time bound is claimed for decode/traversal.
   Outside Loading it is a no-op. Resolver false/exception, empty resolved path,
   invalid AssetKind, read failure or PHRC decode failure makes the graph Failed
   with an asset-specific diagnostic. Do not claim recovery from process OOM.
4. Resolve/read each required unique asset ID once within a session, including
   the root. Shared dependencies (diamonds) succeed. Retain original encoded
   bytes in the result; do not retain copied decoded chunk payloads as well.
   No cross-session cache/deduplication or hot reload in this bundle.
5. Phrc calls the existing decoder, accepts its supported generic profile
   magics, and uses validated Document.contentHash. Bytes is an opaque leaf and
   uses phrc::contentHash64 over the entire vector. No typed-profile/schema,
   embedded material, model or JSON semantic validation is implied by Ready.
6. Traverse only dependencies with kDependencyRequired. flags==0 means optional:
   do not resolve/read that edge, and record it (parent + original dependency)
   in skippedOptionalDependencies. Even if the ID is loaded via another required
   edge, this optional edge remains recorded and imposes no hash constraint.
   Unknown flag bits reject the document with a diagnostic. Optional edges do
   not participate in required cycle detection.
7. For every required edge, nonzero expectedContentHash must equal the target's
   hash under its resolved kind. Zero means unspecified. Enforce constraints
   even when a shared asset was already loaded or a later edge adds a nonzero
   constraint. Conflicting nonzero constraints fail. Include parent/target IDs
   in hash errors. Do not compare PHRC expected hashes with whole-file hashes.
8. Required cycles, including self-cycles, fail. A bounded iterative traversal
   or an equivalent bounded algorithm is appropriate; avoid an unbounded native
   recursion stack if users raise maxAssets. Detection can happen before Ready.
9. maxAssets counts unique required assets including the root. maxDependencyEdges
   counts every encountered edge record, including duplicate/optional records.
   maxEncodedBytes caps cumulative retained encoded bytes. Check arithmetic and
   limits without overflow. Check acceptance before retaining a new payload.
   Exceeding a limit fails the entire graph and releases staged payloads.
10. The encoded-byte cap is NOT a peak-memory cap: provider read temporaries,
    AsyncAssetLoader's completed-read staging, decoder copies/allocations,
    provider caches, graph metadata, vector capacity and consumer-owned results
    are outside it. Decoder can allocate from a document before the graph edge
    limit is inspected. Document these limits rather than promising streaming.
11. Ready requires the full required closure, every applicable hash and required
    cycle check to pass. No partial asset map may escape. Failed/Cancelled have
    empty assets and skipped-optionals and loadedBytes()==0; root ID and error
    remain available. loadedBytes reports this session's accepted encoded payload
    bytes only (not the Async reader's pending completed buffer).
12. cancel succeeds only in Loading, immediately becomes Cancelled, releases the
    underlying request and staged data, and suppresses all late publication.
    It does not interrupt synchronous provider I/O. Other states return false.
13. take succeeds only for Ready/Failed/Cancelled, moves the result out and
    returns the session to Idle. Otherwise false, with output unchanged. Moving
    Ready data transfers its memory/accounting responsibility to the consumer.
14. reset from any state releases active/staged data and returns to Idle. It must
    permit later start even when an old released read is still in flight. Do not
    call the underlying terminal requestStop to implement reset/cancel. Old read
    completion must never contaminate the new session. Destructor joins the
    background reader and may wait for a blocked provider, as in M1-A.

## Verification and acceptance

Follow existing boolean contract functions/TestMain conventions. Use public
synthetic fixtures, actual PHRC encoders and existing archive decoders. Cover:

- root/raw/empty success; multi-level closure, diamond dedup (resolver/read count),
  required hash success for both kinds, zero unspecified hash, later constraints
  on shared nodes, conflicting constraints and ordinary mismatch;
- optional unresolved edges recorded without resolver/read calls, optional
  cycles ignored, unknown flags rejected; self and longer required cycles;
- missing mapping, resolver throw/invalid result, provider failure, corrupt
  PHRC; clear asset-specific diagnostics and no partial output on failure;
- constructor/start/state/take contracts, terminal admission rejection, reset,
  recovery and exact loadedBytes accounting; node/edge/byte limits, including
  duplicates/optionals counting and valid zero limits;
- gated active-read cancellation and reset/reuse, with deterministic evidence
  old completion cannot publish into a new session. Use bounded predicate waits
  and cleanup guards; keep objects alive through guard cleanup. Existing M1-A
  tests are examples, not a reason to duplicate every low-level reader test;
- real dependency-only prefab -> scene -> raw graph (PHRC generic document may
  be used to attach dependencies to a scene). Decode returned prefab/scene via
  the existing stores and assert identities and exact embedded content. This
  tests a usable consumer boundary without production renderer integration.

Run `cmake --build build-presentation --config Debug --parallel 4`, preserving
the VS2026 editor/tests/WX configuration, then
`ctest --test-dir build-presentation -C Debug --output-on-failure`.
Reconfigure only if needed. Save full noisy logs under ignored `artifacts/m1-b/`,
preserve exit codes and report relevant tails/failures. Run `git diff --check`
and new-document link checks. Do not weaken existing tests or alter thresholds.
No GPU check is required for this CPU-only work; no visual readiness claim.

Update REPORT with changes, exact test results/artifacts, risks and deferred M1
work. Astra reviews actual patch and evidence before acceptance. Rough budget:
30 minutes for discovery/implementation/test/debug; ordinary fix loops are part
of the bundle. Checkpoint if interrupted or blocked; do not omit verification.
