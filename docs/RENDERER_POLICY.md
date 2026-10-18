# Project renderer enrollment

As of 2026-10-18, Vulkan and Windows D3D12 are the active engine backends.
OpenGL source and shaders remain available as a shelved legacy backend. Normal
builds use `PHLOSION_ENABLE_OPENGL=OFF`; opt in with `-DPHLOSION_ENABLE_OPENGL=ON`
in a separate build directory when legacy work is specifically needed.
Legacy GL utility code and GLAD remain dependencies; this is not a full removal
of every GL symbol or asset format.

Projects enroll renderers in `phlosion.project.json` (schema 2):

```json
"rendering": {
  "windows": ["d3d12", "vulkan"],
  "linux": ["vulkan"]
}
```

Lists are ordered: Auto prefers the first available entry. A platform omitted
from the policy is unsupported. Unknown names, duplicate entries, empty lists
and D3D12 on non-Windows platforms are rejected. Eligibility is the intersection
of project enrollment, host platform and engine build support. An engine built
with OpenGL enabled does not enroll it in a game that excludes it.

The editor reads this policy before creating its window. Preferences show only
eligible renderers. A retired saved preference migrates to Auto; an explicit
unsupported `--renderer` request fails. Auto may try the remaining enrolled
backends after initialization fails; an explicit API request never falls back.
Opening a project incompatible with an already running editor reports that a
restart with the project's renderer is required, before loading its plugin.

Standalone games embed the same JSON policy at configure time through
`cmake/ProjectRenderers.cmake`, so packaging does not depend on a source checkout
path. Changing enrollment requires reconfiguring/rebuilding the runtime.
Old descriptors without a rendering section retain engine defaults for fixture
and tool compatibility. New games should declare their policy explicitly.

PokemonCyberpunk prefers D3D12 on Windows, with Vulkan also enrolled.
PokemonAutochess prefers Vulkan on Windows, with D3D12 also enrolled. Both declare
Vulkan for Linux and omit macOS for now. This declaration is a target policy,
not evidence of native Linux or macOS qualification.

GPU acceptance applies to each project's enrolled APIs on the qualified host,
including runtime and editor where affected. Expected-content guards, matched
frames and image thresholds remain required. OpenGL parity is no longer a
release gate for these games. Archived three-API reports remain historical
evidence. The engine's legacy build should retain its focused opt-in checks;
routine rendering work need not extend or qualify every new feature on OpenGL.

## Windows qualification, 2026-10-18

RelWithDebInfo builds passed with OpenGL disabled in the ordinary engine/editor
and both game builds. The engine's 150 registered tests passed: 146 in the
initial run and four fixture harnesses on retry with Windows PowerShell's module
path restored in the child process. Those failures were `Get-FileHash` discovery,
not rendering failures. Final policy/core contracts and native Vulkan/D3D12
finite-light/shadow-moment checks also passed after the final edits.

A separate `build-opengl-legacy` with OpenGL enabled built the legacy backend;
renderer-policy and native OpenGL local-haze tests passed (2/2). The modern
planar-reflection image oracle passed without requiring OpenGL captures.

Raw build/test logs are local under `artifacts/renderer-policy`. Consumer
runtime/editor qualification is recorded in each game's test plan. This pass
does not qualify Linux, macOS, or a new performance baseline.
