# M3 battle camera foundation

`Phlosion::Camera` now includes `BattleCamera.h`, a renderer-neutral camera rig
for authored battle shots. A shot specifies yaw, pitch, focus offset, vertical
field of view, framing margin, distance limits and clip planes. Each frame
supplies one to eight target spheres. The rig centers their world-space bounds
and chooses the shortest camera distance that fits every sphere in the
perspective frustum, accounting for aspect ratio, near and far planes. It
reports an impossible shot rather than silently cropping a target.

`cut` immediately publishes a new shot. `blendTo` starts an eased transition
from the current unshaken pose; `step` follows the current target positions and
advances the blend. Yaw takes the shortest angular path and distance remains
positive even for opposite-side shots. Starting another blend uses the current
pose as its source, while a cut discards the prior blend. `reset` clears the
camera for battle exit and makes `pose` unavailable until the next cut.

Shake is a deterministic, bounded translation of the camera's right and up
axes. It moves the eye and look target together, so orientation is unchanged.
The offset is clamped to the current targets' frustum slack; a tightly framed
shot may reduce shake rather than crop a target.
`stopShake` restores the unshaken pose immediately; cuts, blend starts and
reset clear shake. A reduced-motion frame suppresses and cancels the effect,
including when a later frame restores normal motion. The caller chooses when
to trigger an impact shake and how it relates to animation events.

The rig owns no scene objects or renderer resources. It belongs to its
constructing thread, following the existing exploration camera convention.
Invalid shots, targets, frame times or impossible framing leave the rig and
caller output unchanged. Numeric bounds are checked in the implementation;
target positions and published eye/focus must stay within 10,000 world units,
the aspect range is 0.25–8, and frame delta is 0–0.25 seconds.

## Verification and limits

`PhlosionBattleCameraContracts` uses independent perspective projection to
check analytic framing at wide and narrow aspects, sphere-radius padding,
distance/clip rejection, target tracking, eased and interrupted blends,
opposite-side shots, bounded deterministic shake, reduced motion, reset,
transactional failures and thread ownership. The API is CPU-only in this
pass. No runtime or editor presentation was changed, so there is no new native
camera image claim. The M3 battle fixture and actual roster/environment shots
must still run on OpenGL, Vulkan and Direct3D 12 before the double-battle gate
can close.
