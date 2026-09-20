# Phlosion Engine documentation

Start with the [main README](../README.md) for the engine's scope and public
showcase. These documents describe this repository's build and contracts;
game-specific authoring and visual qualification stay with the consuming project.

## Build, review and verify

| Document | Purpose |
| --- | --- |
| [Development](DEVELOPMENT.md) | Prerequisites, build configurations, editor startup and consumer targets |
| [Verification](VERIFICATION.md) | CI scope, local checks, visual requirements and dated evidence |
| [Engine boundaries](ENGINE_BOUNDARIES.md) | What belongs to the engine, a game or an optional feature package |
| [Extraction status](EXTRACTION_STATUS.md) | Completed separation work and remaining delivery limits |
| [Showcase media](SHOWCASE_MEDIA.md) | Branding and editor screenshot provenance |

## Implemented interfaces

| Document | Purpose |
| --- | --- |
| [Material profiles](MATERIAL_PROFILES.md) | Versioned project shader programs and parameter packing |
| [Editor project extensions](EDITOR_PROJECT_EXTENSIONS.md) | Game-plugin ABI, capabilities and command routing |
| [Optional editor packages](EDITOR_PACKAGES.md) | Explicit package declarations and extension points |
| [Editor performance](EDITOR_PERFORMANCE.md) | Development builds, statistics, recording, caches and automation |

## Architecture and future work

| Document | How to read it |
| --- | --- |
| [Editor architecture](EDITOR_ARCHITECTURE.md) | Current host and implemented interaction slices, followed by future milestones |
| [Asset architecture](PHLOSION_ASSET_ARCHITECTURE.md) | Shared resource design and long-term Forge/Vault plans; game examples are consumer evidence |
| [Environment authoring](ENVIRONMENT_AUTHORING.md) | Generic authoring contracts and the original consumer integration; source-specific behavior belongs outside the engine |

These plans do not promise that every proposed editor or asset-pipeline feature
ships today. The current maintenance pass improves presentation and independent
verification; it does not start a new engine feature milestone.
