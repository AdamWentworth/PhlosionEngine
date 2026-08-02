# Editor startup and caching

Project opening and embedded game startup are separate lifecycle phases.

- `IEditorProjectRuntime::open()` mounts the editable scene.
- `prewarm()` prepares only the active Scene viewport.
- `initializeGamePreview()` is deferred until the user selects Game Preview or
  requests one on the command line.

The editor logs each project-load phase as
`[PhlosionEditor][ProjectLoad]` and the deferred gameplay cost as
`[PhlosionEditor][GamePreviewWarmup]`. Project plugins should add their own
phase logs around expensive source decoding and scene composition.

Engine caches are content- and implementation-keyed rather than merely
filename-keyed. OpenGL program binaries include GPU/driver and shader-source
identity; model, neutral-PMREM, and project caches must likewise invalidate
when their inputs or schema change. Cache writes belong under `cache/` and are
never authoritative project content.

Use this repeatable smoke benchmark from the engine repository:

```powershell
.\build\Debug\PhlosionEditor.exe `
  --project=D:\Projects\YourGame\phlosion.project.json `
  --renderer=d3d12 --frames=2
```

Run once without `--game-preview` to measure editing startup, then with
`--game-preview=<id>` to measure the explicitly requested gameplay warmup.
