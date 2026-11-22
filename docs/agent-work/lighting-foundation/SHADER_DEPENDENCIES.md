# Shader build ownership and incremental preparation

2026-11-02 follow-up to SHADER_STARTUP.md. gpu-worker Windows, optimized
RelWithDebInfo, Visual Studio 18 2026 generator and the existing glslang toolchain.

## Ownership

cmake/CompileVulkanShader.cmake owns the common offline Vulkan compile rule:
the explicit source, output and compiler options remain at each call site.
The compiler writes the actual transitive include dependencies. The same rule
serves engine programs, native test programs and composed project materials.
Compiler replacement also invalidates outputs. No shader options, arithmetic,
runtime material dispatch, quality budgets, public ABI or driver-cache policy
change in this pass.

cmake/ComposeMaterialProfile.cmake composes into a target-specific build staging
directory. An optional POST_COMPOSE function receives that directory and may
finish the existing composed files. Only their final contents are published,
using content-preserving copies. Unknown hooks fail configuration.

PokemonCyberpunk's cmake/ComposeWorldMaterials.cmake owns its rain, foliage and
final-output hooks. They no longer rewrite the published files after the engine
has composed them. ModelSupport retains its character fragments; the engine
retains generic compiler/composition behavior.

The explicit material target still prepares all nine required SPIR-V programs.
The existing runtime profile/warmup lists and prepared D3D12 publication remain
authoritative; this does not introduce another preparation service or feature
switch. Compiled content is unchanged, so no new prepared-shader asset release
is required.

## Measured incremental behavior

Same worker/build directory; each source probe appends a harmless comment and
restores the exact original bytes. Times include CMake regeneration where needed.

| Operation | Before | After | Vulkan material programs rebuilt |
| --- | ---: | ---: | --- |
| Direct3D rain-only edit | 38.946 s | 6.748 s | 9 -> 0 |
| Rain motion edit | all 9 depended on it | 7.350 s | 3 vertex/motion programs |
| Rain fragment edit | all 9 depended on it | 38.522 s | 6 fragment programs |
| Unchanged material build | 0.474 s | 0.490 s | 0 |
| Plain reconfigure | 13 published sources rewritten | none rewritten | 0 |

A plain reconfigure previously rewrote intermediate generated sources but did
not itself rebuild SPIR-V: those generated files were missing from the old
explicit dependency lists. The new compiler dependencies include them without
causing false invalidation because publication now preserves unchanged files.

These are developer build times, not gameplay frame times or empty-driver-cache
startup times. Shared fragment dispatchers still contain multiple material
families. Editing a real shared fragment correctly rebuilds its consumers.
Separating their runtime program identities is further work; it is not claimed
by this dependency cleanup.

## Verification

PhlosionShaderBuildDependencies uses real glslang compilation in an isolated
fixture. It verifies stable post-composition publication, no-op builds,
transitive/newly introduced includes, and rejection of a missing include instead
of stale output reuse. Actual game probes additionally cover HLSL-only,
rain-fragment and rain-motion changes and restoration.

The optimized engine/editor and game/runtime/plugin builds preserve all 61
previous engine/game SPIR-V files byte-for-byte. The game composition probe also
preserves every original GLSL/HLSL source. Six engine CTest groups, eight game
CTest groups and eleven asset-package unit tests pass.

The existing PhlosionEngineSemanticBoundary check flags three untouched files:
world_atmosphere.glsl, world_contact_compute.glsl and TestFogComposition.cpp.
The exact check and flagged files match the baseline commit; this pass neither
changes them nor relaxes that guard. The failure is retained in the evidence.

Runtime/editor/API results and the final locked integration receipt are retained
in artifacts/lighting-foundation/shader-organization-20261102/evidence.zip.
No parity category is rescored. Linux and other generators remain unqualified.

## Next loading work

The previous locked normal D3D12 startup trace separates 6.357 s of field asset
construction from 17.862 s of battle construction/prewarming. Battle includes
7.597 s loading participating moves (effects and audio), about 0.997 s reloading
its field asset, and 5.102 s GPU prewarming. Nested durations must not be added
again to the outer 17.862 s total.

Audit immutable asset sharing, prepared move payloads and upload batching before
parallelizing those producers. Do not defer work into the first battle frame,
share mutable animation/weather state, or mistake these costs for shader
compilation. New-program shader-family separation and asset loading remain
distinct follow-ups.
