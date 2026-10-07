# Screen feedback

`IRenderBackendWorld::endWorldSceneColorPass(ScreenFeedbackFrame)` completes a
linear world scene pass and composes display-encoded RGB8 history over it.
OpenGL, Vulkan and D3D12 implement the same operation. A frame contains up to
four ordered rectangular passes per update: previous history, alpha-masked
history, or black. Coordinates have a top-left origin. Alpha blending rounds
to bytes after each pass. Four subpixel samples are averaged with truncation
into the next history texture. The final output evaluates the last program
against its previous history, preserving the high-resolution presentation.

Call with `seed=true` to snapshot the outgoing scene and select a nonzero
sequence identity. Then finish incoming scene passes with that identity and
zero or more updates. Zero updates preserve the clock; multiple updates use
the same current scene input. Reset/re-entry must seed again. Texture keys
identify immutable ingredients. The frame borrows spans/pixels for the call;
backends retain GPU copies, not caller pointers.

There is one active feedback sequence per renderer. History dimensions are
fixed after initial allocation, independently of output viewport dimensions.
Output resize is allowed. Resources are reclaimed with renderer shutdown.
The optional `Phlosion::VfxLibraryBridge` translates a portable MaskFeedback
screen package and its explicit update clock through `PortableScreen`.
Unsupported operations fail instead of silently degrading.

`PhlosionScreenFeedbackTests` compares each API against an independent CPU
reference with opaque/translucent masks, alpha-128 gating, black passes,
fractional rectangles, history interpolation, RGB8 rounding, batched updates,
paused presentation and widescreen output. `PhlosionPortableVertexTests`
checks explicit particle corners, per-vertex colors/alpha and UVs through a
world transform. The current depth-writing portable mesh path is intended
for opaque/cutout meshes; arbitrary translucent depth-write materials are
not qualified by that path.
