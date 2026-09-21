# Astra review: M1-K, first handoff

Disposition: changes requested. The overall CPU asset / generic adapter /
content-example boundary is sound. The submitted 36 native captures look
consistent, but passing the current tests does not cover the issues below.
This is one consolidated correction bundle, owned by the same Flash worker.

## 1. Validate before decoding or indexing

`buildAsset` calls `buildSceneData` before `validateRawSkins`, and no accessor
buffer-range preflight occurs before `iterateAccessor*`. A malformed inverse
bind accessor can reach `readMat4` before its type check. An out-of-range node
skin reaches `asset.skins[skinIndex]` in `importPrimitive`. Node transforms,
scene roots and raw animation channels/counts also need preflight rather than
being silently repaired/dropped by the legacy builder.

Add bounded, checked validation before those helpers: reference indices,
buffer/view/accessor ranges against actual loaded bytes, strides/component
sizes and sparse ranges (or explicitly reject sparse for this subset), raw skin
shape/count/type, node/root/skin references, hierarchy, finite node transforms,
animation input/output type/count/interpolation/channel-path consistency.
Reject zero/invalid rotations, non-finite globals and invalid skin weights;
zero-weight fallback must never activate an unchecked joint. Reject unsupported
required extensions and unsupported used features; the parser's broad legacy
extension mask is not a declaration of support by this new path. Avoid parser
index generation before geometry limits if it prevents the required preflight.
Preserve previous output on every normal rejection. Add focused malformed-file
contracts, including out-of-range skin and truncated accessor payload.

## 2. Honor the selected scene and default material

The import loops currently traverse every mesh node, including nodes outside
the selected scene. Retain index-compatible node/skin data as needed but submit
only mesh nodes reachable from the selected default/first scene. Test a second
scene and an unreferenced mesh. Reject invalid scene roots/default indices
explicitly.

A primitive without an explicit material currently loads with materialIndex -1
and then cannot draw. Resolve the legal glTF default material into an owned,
neutral material binding, or reject it at import if deliberately excluded;
prefer supporting this ordinary default. Add load-plus-draw coverage.

## 3. Enforce the promised material subset

The code accepts every standard min/mag filter but does not store or bind these
values. Restrict admission to the filtering the existing shared world path
actually supplies (document it), rather than silently accepting NEAREST etc.
Do not add a renderer change for this correction.

The documented alpha-factor-1 restriction is not enforced. Validate finite,
supported factor ranges (including normal/AO/MR/emission), enforce that alpha
restriction and check vertex color policy where the shared opaque path cannot
honor it. Add negative contracts. Clarify source digest versus generation versus
content-based cache keys: the current keys do not contain generation, despite
the guide claiming every successful reload changes them. Content-identical
reloads may safely reuse keys; changed geometry/pixels must still invalidate.

## 4. Make draw rejection match its public contract

`drawGltfSceneAsset` promises inconsistent input is rejected before any draw,
but validates and submits in the same loop. Preflight every submission first,
including palette length/binding and finite transforms/options, then submit.
Test a bad second primitive/palette produces zero recorded draws. Preserve
stats on failure. Wire or remove the currently unused textureDetailLodBias
option so the public API does not silently ignore it.

## 5. Exercise the promised transition and color-factor oracle

`a-pose` is sampled to its final pose during prepare; its frame-3 transition is
a no-op. Start it in bind pose and sample the same asset to the animated pose
at the transition. Record actual capture/transition state, and test before and
after values so the handshake cannot merely restate a case constant.

The native guard has no independently calculated base-color-factor pixel
expectation. Add the planned small imported albedo inspection surface (or an
equally independent bounded oracle) whose expected pixels derive from authored
source bytes, linear factor and documented display transfer. Keep the main
character/environment lit. Exercise through the generic scene adapter and
reset debug state. Reject no-factor/double-factor/color-transfer corruption.

## 6. Close evidence-validation holes

The matrix guard reads PNG and handshake but ignores stdout/stderr, editor
metrics, editor activation, handshake hash and requested capture frame. Enforce
the PLAN's full log/PNG/handshake/metrics agreement and canonical containment,
including one native screenshot record, native parity PASS, no fallback,
required editor plugin activation, actual requested capture frame and dimensions.
Do not rely solely on the runner having checked them previously.

Use checked JSON integer extraction before narrowing; e.g. logical_frame is
currently read as int so adding 2^32 can preserve its apparent value. Missing,
wrong-type and overflow evidence must fail cleanly, including dimensions and
binary/file sizes. Reject duplicate binary roles and impossible row metadata.

Run cross-case deformation, failed replacement, replacement material/geometry,
and emission effects independently for each API/host. Comparing only OpenGL
runtime effects then using a whole-image 5% parity tolerance lets a small
missing effect on another backend pass. The expected base-factor check must
also be per capture. Include adversarial mutations of missing evidence,
overflow and emission-off images replaced with emission-on only on Vulkan and
D3D12; pristine must pass again afterward.

Root reproduction script and outputs: ignored
`artifacts/m1-k/astra-review-guard.py` and `astra-review-guard.json`; original
capture data is untouched and original matrix revalidation runs last.

## 7. Respect independent build options and hermetic CPU tests

The scene verification target links public_asset_fixture_verification, which
is not created when tests/public fixture are OFF and scene fixture is ON.
Make support-library creation reflect its consumers without enabling unrelated
hosts. Verify a scene-only build with tests/public/animation/material options
OFF, plus the normal full build. Preserve consumer default-OFF behavior.

The new direct CPU asset/adapter tests do not scope the decoder diagnostic
environment. Use existing scoped test-environment helpers (or a narrow shared
test helper); ambient debug/dump flags must be restored and produce no debug
files outside owned staging. The sample's existing scoped override can stay.

## Re-verification and boundaries

Keep the original ownership and forbidden-change rules. Root owns shared docs,
PLAN/CHECKPOINT/this review; worker updates code/tests/guides/REPORT. No staging,
commit or recursive delegation. Do not change shaders, backend ABI, Model or
cache schema. Reuse existing verification helpers where practical.

Run focused new CPU/adversarial cases, the full Debug warnings-as-errors build
and CTest, then the 36 native scene cases. Refresh the 78 regression captures
only where final binary identity changed (otherwise prove exact SHA/size reuse).
Report all final run IDs, paths and actual outcomes; keep raw logs ignored.
This correction has a 120-minute execution budget, with one milestone at a
natural implementation/test boundary and one final ready_for_review handoff.
