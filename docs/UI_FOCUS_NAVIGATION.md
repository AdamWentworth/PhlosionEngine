# M3 runtime UI focus foundation

`Phlosion::UI` is a renderer-free library for focus and action routing on one
menu or dialogue surface. `UiFocusNavigator` accepts the currently visible
`UiFocusItem` rectangles and stable IDs each frame. It preserves focus while an
item remains enabled, picks the first enabled item when focus disappears, skips
disabled items and chooses directional neighbors from rectangle centers. The
caller owns menu content, drawing, activation semantics and lifetime. IDs
identify controls across frames and should stay independent of localized text.

`routeActions` reads three project-defined `InputActions` names: `ui.move`
(normally Axis2D, X right and Y down), `ui.accept` and `ui.cancel` (normally
buttons). Keyboard, mouse and gamepad bindings are defined by the project.
Call it after `InputActions::advanceFrame()`. The direct `route` overload takes
the same semantic controls when another input source is appropriate. Cancel
wins over accept if both arrive in one frame. `setFocus` synchronizes an
external pointer selection; `reset` clears state on screen teardown. A new or
reset navigator also waits for neutral input before accepting controls.

An active surface returns `blocksGameplay=true` even when it has no focusable
item. The host must use that result to keep its gameplay action route from
acting on the same input; `InputActions` does not consume actions globally.
Direction changes move immediately. A held direction repeats after 0.35 seconds
and every 0.12 seconds thereafter. Deactivating the surface clears that hold;
on reactivation, direction and accept/cancel must return to neutral before new
UI actions are accepted. This prevents a held gamepad control from firing as a
screen regains focus. Pass `active=false` when the menu closes or its window or
viewport loses input focus.

The API validates duplicate/empty IDs, nonpositive rectangles, item count,
frame delta and direction before changing focus or caller output. Instances
belong to their constructing thread. `PhlosionUiFocusContracts` covers spatial
selection, disabled/dynamic items, keyboard and synthetic-controller action
composition, repeat timing, activation/cancel precedence, modal capture,
rearming, invalid frames and thread ownership.

The separate [M3-E text module](UI_TEXT_LAYOUT.md) supplies CPU layout and
fallback selection. The existing `TextRenderer` still uses an OpenGL-only,
byte-oriented glyph path, and `UIManager::drawCard` remains a stub. Actual font
coverage, subtitle settings, native HUD presentation on all three APIs and
game-specific dialogue and battle screens remain M3 work.
