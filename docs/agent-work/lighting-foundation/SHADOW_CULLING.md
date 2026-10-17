# Shadow submission optimization

2026-10-16. Continue the accepted lighting foundation by reducing repeated static
scene submissions. Preserve all source colors, shadow resolution/filtering,
animation, HDR, weather and gameplay. No bounce or reflection feature in this pass.
Root implements under the user's existing fallback authorization after the recorded
DeepSeek credit failure. Before-state: `artifacts/lighting-foundation/shadow-culling/before`.

The glTF adapter currently submits every scene primitive to every shadow view.
Import conservative local bounds from decoded positions. Add opt-in per-pass
frustum culling before material binding/backend submission. Compose current node
and instance placement, test all eight corners against homogeneous clip planes,
and reject only if all corners are outside one plane. Keep validation transactional.
Absent/invalid bounds fail open. Only ordinary PBR mode 2 unskinned primitives
qualify; custom vertex materials, skinning and prewarm bypass the optimization.
No camera-frustum test substitutes for a light-frustum test. Foliage retains its
existing wind-expanded bounds and light matrix.

Game field and battle opt in for the environment's shadow draws; actors and normal
color draws remain unchanged. `Verify-SkyWeather -DisableShadowCulling` provides
the otherwise identical control. The IRenderBackend vtable and shaders are
unchanged; the glTF C++ layouts require a paired host/plugin rebuild.

Acceptance: import/draw CPU contracts (six planes, contact, eye crossing, affine
placement, invalid inputs, prewarm and skinned fallback); three-API runtime/editor
station and battle content/parity; CPU submission-count checks, culling on/off
images and matching 1080p warm timings. Review low sun and oblique views, scene switching and normal
launch. Rebuild hosts/plugins affected by C++ asset layout changes together.
Record measured gains rather than assuming a particular frame target.

## Measured result

Clear-night station, 1920x1080, all local shadows/HDR/normal bloom enabled,
200 warm frames [30,230), sequential GPU runs with Cyberpunk 2077 closed:

| API | GPU without/with culling ms | Host frame without/with culling ms | Local submission without/with ms |
| --- | ---: | ---: | ---: |
| OpenGL | 41.115 / 36.573 | 42.801 / 38.233 | 13.643 / 9.063 |
| Vulkan | 23.194 / 22.778 | 63.291 / 48.456 | 27.980 / 13.558 |
| D3D12 | 21.721 / 12.701 | 21.982 / 12.943 | 9.111 / 4.052 |

Reverse-order D3D12 repeat: GPU 21.632 / 13.280 ms and host frame 21.939 /
13.872 ms. Both runs preserve image/state. GPU and host measurements are separate;
host frame/submission scopes measure wall time, including any waits, not isolated
thread execution. Tail spikes remain (first D3D12 optimized host p99 35.922 ms).
The median improvement does not qualify sustained 60 FPS, rain/battle timing,
other hardware or an entire game. Vulkan remains the largest host-frame cost.

Nine CPU tests pass (four engine, five game), including the expanded glTF import/
draw contract. All 19 wet station, rain battle and source/day/oblique controls are
pixel-identical to the accepted HDR baseline. Same-build performance controls are
identical in GL/Vulkan; D3D12 differs by at most one byte in a few channels
(MAE 0.000000643/255). Six low-sun API/host captures pass at unchanged limits
(max MAE 0.68831/255); the D3D12 culling-off control is pixel-identical. Existing
station source/packet, battle, puddle/reflection and bloom oracles pass.

Game artifacts: `artifacts/lighting-foundation/shadow-culling/verification.json`,
`repeat/verification.json`, CPU/build/verification logs and native captures.
Commands: `Verify-Shadow-Culling.ps1 -Stage performance|views`, then
`verify_shadow_culling.py <root> --baseline artifacts/lighting-foundation/hdr/native`.
The reverse repeat uses `--apis d3d12`. The original compiler warning-as-error
failure is retained in `build-engine.log`; the local variable was renamed and
the second build passes. No image thresholds were relaxed.
