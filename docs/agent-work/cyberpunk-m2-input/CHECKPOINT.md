# M2-D checkpoint

Status: accepted by Astra on 2026-09-22; implementation complete and uncommitted.

- M2-C committed as requested: `6a72dedc7e5aff64cf9bbf92e7114eaa2981db84`.
  Clean before this plan. M2-D is the named-action and SDL-controller dependency
  before the camera/host integration pass. No automatic commit/push requested.
- PLAN defines SDL-free actions plus an opt-in Platform adapter, transactional
  rebinding, explicit frames/controller selection, deadzones and focus recovery.
  Existing InputEvent ABI, runtime/editor event pumps and render behavior stay
  unchanged. Real SDL virtual-device tests are required; no physical-device or
  native walkable-scene qualification is implied.
- Root owns architecture/shared docs/acceptance. Implementation uses the approved
  Astra fallback following the recent Flash HTTP402, with unchanged routing.
- Explicitly selected Astra child `/root/cyberpunk_m2_input_astra` completed the
  bounded PLAN and REPORT, then was interrupted after completion. No worker
  self-approval; root reviewed the actual implementation, tests and evidence.
- Delivered `Phlosion::Input`, named Button/Axis1D/Axis2D actions, rebinding,
  explicit frame edges, controller selection/deadzones and neutral rearm across
  focus/enable changes. The Platform adapter owns balanced SDL references and
  mapped handles, forwards cached event snapshots and polls without consuming
  the event queue. Six pure and three real SDL virtual-device groups cover it.
- Full enabled Debug build and fourteen CTest entries passed (299.96 seconds).
  A second full run with temporary global `/WX` passed (355.57 seconds): all 37
  compiled targets / 177 compiler commands used `/WX`. Existing aggregate tests
  remain `/W1`; other targets use `/W4`. Original flags restored. Final test-only
  cleanup passed a full build and both input executables directly.
- Evidence lives under ignored `artifacts/m2-d/`. Root matched all eleven final
  source/document/binary hashes, checked direct import reports, and accepted with
  no outstanding findings. See [ACCEPTANCE](ACCEPTANCE.md) and [REPORT](REPORT.md).
- Roadmap, verification, boundaries, consumer target guide and documentation
  index reflect M2-D. New work is not staged, committed or pushed.
- Resume with exploration camera design and action/character host integration.
  Mouse-motion/scroll action sources are still absent; define the camera input
  contract explicitly. Qualify physical input and runtime/editor focus/capture,
  then the scene on all three native render APIs at matching frames and 16:9 /
  21:9. Virtual-device tests alone do not close M2. Game content stays in its repo.
- Commit follow-up: the user requested committing M2-D and continuing. Root
  rechecked the accepted scope and whitespace; implementation and test evidence
  are unchanged. This commit contains only M2-D and its documentation. No push.
