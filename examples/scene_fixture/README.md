# Scene fixture

One public animated block character in a small authored environment, presented
through the engine's CPU-owned glTF scene path.

The sample owns content, camera and lifecycle choices. Everything else --
import, palette composition and draw binding -- comes from
`src/engine/render/gltf/GltfSceneAsset*.{h,cpp}`; this directory writes the
authored glTF document and its PNG maps into a run-owned staging directory,
loads them with `loadGltfSceneAsset` and submits them with
`drawGltfSceneAsset`.

Components:

- `SceneFixtureCase.h/.cpp` -- the six authored cases, the camera and the
  projection helpers.
- `SceneFixtureAssets.h/.cpp` -- authored content tables, the analytic rig, the
  staging directory and the glTF/PNG authoring.
- `SceneFixturePresentation.h/.cpp` -- the shared session lifecycle, drawing and
  handshake.
- `SceneFixtureRuntimeMain.cpp` -- standalone hidden-window host.
- `SceneFixtureEditorPlugin.cpp` -- real editor project plugin.

The runtime and editor hosts render the same session with the same camera; the
capture runner sizes the runtime viewport from the editor viewport. Verification
lives in `tools/scene_fixture`, `tests/TestSceneFixture.cpp` and
`tests/scene_fixture`. See [the scene fixture guide](../../docs/SCENE_FIXTURE.md).

The authored content is a small fixture, not a claim of general glTF support.
