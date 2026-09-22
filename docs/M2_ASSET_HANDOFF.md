# M2 handoff when game assets arrive

The public exploration course is the integration/verification vehicle while
character and environment assets are unavailable. Its current status is in the
[readiness roadmap](CYBERPUNK_ENGINE_READINESS.md) and
[M2-F checkpoint](agent-work/cyberpunk-m2-exploration/CHECKPOINT.md). Passing that
course qualifies the tested engine path, not the eventual Outskirt Stand scene.

## Character fitting

Use the existing [supported glTF scene path](GLTF_SCENE_ASSETS.md) and its
material/animation limits. M2 needs a visible character placed correctly against
the collision controller; animation-runtime expansion belongs to M3.

The [character API](CHARACTER_CONTROLLER.md) reports the geometric center of an
upright Y-axis capsule. Its feet are at
`center.y - radius - cylinderHalfHeight`. At default settings its total height
is 1.8 world units. Establish the asset's scale, root/feet origin and forward
direction before choosing the instance transform. Keep this visual fitting
separate from movement intent and collision shape settings.

Check standing height, floor contact, wall clearance, stairs, ramps and ledges
with the real mesh. Fit the camera pivot offset to the character's framing;
review probe radius, pitch/distance limits and field of view against its visible
silhouette. A placeholder's successful framing cannot certify the final mesh.

## Environment and gameplay placement

The game repository owns the scene adapter and authored data. Establish a common
coordinate system/world-unit scale for visible geometry, static collision,
spawns, sensors, camera targets and NPC waypoints. Collision descriptions accept
rigid transforms; bake scale into proxy geometry or extents explicitly.

Supply and review:

| Data | Asset-specific check |
| --- | --- |
| Static collision proxies/meshes | Visible floors/walls/slopes/steps agree with contacts; avoid invisible blockers and gaps |
| Player spawn and recovery point | Capsule starts clear, supported and within the numeric envelope |
| Interaction/transition sensors | Zone size and sample cadence catch intended movement; complete crossings between samples can be missed |
| Camera framing and any scene zones | Player, paths and nearby obstacles stay readable; review close-up/occluded views |
| NPC routes where needed | Waypoints are traversable; blocked segments stop/retry safely and visibly |

The camera's sphere probe tests a final radial segment. It does not establish
continuous camera-path or whole-frustum clearance. Review actual scene corners,
ceilings, narrow passages and transitions with that limitation in mind.

## Final scene qualification

Repeat the tested host scenarios with the actual assets: walk/run, walls, slopes,
stairs/ledges, sensor events, camera obstruction/recovery, reset, controller
selection/hotplug, focus loss/resume and editor play/pause/stop. Capture matching
simulation states on native OpenGL, Vulkan and Direct3D 12 at 16:9 and 21:9 in
the runtime and editor, using expected scene/player content checks as well as
cross-API comparisons. Preserve exact asset and binary provenance.

Record physical-controller observations separately from SDL virtual-device and
scripted host evidence. Only the actual game scene passing these checks closes
the M2 Walkable Outskirt Stand exit gate. Keep any remaining asset, hardware or
host limitation explicit in the roadmap rather than inheriting a public-course
PASS for content that was never tested.
