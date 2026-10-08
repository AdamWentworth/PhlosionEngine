# CPU-generated pixel presentation (OpenGL)

`engine::render::OpenGLPixelPresenter`, in `Phlosion::Render`, is an opt-in
OpenGL 3.3 component for a finished CPU-generated image. Existing render backend
interfaces, shaders, sprite paths, and default behavior are unchanged. Vulkan
and D3D12 support for this component is not implemented.

Create it after loading GL functions. Supply tightly packed, top-down RGBA8
pixels as `std::span<const std::uint32_t>`, source width/height, a top-left pixel
viewport, destination surface height and `PixelFilter::Nearest` or `Sharp`.
Alpha is ignored; this is opaque screen presentation. The caller owns aspect
ratio/integer layout, borders and framebuffer clearing. Calls do not retain the
input span. Three persistent textures rotate to avoid overwriting an image still
used by a previous fullscreen presentation. Storage is allocated per source-size
change, followed by one texture update and one triangle per frame. Textures are
linear RGBA8; Sharp blends encoded channel values only at pixel boundaries and does not perform
color correction, gamma conversion, sharpening halos or invented detail.

`timings()` returns the last call's CPU elapsed state-setup, upload and draw
submission times. They can include driver waits; they are not GPU execution
times. The caller can time the full call to include GL state restoration.

The current framebuffer is the destination. Program, VAO, viewport, texture
unit/binding/sampler, unpack buffer/layout, polygon fill mode, color mask and
modified raster enables are restored. Create/use/destroy on the render thread with the same GL context
current; destroy before that context. Invalid dimensions/count/filter throw
`std::invalid_argument`; shader setup failures throw `std::runtime_error`.

Configure with `PHLOSION_BUILD_TESTS=ON` and `PHLOSION_BUILD_GPU_TESTS=ON` to
enable `PhlosionPixelPresenterContracts` (CTest labels `gpu;opengl`). The GPU
option defaults off so ordinary source/CPU CI does not acquire a graphics-device
requirement. On a qualified machine, build `PhlosionPixelPresenterTests` and run
`ctest --test-dir <build> -C RelWithDebInfo -R PhlosionPixelPresenterContracts --output-on-failure`.

This contract creates a hidden real OpenGL window. It checks
nearest/sharp output against an independent box-footprint CPU reference,
fractional/integer enlargement, opaque borders, repeated uploads, source-size
changes, GL state restoration (including inherited wireframe, raster discard and
color logic), and invalid input. It requires a working GL 3.3
driver and SDL video environment. It is not a Vulkan/D3D12 qualification.
