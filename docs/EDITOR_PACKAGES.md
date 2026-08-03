# Optional editor packages

Phlosion editor packages are reusable features that are too specialized for
the universal Engine shell and too general for one game. First-party packages
share the sibling `PhlosionPackages` monorepo; a package is not a nested Git
repository.

`phlosion.project.json` opts in explicitly:

```json
"editor_packages": [
  {
    "id": "phlosion.tile-tools",
    "version": "0.1.0",
    "library": "PhlosionTileTools",
    "directory": ".phlosion/packages/{config}/phlosion.tile-tools"
  }
]
```

The directory is generated and ignored by the game. Building the game's
editor dependencies places the DLL there. On project open, Phlosion validates
the package ABI, id, and exact version before activation. Missing required
packages fail with a build instruction instead of silently exposing a partial
tool.

Packages may contribute to three controlled presentation points:

- the active viewport toolbar;
- the Scene viewport overlay and pointer interaction;
- the Inspector.

They receive only the generic workspace views and action requests declared by
the Engine contract. The game plugin still owns asset catalogs, semantics,
transactions, persistence, and undo history.
