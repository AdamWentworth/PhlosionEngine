# World material profiles

A world material profile supplies project-owned fragment-shader behavior without
rebuilding or specializing the engine library. `WorldMaterialProfile.h` owns the
versioned interface. Version 1 uses the existing world vertex inputs, textures,
material parameters and fragment outputs; changes to those bindings must update
the version and rebuild project Vulkan variants.

`loadWorldMaterialProfile(projectRoot, manifestPath)` reads a manifest whose file
paths are relative to the project root. The manifest declares `version`, `id`,
`opengl` and `d3d12` source files (`declarations` and `evaluation`), and four
`vulkan` SPIR-V files (`direct`, `direct_dual_source`, `indirect`, and
`indirect_dual_source`). A selected profile must provide every backend variant.

For OpenGL and D3D12, declarations replace the world shader's
`__PHLOSION_PROJECT_MATERIAL_DECLARATIONS__` marker and evaluation replaces
`__PHLOSION_PROJECT_MATERIAL_EVALUATION__` inside the fragment entry point.
Evaluation branches return after handling their own material modes. Unhandled
modes continue through the standard shader. The shader compiler caches use the
complete composed source, including project code.

Vulkan project builds compile `world.frag` and `world_indirect.frag` with
`PHLOSION_PROJECT_MATERIAL=1`, supplying `project_world_declarations.glsl`,
`project_world_evaluation.glsl` and their `project_world_indirect_*` counterparts
through a project-only include directory. Compile both again with
`PHLOSION_VULKAN_DUAL_SOURCE_BLEND=1`. The default engine build omits the project
define and has no dependency on those files. Preserve the engine's descriptor
bindings, vertex interface, and world-color output conventions.

Optional `d3d12_constant_overrides` maps decimal material-mode keys to objects
whose keys are named scalar `WorldPsConstants` fields. String values name scalar
`WorldTextureData` fields to copy; numeric values provide literal floats. The
loader resolves names to validated member offsets. Mappings use current draw
data, so camera-dependent values are not frozen when the profile loads.

Pass the loaded profile to a native backend constructor before pipeline creation.
`setWorldMaterialProfile` replaces pipelines between frames; passing `{}` restores
engine defaults. Profiles are copied by the renderer, have no plugin callbacks,
and do not depend on a plugin DLL remaining loaded. The editor's optional
`world_material_profile` descriptor entry selects the profile for that project;
absence selects the defaults. Closing a project restores those defaults too.
A failed candidate project restores the previous
profile. Reload a project to pick up edited profile files after rebuilding its
Vulkan artifacts.

Missing files, malformed/incomplete profiles, unknown mapping names, invalid
SPIR-V headers, shader compilation errors, or incompatible interface versions
are errors. They do not silently fall back to another API or another profile.
Backend parity and material appearance remain the consuming project's
verification responsibility.
