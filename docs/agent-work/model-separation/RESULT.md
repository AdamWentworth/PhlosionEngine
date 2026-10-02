# Model separation result

Generic 3D support is part of PhlosionEngine, included by `Phlosion::Engine`.
`PokemonModelSupport` is an optional local source adapter. Both games consume
the engine and adapter independently; neither supplies the other game's code.
See [model APIs and ownership](../../MODELS.md).

## Ownership

| Owner | Contents |
| --- | --- |
| PhlosionEngine | Neutral model data/cooking/loading, animation tracks and poses, instances, skinning, billboards, indexed rendering, runtime provider and ordinary inspector |
| PokemonModelSupport | Native Pokemon IR translation, recovered character shaders/materials, source compatibility and probes, offline cooker and thin provider entry points |
| Games | Actions, simulation, placement, procedural policies, scenes/content selection, field programs and game/editor integration |
| Research | Extraction, retargeting, recoloring and source provenance |

The runtime provider has no editor or ImGui linkage. Model-render ABI 5 makes
grounding and lighting choices explicit and supports static bind poses. Instances
own their geometry, material variants and 16 pose slots; copying is disabled.
Failed loads preserve valid resources. Source metadata caches are bounded and
thread-local; the runtime instance owns its material templates.

Cyberpunk builds and stages its own model DLLs and cooker. Import, capture and
research entry points no longer borrow an Autochess executable. Generic image
verification helpers live in the engine; native payload publication lives in the
optional adapter, with compatibility entry points for existing Autochess tools.

The private catalog is a separate local publication at
`D:/ProjectData/Phlosion/PokemonModels/cooked`: 570 models and 6,884 files, including
two probes, copied and SHA256-verified. Original inputs remain intact. Cyberpunk's
legacy `content/autochess` mount and asset IDs now resolve to that publication.
No private assets were added to Git or uploaded.

## Verification

- Engine model-core, instance/provider, full engine contracts and strict semantic
  boundary pass in Debug and RelWithDebInfo. Generic runtime/preview DLLs build
  without the Pokemon adapter. Move-only instance contracts pass both configurations.
- Autochess's complete C++ suite passes both configurations; its 19 boundary and
  material gates pass. Fresh targeted tests pass after the cache-lifetime refinement.
- Cyberpunk's complete 11-test CPU suite and ABI 5 fractional/one-shot checks pass
  both configurations. The adapter's 11 independent tests pass both configurations,
  including runtime replacement, isolation and 300 live resource generations.
- All 47 Cyberpunk inspector cases pass across OpenGL, Vulkan and D3D12: 141
  captures, including original clips, retargets, source lighting and material checks.
- The 24 accepted shiny Espeon inspector captures and 30 accepted Umbreon inspector
  captures are pixel-identical. Shiny Espeon mesh, skeleton, animation and material
  resources also recook byte-for-byte identically through the new cooker.
- All 36 shiny Espeon and 48 regular/shiny Umbreon battle captures pass on all three
  APIs, both runtime/editor hosts and both 16:9/21:9 aspects. The 84 captures match
  accepted baselines within the unchanged thresholds (maximum normalized MAE
  0.000003182; maximum changed-pixel fraction 0.000056217).
- Autochess's 17 inspector material cases (51 captures) and two runtime cohorts
  (six captures) pass on all three APIs, including field composition and recovered
  fire, smoke, eyes, fur, wings and source-stage lighting.
- Both games configure and build from independent source exports without the
  other game at a sibling lookup path. Generated build graphs contain zero
  references to the other game. Cyberpunk's isolated editor probe and runtime/editor
  battle checks pass. Autochess's isolated executable also passes the character
  cohort capture/content/parity gate on OpenGL and D3D12.
- PowerShell entry points parse, importer PlanOnly succeeds, shared payload/image
  helper tests pass, and repository diff whitespace checks pass.

The existing Umbreon special-a visibility guard sampled rings that are intentionally
fully dim. Its region now samples the visible face/eyes using the same contrast
threshold. No numeric image thresholds were relaxed.

## Evidence locations

- Engine: `artifacts/model-separation/`
- Adapter: `artifacts/lifetime-*` and `artifacts/runtime-stress-*`
- Cyberpunk: `artifacts/model-separation/` and `artifacts/model-separation-*.log`
- Autochess: `artifacts/model-separation/`
- Isolation: `D:/ProjectData/Phlosion/Qualification/model-separation-20261002/`

The optional adapter is a local Git repository without a remote. Matching local
engine/adapter revisions are required until release pins are updated. Publishing
the adapter and updating release pins remain separate from this local migration.
Existing source-material license exclusions follow the moved files.

Execution continued in the root session after the delegated Flash worker failed
before edits with HTTP 402. No model/provider configuration was changed.
