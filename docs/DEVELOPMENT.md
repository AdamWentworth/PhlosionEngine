# Building and using Phlosion Engine

## Requirements

The maintained workstation and CI path is Windows x64 with MSVC. Install:

- Visual Studio 2026 or 2022 Build Tools with the C++ desktop workload and Windows SDK;
- CMake 4.2 or newer for the checked-in presets;
- Git and vcpkg, with `VCPKG_ROOT` pointing to the vcpkg checkout;
- graphics drivers for whichever native API the editor will use.

`vcpkg.json` pins the dependency baseline and Lua override. CMake uses the
manifest to restore SDL2, font support, ImGui, Vulkan headers/loader, glslang
and the other libraries. Vulkan is a required build dependency in the current
CMake graph, even if you intend to run OpenGL. A separate Vulkan SDK install
is not required when these manifest dependencies are available.

The standalone engine builds without Pokemon Autochess, PhlosionVFX,
PhlosionPackages or a private content depot. Projects remain responsible for
restoring their own content and building any plugins/packages they declare.

## Build configurations

From the repository root:

```powershell
cmake --preset vs2026
cmake --build --preset editor
```

| Configuration | Use | Build command |
| --- | --- | --- |
| Development (`RelWithDebInfo`) | Daily editor use: optimized code, symbols and assertions | `cmake --build --preset editor` |
| Debug | Diagnostics and contract testing | `cmake --build --preset debug` |
| Release | Performance measurements and release-style qualification | `cmake --build --preset release` |

The `editor` preset builds `PhlosionEditor` and `PhlosionEditorPluginProbe`.
The `development`, `debug` and `release` presets build all enabled targets.
When changing build configurations, rebuild the editor and the project's plugin
together. ABI validation rejects incompatible modules.

Visual Studio 2022 uses its own build directory:

```powershell
cmake --preset vs2022
cmake --build --preset vs2022-debug
ctest --test-dir build-vs2022 -C Debug --output-on-failure
```

For Ninja, start in an x64 MSVC developer shell and use `ninja-msvc`,
`ninja-debug` / `ninja-release`, and `ctest --test-dir build-ninja -C Debug
--output-on-failure`. Other operating systems are not covered by the current
Windows CI workflow.

`PHLOSION_BUILD_EDITOR` and `PHLOSION_BUILD_TESTS` default to `ON` for a
standalone engine checkout and `OFF` when included by another CMake project.
CI explicitly enables both and `PHLOSION_WARNINGS_AS_ERRORS`.

## Run the editor

```powershell
.\build\RelWithDebInfo\PhlosionEditor.exe
```

Without a project, the editor opens its project browser. Select a tracked
`phlosion.project.json`, drag a descriptor onto the window, select a recent
project, or pass a descriptor directly:

```powershell
.\build\RelWithDebInfo\PhlosionEditor.exe --project=D:\Projects\YourGame\phlosion.project.json
```

Recent projects, layout, window placement and rendering preferences live in
the operating system's local application-data directory. They are not shared
project files. On Windows, `Auto` selects Direct3D 12. Choose another API under
**Edit > Preferences > Rendering**, or launch with `--renderer=opengl`,
`--renderer=vulkan` or `--renderer=d3d12`.

Projects open in Edit mode with simulation frozen. Scene displays the editable
asset view; Game hosts the project's persistent runtime. Play/Pause/Step control
the selected surface. The project supplies scene catalogs, preview scenarios,
gameplay and content through its plugin. Optional packages load only when
declared by the project; there is no mandatory Tile Tools dependency in this build.

For isolated automation, use `--hidden`, `--frames=<count>` and
`--state-directory=<temporary-directory>`. See [editor performance](EDITOR_PERFORMANCE.md)
for captures, metrics and recording conventions.

## Consume the engine

The supported integration is a CMake source checkout. There is no installed
`PhlosionConfig.cmake` package yet. After creating a game target, use:

```cmake
add_subdirectory("${PHLOSION_ENGINE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/phlosion-engine")
target_link_libraries(MyGame PRIVATE Phlosion::Engine)
```

Supply `-DPHLOSION_ENGINE_SOURCE_DIR=<engine-checkout>` when configuring the
game, and make its dependencies available through the same vcpkg/toolchain setup.
Consumers should pin the engine revision they qualify.

| Target | Role |
| --- | --- |
| `Phlosion::Engine` | Core, platform and rendering aggregate |
| `Phlosion::Core` | ECS, events and cooked-resource/scene primitives |
| `Phlosion::Platform` | SDL window and input integration |
| `Phlosion::Render` | Native rendering, models, animation, resources and common UI |
| `Phlosion::Runtime` | Application lifecycle; includes the Engine aggregate |
| `Phlosion::EditorCore` | Descriptor, plugin ABI, preview routing and reload contracts |
| `Phlosion::Editor` | Native editor implementation; available with `PHLOSION_BUILD_EDITOR=ON` |
| `Phlosion::VfxPreview` | Generic preview host; effect implementations remain external |

Projects implement the [editor extension boundary](EDITOR_PROJECT_EXTENSIONS.md)
and can select [world material profiles](MATERIAL_PROFILES.md). Rebuild generated
project artifacts when the corresponding ABI or material interface changes.

## Verify a change

```powershell
cmake --build --preset debug
ctest --preset debug
```

Read [verification](VERIFICATION.md) before claiming renderer or editor parity.
The CPU contracts run without private assets or a GPU context; their result
does not substitute for native visual qualification.
