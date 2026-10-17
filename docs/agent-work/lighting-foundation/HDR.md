# Scene radiance and bloom contract

Implemented 2026-10-16; native scene acceptance is recorded in CHECKPOINT.md.
The target is conventional lighting behavior; no hardware ray tracing or exact
Cyberpunk shader port is implied.

## Data flow

World scene-color targets use RGBA16F in runtime and editor. Display textures,
swapchains and editor chrome remain their existing formats. Generic renderers
retain compatible pipelines for direct display-target world draws. D3D12 RTV
formats and Vulkan render-pass compatibility must match the actual bound target;
changing only texture allocation is insufficient.

Scene-linear output is an explicit per-view mode, disabled at a new world pass.
The game enables it for its outdoor environment before submitting sky/geometry.
Model inspection, other games and source-fidelity material views retain their
existing output convention unless they opt in. Shared material code handles the
new mode; source-specific Pokemon decoding remains in its adapter.

In radiance mode, preserve nonnegative unclipped linear lighting and emission.
Compose opaque and transparent radiance, then apply the authored view exposure
consistently to the result. Extract and filter bright radiance, compose the optical
halo, then apply the established ACES display curve and sRGB conversion once.
The completed view is opaque for editor compositing. UI and screen-feedback
effects composed after that boundary keep their display-space behavior.

Storage is RGBA16F even for non-opted-in world views. They retain per-material
mapping and the existing display transfer; intermediate blend quantization can
differ from the former RGBA8 targets. Their native character/content and parity
checks are mandatory. This does not promise byte-identical legacy blending.

`IWorldSceneRadiance` is an optional interface, leaving IRenderBackend's vtable
unchanged. Positive exposure enables radiance; each new color pass resets it to
zero. Runtime/editor pipelines match float world targets and normalized display
targets. Sky radiance also remains unclipped. Display dithering occurs after the
curve and sRGB transfer. Feedback seeds map/dither into their existing RGB8 history
before source-authored display-space feedback operations.

The public native ramp tests use an independent scalar ACES reference. They test
emission 0.25/1/4/16, additive, alpha, premultiplied and dual-source composition,
bloom response above 1, invalid exposure, per-view reset and feedback seeding.
They also exposed and fixed an OpenGL first-draw bug: streaming mesh handles must
be initialized before they are copied into the internal submission call.

Use one shared display curve across native backends. Keep the legacy bloom path
for consumers that have not enabled scene radiance. The new radiance path must
distinguish emission above 1.0 and preserve hue through the glow. Do not add
colored fill lights, alter source radiance to hide pipeline bugs, or call bloom
indirect illumination. Puddles receive the same direct-light visibility as other
materials; scene-object reflections are still a later system.

## Required verification

- CPU/contract coverage of display transform, format policy and mode reset.
- A public intensity ramp proves that values above 1 survive until bloom/tone
  mapping; compare halo response at multiple exposure/emission strengths.
- Existing opaque/cutout/alpha/additive/dual-source material paths agree across
  APIs and runtime/editor. Preserve non-opted-in character/source views.
- Dry/wet/night/day station and battle captures, lights-off/bloom-off controls,
  and camera movement; check rough sand versus asphalt versus shallow water.
- Rebuild all affected consumers and run the shared character/editor smoke matrix.
- Record native GPU cost and startup/resource changes; preserve original failures
  and unchanged visual thresholds. Do not report the 75% target as measured parity.
