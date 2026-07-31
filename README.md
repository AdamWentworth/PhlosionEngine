# Phlosion Engine

Phlosion Engine is a reusable C++20 game engine extracted from Pokemon
Autochess. It owns the runtime, rendering backends, platform integration,
resource containers, scene loading, common UI, and reusable preview tooling.
Games remain separate consumers and supply their own gameplay code and
content.

The current renderer supports OpenGL, Vulkan, and Direct3D 12. The asset
runtime includes the first PHRC-based `.phlo` and `.phscene` contracts. The
repository intentionally contains engine code and engine-owned shaders only;
proprietary source assets and game content do not belong here.

## Build

Requirements:

- CMake 4.2 or newer
- a C++20 compiler
- vcpkg through `VCPKG_ROOT`
- the Vulkan SDK on configurations that enable Vulkan

On the current Windows development setup:

```powershell
cmake --preset vs2026
cmake --build --preset debug
ctest --preset debug
```

## Editor

`PhlosionEditor.exe` is an Engine application. Start it without a project to
open the project browser:

```powershell
cd D:\Projects\PhlosionEngine
.\build\Debug\PhlosionEditor.exe
```

Choose a game's tracked `phlosion.project.json`, select a recent project, drag
the descriptor onto the window, or pass it directly:

```powershell
.\build\Debug\PhlosionEditor.exe `
  D:\Projects\PokemonAutochess\phlosion.project.json
```

The editor stores its recent-project list, dock layout, and outer-window
placement in the operating system's local application-data directory. Move it
to the preferred monitor once and future launches restore that placement.
Games may provide a generated editor plugin for game-specific loading and
inspectors; the Engine still owns the executable, project browser, render loop,
window, and common editor UI.

On Windows, `Auto` selects Direct3D 12 for both the editor shell and its
embedded render surfaces. Direct3D 12, Vulkan, and OpenGL are all available
from `Edit > Rendering API`; changing the selection persists the preference
and restarts the editor with the open project restored. Vulkan uses its native
Dear ImGui bridge and renderer-owned offscreen surfaces, while OpenGL remains
the broad compatibility option.

An opened scene starts in Edit mode with simulation time frozen. The central
Viewport switches between Scene and Game surfaces. Scene shows the editable
asset view; Game hosts one persistent project runtime in-process. A project
plugin can expose named game previews and switch that warm runtime between
frontend, mode, phase, and snapshot states without launching another process
or repeating application startup. Play/Pause/Step drive the selected runtime
surface. Standalone launch configurations remain available for workflows that
specifically require a separate process.

Consumers can initially use the engine as a CMake subdirectory:

```cmake
add_subdirectory(path/to/PhlosionEngine)
target_link_libraries(MyGame PRIVATE Phlosion::Engine)
```

See [docs/EXTRACTION_STATUS.md](docs/EXTRACTION_STATUS.md) for the active
separation boundaries and
[docs/PHLOSION_ASSET_ARCHITECTURE.md](docs/PHLOSION_ASSET_ARCHITECTURE.md)
for the shared cooked-resource design. The native editor boundary and first
vertical slice are defined in
[docs/EDITOR_ARCHITECTURE.md](docs/EDITOR_ARCHITECTURE.md).
