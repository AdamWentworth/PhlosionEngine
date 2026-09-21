# M1-K: integrated glTF scene path

Status: accepted by Astra on 2026-09-20; included in the user-authorized commit
`Integrate owned glTF scenes with shared rendering`. Nothing pushed.

## Delivered behavior

The engine now has an owned CPU glTF scene snapshot, pose sampling and a generic
adapter to its shared world renderer. `Model` is not constructed. The bounded
subset supports selected-scene geometry, static and weighted animated nodes,
five material map roles, separate linear base factors and factor-only emission.
Publication and pose sampling preserve previous output on rejection; retained
snapshots survive source removal and replacement. Default materials are usable.

The public example supplies a small character/environment and lifecycle cases;
reusable loading, palette composition and draw binding live under
`src/engine/render/gltf`. Import remains synchronous. The example is not a game
adapter or a private-content qualification.

## Review and routing

Astra planned and reviewed; native `astra_flash_builder` implemented the first
bundle and began the consolidated correction. Provider HTTP 402 Insufficient
Balance interrupted that correction. The user explicitly authorized Astra to
finish it, so the remaining implementation and verification ran in the root.
The stopped child was interrupted; no alternate provider was silently used.

[The first review](ASTRA_REVIEW.md) found unsafe validation ordering, selected
scene/default-material errors, ignored sampler options, partial draw rejection,
missing transition/color-factor proof and weak evidence validation. Independent
probes showed the first guard accepted missing logs/metrics/frame evidence,
an overflowing frame number and missing emission on two APIs. Those earlier
passing matrices are historical, not accepted evidence for the final sources.

Corrections include accessor/buffer/reference checks before helper decoding,
raw animation/skin/hierarchy validation, finite/range checks, selected-scene
traversal, a default material, explicit filter restrictions, complete draw
preflight, a real same-asset bind-to-pose transition and capture-frame handshakes.
An imported albedo panel now checks each pixel in two interior 5x5 patches
against authored source bytes and linear factor math (channel tolerance 3).
Cross-case effects are checked separately for all six API/host combinations.
The guard validates logs, PNG and handshake hashes, editor metrics/activation,
frames, dimensions, canonical capture paths and bounded numeric evidence.

## Final evidence

Final evidence is based on `cc600e1` plus the accepted working tree, including
the final oversized material-reference rejection. All paths below are relative
to the ignored `artifacts/m1-k/` directory.

| Check | Accepted evidence |
| --- | --- |
| Full Debug build, warnings as errors | `astra-build-final.log`, exit 0 |
| 29 CPU contracts and all nine CTest entries | `astra-ctest-accepted.log`, 9/9, 284.37 seconds |
| Scene guard/runtime with tests, editor and other fixture options off | `astra-scene-only-final.log`, exit 0 |
| Integrated scene, six cases x three APIs x two hosts | `matrix/m1-k-accepted-scene/`, 36 captures |
| Archive regression | `regression/archive/m1-k-accepted-archive/`, 24 captures |
| Animation regression | `regression/animation/m1-k-accepted-animation/`, 36 captures |
| Material regression | `regression/material/m1-k-accepted-material/`, 18 captures |
| Binary identity, changed Markdown links and untracked whitespace audit | `astra-final-audit.json`, 16 binary SHA/size matches, no failures |

All four native matrices passed, totaling 114 captures on Windows/GTX 1070.
The new scene's 36 captures pass expected content and 90 image comparisons
under unchanged standard limits (MAE .01, RMSE .08, changed ratio .05, channel
tolerance 8). All 60 color-factor patches matched every one of their 25 pixels
at tolerance 3. Deformation changed ratio is .21365-.21387 and replacement is
.54926 across all six API/host combinations; emission-on head luminance is
about 218.28 versus 77.34-77.68 when off. The capture handshakes describe frame 6
after the frame-3 transition. Astra inspected the final contact sheet and
confirmed the character, environment, pose, replacement, emission and unload
states. CPU harness rejection coverage includes 29 evidence/content mutations
and recovery to the pristine matrix.

The source review is closed: the seven finding groups in ASTRA_REVIEW.md are
addressed, including complete reference validation before narrowing and helper
decoding, atomic adapter preflight, optional-build independence and test
environment restoration. Earlier passing runs do not replace this evidence.

## Limits and next work

- CPU import and per-pose timings are measurements, not frame-time guarantees.
  Retained geometry/pixel byte counts exclude parser/decoder temporaries,
  node/clip metadata, container capacity and filesystem reads.
- Dropping a CPU snapshot does not evict renderer GPU caches. Asynchronous
  archive-to-scene integration, bounded GPU residency/release and broader
  production-load measurements remain explicit follow-up work.
- General glTF/PBR, independent map UVs/transforms, compressed textures,
  private roster/district content and other hardware/platforms remain unqualified.
- Legacy `Model` retains its existing base-factor and factor-only-emission
  behavior. The new scene path is qualified separately; no shader/backend,
  legacy cache schema or editor plugin ABI change is part of this pass.

This advances the engine-facing M1 integration gate without declaring all M1
finished. Movement/collision/camera/input remain M2, and lighting follows that
usable scene context before full district qualification.
