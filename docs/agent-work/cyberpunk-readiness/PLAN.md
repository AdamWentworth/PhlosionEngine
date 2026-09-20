# Cyberpunk engine readiness audit

Date: 2026-09-20
Owner: Astra (architecture, scope, priorities, acceptance)
Implementation: one native `astra_flash_builder` (repository evidence and documentation)

## Objective and scope

Produce an evidence-backed roadmap for returning the planned Pokemon Cyberpunk
game from Unreal to Phlosion. This assignment implements documentation only;
it does not begin engine feature development or migrate the game.

Product context recovered from the user's supplied
[Engine Gap Analysis conversation](https://chatgpt.com/share/6aaee44c-a1a0-83e8-8b4c-e827f3140144).
The discussion describes a Colosseum-style Cyberpunk remake, first built in
Unreal for the course. The representative Phlosion qualification slice is
Outskirt Stand: controller exploration/collision, NPC/Rider Willie interaction
and dialogue, Espeon/Umbreon double battle, cameras/animation/VFX/audio/HUD,
return to the overworld, and save/quit/reload. Day/night/rain, wet materials,
Agate water and vegetation are relevant. The user explicitly added Quilava and
Typhlosion to the material/animation proof roster alongside baseline Umbreon,
localized tail fire and larger skinned fire cases. The prior assistant's gap
claims are historical hypotheses, not present implementation evidence. The
conversation is useful product context but not a complete signed-off design spec.

Do not require a seamless megacity, vehicles, multiplayer, destructive physics,
or Unreal feature parity. Scope those only if later requested. Add useful
inspect/command/validation/capture interfaces for agent-assisted authoring.
The Unreal repository currently exposes a minimal native module; its binary
content has not been audited and must not be called empty.

Engine primitives belong to PhlosionEngine; reusable specialized systems can
belong to PhlosionPackages/PhlosionVFX; Pokemon rules, content and game-specific
editor adapters belong to the game. Preserve these boundaries. Target game
requirements, not general Unreal feature parity. Integration of mature libraries
is a valid future alternative to building physics/audio/navigation from scratch;
selecting vendors and exact APIs is outside this audit.

## Baseline and ownership

- Engine: `D:/Projects/Phlosion/PhlosionEngine`, branch `main`, HEAD
  `c17d2122bd36b3c3bb49c4e703900f40cccfbd76`; initially clean, including index.
- Read-only consumers: `D:/Projects/Games/PokemonAutochess`,
  `D:/Projects/Games/PokemonCyberpunkUnreal`, sibling Phlosion packages and VFX.
- Astra owns this plan and `CHECKPOINT.md`.
- Flash may write only `docs/CYBERPUNK_ENGINE_READINESS.md`, one link in
  `docs/README.md`, and `docs/agent-work/cyberpunk-readiness/EVIDENCE.md`.
- No runtime/configuration changes, commits, staging, pushes, or asset mutations.
  Preserve unrelated work. No recursive delegation.

## Phase 1 — evidence and roadmap document

Audit source, tests, build dependencies and current contracts. Explicitly
distinguish implemented, partial, planned-only, not-found-in-inspected-scope,
and unverified at RPG scale. A header, stub, TODO, planned architecture section,
or a source-contract test is not runtime qualification. Describe negative search
scope instead of claiming global absence.

Cover platform/input/camera; runtime/ECS/jobs; resources/cooking/import; scene and
world streaming; physics/collision/traversal; animation; navigation/AI support;
rendering/lighting/materials/VFX; audio; player UI/text/accessibility/localization;
scripting/reflection; persistence; editor/content workflows; diagnostics,
performance, builds, packaging and release; and conditional networking/vehicles.
Include shared interfaces for game systems without putting Pokemon rules into
the engine. Check the existing graphics roadmap and issue register for known
consumer qualification limits.

Deliver an evidence matrix with source/test links and exact gaps, followed by
dependency-ordered milestones, owners, prerequisites, measurable exit gates,
and an Unreal-to-Phlosion portability strategy. Priorities:
requirements/benchmarks -> independent engine fixture and asset contracts ->
walkable Outskirt Stand -> double battle -> persistent RPG loop -> representative
environment scale/presentation -> editor production workflow -> migration/release gate.
Rendering parity, profiling, data stability and authoring are cross-cutting gates
from the first slice, not deferred cleanup phases. Avoid invented effort estimates.

The return gate must cover a packaged representative game slice, save/reload,
authoring/reimport, content scale and explicit CPU/GPU/memory budgets. All affected
native OpenGL/Vulkan/D3D12 gameplay and editor cases require matching-frame visual
and expected-content checks plus CPU contracts. Existing Autochess captures do not
certify a future RPG. Mark budgets TBD where product/hardware targets are unknown.

## Phase 2 — Astra acceptance

Review actual additions and index diff against the baseline; spot-check major
claims and the roadmap's dependencies. Validate local links, whitespace, scope,
and documented provenance. No runtime or GPU test reruns for documentation-only
work; new runtime readiness remains explicitly unverified. Consolidate any needed
corrections into one worker follow-up. Record accepted work and routing evidence
in the checkpoint and present the useful roadmap to the user.

## Routing evidence before dispatch

Installed doctor: static-ready, worker route
`deepseek/deepseek-v4.1-flash`, role `astra_flash_builder`, effort `high`.
Current root session metadata: `gpt-6-astra`, effort `xhigh`.
No project config override found in the current or engine repository. Role is
available in the native delegation tool. Inspect host/router metadata after the
real task; a static configuration check does not establish provider inference.
