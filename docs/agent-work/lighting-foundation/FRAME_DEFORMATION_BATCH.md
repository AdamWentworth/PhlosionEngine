# One deformation upload batch for the world view family

2026-10-30. Baseline engine 9ed086d, game e2a19a9, ModelSupport c7f972e.
All implementation, optimized builds, tests and processing ran on gpu-worker
Windows, using the EVGA GPU1. controller owns integration and commits.

## Change

Normal field and battle presentation gather admitted glTF, foliage, previous-pose
and native-model palettes before one owner-controlled commit. Each producer's
inputs are immediately snapshotted and coalesced in ordinary RAM; GPU reservation,
joined copies, per-copying-thread store fences and owner memory flush happen
only at commit. Consumers still reference the shared current/previous storage.

The wet field's same twelve palettes / 11,904 bytes now use one copy join instead
of three; normal battle joins fall from four to one. Its 8,320-byte rain-parameter upload remains immediate. No palette,
draw, afterimage, rain behavior or graphics budget is removed. All 61 staged
shader/material payloads are byte-identical to the baseline.

Failed admission rolls back only that producer's pending additions. Failed
reservation/flush preserves prior published entries, rolls back the backend
arena cursor and leaves the batch available for retry. A live or canceled batch
rejects deformation consumption; it cannot silently use draw-time uploads.
Scope destruction cancels unfinished work, and frame reset clears the aborted
state only at the existing fence boundary. Prewarm-only battle setup opens no
GPU batch. Standalone previews retain synchronous local preparation.

## Source contract and limits

The supplied renderSkinManager.cxx snapshot (SHA-256
2651cb46920e15e2dc39c3b163743dbcd8a4172b3f8ce9ae2206d68cbb5b7b54)
gathers active updates before its joined copy/publication step, around lines
335-389. This change closes that scheduling gap for the normal world view family.

Persistent registered ownership is still absent. The remaining manager contracts
are versioned IDs and explicit owner lifetime, retained current/previous versions,
unchanged-pose reuse, keepalive, and pressure/fence-safe retirement. The complete
producer/material graph beyond the qualified paths remains an open category gate.
A longer-lived content cache alone would not satisfy those contracts.

The shared-preparation matrix remains 90-95% estimated technique coverage.
This checkpoint is not verified 100% category parity or a retail performance
comparison. The source snapshot is not authenticated as current retail. Linux,
C7/C8 and sustained whole-frame 16.67 ms acceptance remain separate open gates.

## Verification

Fifteen selected CPU test groups plus private foliage arithmetic pass; thirteen
validation-enabled native checks pass on both APIs. New tests cover producer
mutation, cross-producer deduplication, unique-byte budgets, admission rollback,
no GPU reservation before commit, copy/flush failure and retry, draw rejection
while open/aborted, nested/reset misuse and fence reset. Native motion and glTF
fixtures exercise split admission and one commit.

Eighteen runtime/editor/preset pairs pass state and allocation contracts; all
candidate world frames with multiple prepared palettes use exactly one copy join,
with no cache failures/bypasses. Two additional pairs preserve four afterimages.
Existing fixed-frame baseline images were reused only after proving the complete
staged baseline equals the preceding qualified candidate; reused-baselines.json
records each source. All final timing runs are fresh.

- d3d12: 1/9 exact pairs; maximum 8/255; 5 above the strict 1/255 threshold.
- vulkan: 6/9 exact pairs; maximum 1/255; 0 above the strict 1/255 threshold.

Small image residuals remain explicit. The D3D12 wet editor residual peaks at
8/255, with only five pixels above 4/255; the reviewed maximum lies near the rain
and canopy lighting. This does not prove its cause or establish pixel identity.
Contact sheets, numerical differences and acceptance limits are preserved.

The first build exposed a static-extent test-span conversion error, corrected
before tests. Review also found that prewarm setup must not open a GPU batch.
The final runtime/editor targets were relinked and restaged after that correction
before qualification. Original failure and relink receipts remain in the evidence.

## Matched optimized cost

Native 1920x1080 Medium, night/overcast, RelWithDebInfo, validation disabled.
Wet AB and battle ABBA per API provide twelve primary runs, 140 steady frames
each. Four separate diagnostic runs retain phase data. GPU thermals, API errors,
paired settings and runtime states are checked. One original Vulkan candidate
run reported a software thermal flag. Its matched C/B pair was rerun; the
accepted set contains twelve selected primary runs from fourteen executed. The
original flag, excluded pair and replacement receipts remain available.

| API / scene | GPU mean ms | CPU frame mean ms | Active CPU mean ms |
| --- | ---: | ---: | ---: |
| d3d12 / wet | 16.461 -> 16.372 | 16.449 -> 16.602 | 8.632 -> 9.167 |
| d3d12 / battle | 17.047 -> 17.079 | 19.509 -> 19.292 | 17.236 -> 16.805 |
| vulkan / wet | 18.314 -> 18.397 | 18.159 -> 18.230 | 4.716 -> 4.539 |
| vulkan / battle | 18.029 -> 18.004 | 18.022 -> 17.916 | 10.271 -> 8.993 |

Whole-frame and active-CPU results govern the cost assessment; moving work between
phases is not a saving by itself. One wet AB pair is not a stable speedup claim.
GPU times are effectively flat, while CPU results are mixed across the whole-
frame, active-time and diagnostic samples. This pass establishes the shared
commit contract; it does not establish a broad speedup or close the frame target.
These are comparisons between Phlosion builds on the worker, not Cyberpunk retail.

Evidence: artifacts/lighting-foundation/frame-deformation-batch-20261030.
The game roadmap records strict dependency and final payload identity checks.
