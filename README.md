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
