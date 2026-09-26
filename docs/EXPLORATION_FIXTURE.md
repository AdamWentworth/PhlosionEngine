# Exploration preview ownership

Phlosion Engine provides the reusable collision, character, input, camera, audio, rendering, and editor-extension APIs used by exploration projects. Scene content, movement bindings, NPC behavior, battle rules, HUD art, and preview runtime/editor plugins belong to each game.

The former exploration course and Outskirt Stand preview now live in [PokemonCyberpunk](../../../Games/PokemonCyberpunk/docs/PREVIEW_QUALIFICATION.md). Its `CyberpunkPreviewRuntime`, `CyberpunkEditorPlugin`, tests, and native capture tools build from that game repository. Engine SDL extension contracts are exercised by source-neutral test plugins here.

For engine-only verification, run `PhlosionEngineContracts`, `PhlosionSdlExtensionValid`, `PhlosionSdlExtensionMalformed`, `PhlosionSdlExtensionAbsent`, and `tests/CheckEngineSemanticBoundary.cmake`. For the game integration and three-renderer matrix, follow the game's [test plan](../../../Games/PokemonCyberpunk/docs/TEST_PLAN.md).
