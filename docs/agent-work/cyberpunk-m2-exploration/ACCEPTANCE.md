# M2-F acceptance review

Status: asset-independent implementation and automated qualification accepted
2026-09-22. Basic physical controls exercised; manual editor toolbar checks
remain pending at the user's request. Baseline: `36b6986`.

## Review scope

The [approved plan](PLAN.md) integrates the accepted M2 foundations into a public
course shared by a standalone runtime and the actual editor Game preview. The
root reviews the final patch and recorded evidence after the implementation
worker finishes its build, test and native qualification loop.

Acceptance requires:

- unchanged main plugin ABI 34 and InputEvent contract; an optional validated
  SDL extension with safe load, reload and destruction lifetimes and a preserved
  legacy input route;
- shared, tested gameplay gates and fixed-tick action delivery, including zero
  and multiple simulation ticks per rendered frame, rearming, explicit device
  selection and viewport-local pointer controls;
- one authored course for rendering and collision, visible player and sensor
  responses, NPC blocked/retry behavior, bounded GPU resources and an authored
  camera/runtime-state split;
- an enabled Debug build, relevant compiler-warning evidence, all CTest entries
  and meaningful malformed-input/evidence rejection tests;
- 96 exploration captures across eight states, two hosts, three native APIs and
  two exact aspect ratios, with matching simulation state, independent expected
  content checks and exact capture/build/source provenance;
- the existing 36-case scene fixture matrix on final binaries, unchanged image
  thresholds and visual inspection of the recorded native results.

## Initial root review

The complete production/test/tool patch was reviewed after the worker's source
handoff. Main ABI 34, InputEvent, and the accepted collision, character, action,
SDL adapter and camera algorithms are unchanged. The optional extension is
validated before runtime construction, remains tied to the loaded library and
replaces legacy event delivery only for opted-in plugins. Shared input gates,
pending edges/deltas, explicit selection, authored settings, shared course
geometry and bounded resource ownership are implemented.

One consolidated correction request was implemented and re-reviewed:

1. Reject oversized unsigned JSON integers before narrowing in replay parsing;
   add boundary tests that cover signed/unsigned representations.
2. Hash and verify the actual staged project descriptor and plugin, bind the
   editor command to that project, require the relevant source catalog, and add
   negative provenance tests for those cases.

The fixes include unsigned boundary cases and valid endpoint tests, a required
33-path source catalog, staged/build DLL byte and SHA-256 equality, exact project
argument validation and four additional native provenance mutations. No further
production correction was requested. Final test/native evidence passed review.

Pre-correction native results remain development evidence. Acceptance uses the
corrected binaries and final runs below.

## Final evidence

All paths below are under ignored `artifacts/m2-f/`:

| Evidence | Result |
| --- | --- |
| `build-full-03.log` and `.exit` | All enabled Debug targets built; exit 0 |
| `ctest-all-03.log` and `.exit` | 20/20 passed in 349.04 seconds |
| `matrix-04/manifest.json` and `validation.json` | 96 native captures passed across both hosts, three APIs and both aspects |
| `supplemental-01/manifest.json` and `validation.json` | 12 steep-slope/ledge-drop captures passed across both hosts and three APIs |
| `scene-regression/final-02/` | Existing 36-case scene matrix passed on final binaries |
| `negative-01.log` | All 27 mutations rejected; pristine before/after passed |
| `root-review.json` | 936 unique referenced files match current hashes/bytes |
| `compiler/` | Affected targets use `/W4 /WX`; the existing aggregate test target retains `/W1 /WX-` |

Both exploration matrices have zero maximum normalized MAE, RMSE and changed
pixel ratio across matched viewport comparisons. Standing thresholds were not
changed. Root inspected all three contact sheets and the raw 21:9 D3D12 editor
sensor capture. Expected course/player/sensor content and Game letterboxing are
present. Actual simulation tick 120 is distinct from screenshot frame 160;
synthetic focus/background replay is recorded explicitly.

Root completed this evidence review after the implementation worker ended on a
usage-limit error. Its final launched commands had completed successfully; no
implementation edits or redundant full-suite rerun were needed on resumption.
Whitespace, clean staging and unchanged accepted-contract checks passed.

## Hardware and asset limits

Normal visible D3D12 sessions ran on 2026-09-22 with SDL identifying the device
as `Xbox 360 Controller`. The user reported that all requested runtime and
editor checks passed. Launch identities, raw traces and verbatim observations
are under `artifacts/m2-f/manual-20260922-155942/`; both hosts were closed after
the checks. Neither trace uses synthetic input or the background override.

Runtime telemetry confirms movement, run, orbit/zoom, focus loss, pause and three
resets to authored distance 7. The user reported unplug/reconnect/reselection
success; the trace contains no SDL device inventory removal/addition transition,
so physical detach events are not independently corroborated.

Editor telemetry confirms movement, orbit/zoom and focus loss with authored
distance 9. It contains no Pause/Stop/Scene-switch or reset transition. On
clarification, the user requested leaving those manual checks pending. Do not
treat the initial broad response as qualification of those missing transitions.
The automated replay gate coverage remains separately accepted. No further
manual session is scheduled by this pass.

The manual checklist is:

1. Explicit selection, analog movement, run, orbit and reset to the safe spawn.
2. Focus loss while moving and return to the game, with no stale movement or
   camera delta; held controls must rearm according to the documented policy.
3. Unplug/reconnect and explicit reselection without an automatic device switch.
4. Editor Game focus and Play/Pause/Stop transitions, with input gated while
   inactive and authored camera settings preserved by runtime resets.

Record each tested host and behavior individually. A short runtime check does
not establish editor, all-device or all-driver physical coverage.

The final character and environment are unavailable. Even an accepted public
course cannot close the actual Outskirt Stand exit gate. Follow the
[asset handoff checklist](../../M2_ASSET_HANDOFF.md) when that content arrives.

## Routing and repository state

Implementation uses the previously user-approved Astra fallback after Flash
returned HTTP 402 / Insufficient Balance. The unchanged routing configuration
hash is `1c901d09c3060eab1d41cf8fbde2fd6a96447bef1d2e687817d57bc124b3220a`.
After acceptance, the user authorized committing this pass and holding afterward.
No push was requested.
