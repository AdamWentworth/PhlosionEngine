# M3 code completion design

The M3 exit gate is a game, engine and VFX integration gate. M3-A through E
provide animation, camera and UI CPU foundations, with synthetic animation
native evidence. The present game checkout has no Espeon, Umbreon, Outskirt
Stand or Rider Willie asset files, so asset-specific framing, skin/material,
font and three-renderer battle captures cannot close in this pass.

The remaining asset-independent sequence is:

1. Add a renderer-neutral audio mixer for decoded PCM clips, deterministic
   music/SFX/ambient/voice buses, fades, looping, spatial gain/pan and stop-all.
   Use a fixed stereo float output contract at 48 kHz. Keep decoding and asset
   IDs outside the engine; malformed clips and commands fail transactionally.
2. Connect the mixer to an opt-in SDL playback device with bounded queued
   frames, a no-device sink and explicit retry/teardown. Simulated device
   failure and cancellation must not leak or keep a voice alive.
3. Provide generic spawned/attached effect lifetime control using the existing
   Phlosion VFX runtime, then bind game-owned battle events to it. Review the
   current game VFX path before changing PhlosionVFX or game code.
4. Integrate battle camera, animation events, UI and audio into a synthetic
   game-owned double-battle fixture, including cancel/skip/exit cleanup. Native
   UI/text and effects require matched-frame expected-content tests on OpenGL,
   Vulkan and Direct3D 12 in runtime and editor.
5. Replace synthetic actors/scenes with the incoming assets and run the final
   M3 exit gate. This requires the other thread's asset handoff.

The engine owns reusable playback, presentation and lifetime contracts. The
game owns Pokemon moves, battle rules, strings, event bindings and the HUD.
PhlosionVFX owns reusable effect behavior and renderer-neutral batches. No
repository may depend upward on the game. A CPU result never substitutes for
the native visual or physical audio-device gate.
