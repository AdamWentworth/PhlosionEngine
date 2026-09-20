# World material profiles

A world material profile supplies project-owned vertex and fragment programs.
`WorldMaterialProfile.h` owns the versioned interface. Version 2 preserves world
vertex inputs, textures, scalar parameters and fragment outputs, and adds vertex
snippets, per-mode packing settings and scalar expressions. Binding changes
require a version bump and rebuilt project artifacts.

`loadWorldMaterialProfile(projectRoot, manifestPath)` reads paths relative to the
project root. The manifest declares `version`, `id`, and four source pairs:
`opengl`, `d3d12`, `opengl_vertex`, `d3d12_vertex`, each with `declarations` and
`evaluation`. Its `vulkan` object contains six SPIR-V files: `direct`,
`direct_dual_source`, `indirect`, `indirect_dual_source`, `direct_vertex`, and
`indirect_vertex`. Every variant is required for a selected profile.

OpenGL and D3D12 insert those pairs at
`__PHLOSION_PROJECT_MATERIAL_DECLARATIONS__` and
`__PHLOSION_PROJECT_MATERIAL_EVALUATION__`. Fragment evaluation supplies the
complete material implementation, including ordinary surfaces. Vertex evaluation
runs before skinning and model transforms. Empty profiles select the engine's
standard PBR implementation and undeformed vertices. Shader caches use the
complete composed source, including project code.

Vulkan projects compile `world.frag`, `world_indirect.frag`, `world.vert` and
`world_indirect.vert` with `PHLOSION_PROJECT_MATERIAL=1`. Supply
`project_world_declarations.glsl`, `project_world_evaluation.glsl`,
`project_world_vertex_evaluation.glsl` and corresponding `project_world_indirect_*`
files through a project include directory. Compile both fragments again with
`PHLOSION_VULKAN_DUAL_SOURCE_BLEND=1`. The independent engine build omits these
defines and has no dependency on project includes. Preserve descriptor bindings,
vertex interfaces and world-color output conventions.

Optional `modes` entries use decimal byte-sized mode keys. Each can set
`pbr_packing`, `opengl_debug_parameter`, `d3d12_debug_parameter` (default true),
and `occlusion_maximum` (default 1). The engine has no project-specific material
IDs or packing exceptions.

Optional `d3d12_constant_overrides` maps mode keys to named scalar
`WorldPsConstants` destinations, including vector components such as
`projectedShadowRowX.0`. Numeric values are literals; strings copy a scalar
`WorldTextureData` member. An object term selects `source`, `constant`, or
`value`, then optionally applies `minimum`/`maximum`, `bias`, `scale` and `round`
in that order. A `terms` array sums multiple terms. `constant` reads the current
packed constant, so mappings should not depend on the order of other overrides.
An optional `when` object names a texture-data `source` and exclusive
`greater_than`/`less_than` bounds. The loader validates member offsets and finite
parameters. Evaluation uses current draw data, including the current camera.

Pass the loaded profile to a backend before pipeline creation.
`setWorldMaterialProfile` replaces pipelines between frames; `{}` restores the
defaults. Profiles are owned copies, contain no plugin callbacks, and remain
valid after a plugin DLL unloads. The editor's `world_material_profile` descriptor
selects the project profile. Closing a project restores defaults; failed project
activation restores the previous profile. Reopen after rebuilding edited shader
artifacts. Editor and plugin must share ABI 34.

The editor labels lighting preset 4 `Authored Stage`; its interpretation belongs
to the profile. The CLI accepts `authored-stage` and retains older spelling aliases
for capture-script compatibility. Other review presets remain generic diagnostics.

Missing files, invalid mappings, malformed or incomplete SPIR-V, shader compiler
errors and incompatible interface versions fail explicitly. They never silently
select another renderer or profile. Material appearance and native backend parity
remain the consuming project's verification responsibility.
