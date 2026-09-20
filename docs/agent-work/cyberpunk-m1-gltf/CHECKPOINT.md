# M1-F checkpoint

Status: accepted by Astra; authorized for commit.
Date: 2026-09-20.
Baseline: clean engine main `9d094c91b9b801459069dedfd8eaaac213c821b2`.

User authorized tidying, committing M1-E and proceeding after clarifying that
meshes/animation/materials already exist. M1-E committed as 9d094c9 (22 files).
Guides distinguish typed storage from existing runtime functionality and keep
the full custom-format family optional. Nothing pushed.

M1-F verifies existing glTF scene/skeleton/animation import and material
metadata using current CPU functions and tiny public fixtures. The accepted
implementation follows [PLAN.md](PLAN.md), with coverage and artifacts in
[REPORT.md](REPORT.md) and the public
[guide](../../GLTF_IMPORT_VERIFICATION.md). No production or graphics behavior
changed, and no private assets or other repositories were used.

Root remains Astra. Static doctor is ready with unchanged configuration hash
237343dd9547ecc87450fada2a20e801718b62afe11628a371d707320f7d1a26; native
astra_flash_builder pins DeepSeek V4.1 Flash/high. Reuse actual host/router route
verification recorded in the M1-D checkpoint. The optional unauthenticated
loopback /models check returned HTTPError this turn; it is not an inference
test, and no settings or credentials were changed. Static routing and prior
real routed tasks remain the dispatch evidence; do not claim a fresh catalog
or inference verification from this doctor run.

One native worker, /root/cyberpunk_m1_gltf, implemented the tests, test
registration and guide. Astra reviewed tracked changes and new files against
the captured baseline, required corrections and integrated the documentation
index, verification record and roadmap. Worker handoffs were interrupted after
completion for host bookkeeping. No worker staged, committed or pushed.

Review covered specification compliance and code/lifecycle quality. The first
correction replaced shared temporary paths, made the skinned fixture coherent
(mesh/skin assignment, joint/weight attributes, parentless scene roots), added
the existing parser's structural validation and clarified evidence limits.
The second correction addressed a material filesystem concern: the new
ownership regression's sentinel setup could still overwrite shared paths.
Its sentinel namespace is now acquired atomically and cleaned up only by its
owner, with no dependence on an unrelated global escape.txt. Astra reviewed
the corrected setup, cleanup paths and verification artifacts before acceptance.

Final verification:

- VS2026 Debug full build, editor/tests enabled, warnings as errors: passed,
  artifacts/m1-f/build-full-08.log.
- Six of six CTest entries: passed, artifacts/m1-f/ctest-all-04.log.
- All 21 C++ contracts from the CTest build directory: passed,
  artifacts/m1-f/tests-from-ctest-dir-04.log.
- Helper-only two-process ownership/cleanup probe: passed,
  artifacts/m1-f/helper-probe-concurrent-02.log. No full-suite concurrency claim.
- Deliberately wrong child order, emissive multiplication, inverse-bind layout,
  joint range and fixed-name helper regressions failed before being reverted;
  artifacts/m1-f/regression-probe-0{1..5}.log.
- Astra confirmed no changes under src, assets, examples or tools relative to
  9d094c9. Final documentation links and tracked/new-file whitespace checked.

Acceptance is limited to CPU import/material metadata and test support. These
contracts do not sample poses, evaluate skinning, decode textures, draw pixels
or certify three-API parity. CUBICSPLINE tangent loss and missing morph-weight
channels are recorded limitations, not desired behavior frozen by a test.
Existing M1-D/E native evidence retains its original scope.

Next bounded direction: verify existing pose sampling and skeletal deformation,
including representative native runtime/editor coverage on all three APIs,
then address proven gaps. Additional custom formats are optional. The user has
now requested committing M1-F and proceeding; the next checkpoint will record
the resulting revision. The previous pass is 9d094c9. Nothing pushed.
