# Effect host lifetime

`vfx::runtime::EffectHost` is a renderer-neutral owner-thread controller for
game-selected effects. The game supplies a `HostedEffect` adapter, a spawn
transform, and a bounded lifetime. The host returns a stable ID for explicit
stop, or the game may attach the effect to an anchor ID with a local transform.
Each update supplies the current anchor transforms. A missing anchor ends its
effect; `detach` preserves the last world transform. Expiry, explicit stop, and
`stopAll` each call the effect's `stop` method once before releasing it.

The host checks frame values and anchor identity before mutating active
effects. It does not choose attacks, resolve game actors or sockets, render
batches, or own project assets. Callbacks must not throw. Games should call
`stopAll` on battle cancellation, skip, exit, and scene teardown.

`PhlosionEffectHost` covers spawn, moving attachments, detach,
expiry, orphan cleanup, cancellation, invalid frames, stable IDs and thread
ownership. Native effect drawing and game event binding remain separate gates.
