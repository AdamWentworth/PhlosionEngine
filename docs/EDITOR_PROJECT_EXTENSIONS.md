# Editor Project Extension Boundary

Status: Active
Last updated: 2026-08-02

## Rule

Phlosion Editor is one reusable host. Opening a project may add tools, but the
host must never branch on a game's title, asset IDs, object kinds, or rules.

## PhlosionEngine owns

- the native window, docking shell, renderer selection, and offscreen Scene,
  Game, and asset-preview surfaces;
- project loading and the versioned editor-plugin ABI;
- generic Scenes, Hierarchy, Assets, Console, Inspector, and Game Preview
  panels;
- generic object selection, multi-selection, transform gizmos, live preview,
  commit/cancel, and undo/redo routing;
- a reusable grid-terrain widget: projected cell selection, coordinates,
  levels, copy/paste stamps, prefab cards, and platform-footprint controls;
- a declarative project-command renderer with typed boolean/float options and
  confirmation for destructive operations.

These facilities do not know what a Pokemon, Autochess board, Route, lawn,
dirt path, bench, or encounter-grass record is.

## A game project owns

- scene and game-preview catalogs and all runtime-state transition semantics;
- game-specific hierarchy categories and layout-object `targetKind` values;
- each layout object's transform capabilities, Scene/Game viewport visibility,
  snapping, Inspector copy, and reset language;
- terrain surfaces, prefab combinations, swatches, variants, and the meaning
  of terrain edit operation IDs;
- specialized project commands and their execution, persistence, and status;
- decoding and rendering project-specific prefab and VFX previews;
- project-authored scene files, runtime preview overrides, and their history.

`targetKind` is opaque metadata. Phlosion may display or return it, but the
shell must not compare it to a game-defined string to decide behavior.

## Capability model

`EditorProjectLayoutObject` declares allowed translate, rotate, scale, rename,
reparent, duplicate, delete, suppress, and reset actions. It also declares
which viewport may edit it and optional per-axis translation snapping. This
keeps generic transform code in Phlosion while policy remains with the game.

`EditorProjectCommand` describes a project-owned action. Phlosion renders its
name, explanation, fields, confirmation, and button; the plugin interprets the
command ID and performs the transaction. No command-specific request or result
type belongs in the Engine ABI.

## Review gate

An Engine editor change fails the repository boundary review if it adds:

- a game title or title-specific source-format name;
- a comparison against a game-defined `targetKind`;
- a game-specific asset ID, terrain surface, command, or save path;
- game rules encoded in a generic widget.

Game-specific examples may appear in documentation and tests, but executable
Engine code must remain project-neutral.
