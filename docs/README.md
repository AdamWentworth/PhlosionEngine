# Phlosion Engine documentation

Start with the [main README](../README.md) for the engine's scope and public
showcase. These documents describe this repository's build and contracts;
game-specific authoring and visual qualification stay with the consuming project.

## Build, review and verify

| Document | Purpose |
| --- | --- |
| [Development](DEVELOPMENT.md) | Prerequisites, build configurations, editor startup and consumer targets |
| [Verification](VERIFICATION.md) | CI scope, local checks, visual requirements and dated evidence |
| [glTF import verification](GLTF_IMPORT_VERIFICATION.md) | CPU evidence for the existing character/animation importer and material metadata, with runtime coverage limits |
| [glTF texture decoding verification](GLTF_TEXTURE_VERIFICATION.md) | Real PNG/JPEG loader-to-decoder evidence for five material roles, sampler metadata, alpha integration and remaining rendering gaps |
| [Material verification](MATERIAL_VERIFICATION.md) | Five decoded material maps and corrected empty-image fallbacks in native runtime/editor previews on all three APIs, with content and parity guards |
| [Integrated scene verification](SCENE_FIXTURE.md) | Imported animated character/environment, replacement, unload and independent color-factor checks in both hosts on all three APIs |
| [Animation verification](ANIMATION_VERIFICATION.md) | LINEAR/STEP/CUBICSPLINE pose sampling, cache fidelity and public weighted skeletal deformation in native runtime/editor previews on all three APIs |
| [Engine boundaries](ENGINE_BOUNDARIES.md) | What belongs to the engine, a game or an optional feature package |
| [Extraction status](EXTRACTION_STATUS.md) | Completed separation work and remaining delivery limits |
| [Showcase media](SHOWCASE_MEDIA.md) | Branding and editor screenshot provenance |

## Implemented interfaces

| Document | Purpose |
| --- | --- |
| [Material profiles](MATERIAL_PROFILES.md) | Versioned project shader programs and parameter packing |
| [Asynchronous asset loading](ASYNC_ASSET_LOADING.md) | Background byte reads, request lifetime, cancellation and retained-payload limits |
| [Asset dependency loading](ASSET_DEPENDENCY_LOADING.md) | Required asset graphs, PHRC integrity, dependency hashes and session ownership |
| [Staged archive activation](ARCHIVE_ACTIVATION.md) | Scene/prefab candidate validation, explicit activation and retained snapshots |
| [Typed static mesh assets](MESH_ASSETS.md) | Portable PHMS geometry, submeshes, material slot labels, bounds and strict CPU validation |
| [Static collision queries](COLLISION_WORLD.md) | Opt-in Jolt-backed boxes/meshes, layers/sensors, ray/capsule casts, precise overlaps and world ownership |
| [Kinematic characters](CHARACTER_CONTROLLER.md) | Fixed-tick walk/run, gravity, grounding, slopes, stairs, floor following, recovery and safe teleport with standalone CPU contracts |
| [Character trigger events](CHARACTER_TRIGGERS.md) | Explicit discrete sensor Enter/Stay/Exit samples, independent filters, copied metadata and lifecycle rules |
| [Named input actions](INPUT_ACTIONS.md) | SDL-free action state, rebinding, deadzones, controller selection and focus recovery, plus the opt-in SDL adapter |
| [glTF scene assets](GLTF_SCENE_ASSETS.md) | Owned CPU scene snapshots, validated import, pose sampling and a reusable world-renderer adapter |
| [Public asset fixture](PUBLIC_ASSET_FIXTURE.md) | Synthetic archive rendering, scene replacement and native runtime/editor capture verification |
| [Editor project extensions](EDITOR_PROJECT_EXTENSIONS.md) | Game-plugin ABI, capabilities and command routing |
| [Optional editor packages](EDITOR_PACKAGES.md) | Explicit package declarations and extension points |
| [Editor performance](EDITOR_PERFORMANCE.md) | Development builds, statistics, recording, caches and automation |

## Architecture and future work

| Document | How to read it |
| --- | --- |
| [Editor architecture](EDITOR_ARCHITECTURE.md) | Current host and implemented interaction slices, followed by future milestones |
| [Asset architecture](PHLOSION_ASSET_ARCHITECTURE.md) | Shared resource design and long-term Forge/Vault plans; game examples are consumer evidence |
| [Environment authoring](ENVIRONMENT_AUTHORING.md) | Generic authoring contracts and the original consumer integration; source-specific behavior belongs outside the engine |
| [Cyberpunk engine readiness](CYBERPUNK_ENGINE_READINESS.md) | Recovered game context, present engine capabilities and gaps, a dependency-ordered readiness roadmap and the migration return gate |

These plans do not promise that every proposed editor or asset-pipeline feature
ships today. The readiness roadmap distinguishes accepted implementation passes
from remaining work. Runnable samples live in examples; verification utilities
and automated checks live in tools and tests respectively.
