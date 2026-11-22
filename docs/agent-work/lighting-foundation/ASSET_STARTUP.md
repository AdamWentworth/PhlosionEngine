# Prepared asset startup — 2026-11-03

This pass removes the temporary scalar document expansion from loading baked
indirect lighting. It does not change the rendering equations, effect budgets,
resource scheduling or when battle preparation finishes. No parity row is rescored.

## Format and ownership

The engine writes phlosion-indirect-transport-v2 and still reads v1. Small CBOR
metadata describes scene identity, grid and emission bindings; large collections
are binary little-endian records. There are no native struct dumps or padding.
Float32 bits, signed integers and visibility bytes are preserved. Record widths
are terrain 32, surfel 61, brick 20, probe 88, factor 8, tetrahedron 132 and
acceleration 4 bytes. Length/type/count checks precede decoded-array allocation;
the existing file budget and full semantic validation still apply.

PhlosionIndirectBake --repack input.gi output.gi converts an existing bake to a
new file and checks the GPU rows bit-for-bit. The real station's 22,806,304-byte
GPU payload is unchanged. Repeating the v2 conversion produces identical bytes.
The station file shrinks from 38,890,346 to 32,806,093 bytes. The original bake
identity remains recorded separately from the converter identity.

PokemonCyberpunk owns the new outskirt-indirect-v1/2026-11-03-packed publication,
scene extent and authored lighting. Its normal station preparation verifies the
pinned data. The editor launcher no longer builds or runs the baker for normal
launches; explicit -StationSource authoring retains preparation. Shader packages
and ModelSupport are unchanged.

## Measurements

gpu-worker Windows, EVGA RTX 2060 SUPER, optimized RelWithDebInfo, ordinary
1920x1080 Medium heavy rain, same binary and shader caches, 120 frames. Existing
file/application/driver caches were retained; these are repeated development
launches, not factory-cold installation timings. First-frame timestamps exclude
the harness's cooldown, capture and shutdown waits.

| Measurement | v1 | v2 |
| --- | ---: | ---: |
| Standalone station transport read | 2.701 s | 0.264 s |
| D3D12 first completed frame | 28.447 s | 23.409 s |
| Vulkan first completed frame | 28.634 s | 24.092 s |

Both field and battle load the same station transport, so the decode reduction
saves roughly 4.5–5 seconds across startup. This does not defer work into the first
battle and is not a steady-frame performance improvement.

## Verification and evidence

CPU tests cover v1 compatibility (including absent optional old fields), every
nonempty v2 block, little-endian signed/boolean layout, exact GPU payload,
truncation, wrong block types, unsupported schemas, noncanonical flags and
nonfinite decoded values. Prepared package restoration, verification, normal
station preparation and deterministic conversion are checked on the worker.

Both APIs passed validation-enabled editor battle Ultra and dry Low scene return,
plus strict prepared-shader Medium wet runtime startup. Vulkan's v1/v2 image
difference is 0.0065/255 mean, maximum 1. D3D12 is not pixel deterministic in
these repeated launches: the same v2 file differs by 5.022/255 mean between runs,
while the original v2 and repeated v1 pair are much closer (see control report).
World-environment input sequences match. The initial D3D12 image gate failed
and is retained, not silently reclassified as exact visual equality. Exact
transport bytes and unchanged shader identities establish the data-preservation
claim; the cause of D3D12 run-to-run image variation remains unaudited.

Prepared-station scripting passed in the restored flat task layout. The worker's
older checkout has an unrelated rain junction outside the checkout; the package
tool correctly rejects that layout. Containment checks were not weakened.

The task evidence retains startup traces, exact revisions/binaries/package hashes,
API validation, image comparisons and rejected attempts. Raw private payloads,
binaries and deployments remain outside Git. Evidence is at
artifacts/lighting-foundation/asset-startup-20261103/evidence.zip.

## Remaining startup costs

The finer game diagnostics identify about 7.6 seconds in encounter move loading,
including a separate audio scan of already loaded visual documents. Battle GPU
preparation is about 5.4 seconds, including roughly 2.7 seconds preparing the field.
Immutable scene reuse, move audio metadata reuse and upload/mipmap preparation are
separate next targets. Do not move these costs into gameplay or remove content.

The 2026-11-05 follow-up [asset lifetime completion](ASSET_LIFETIME_COMPLETION.md)
resolves duplicate scene/transport preparation, move-audio scanning and compatible
native uploads. Its measured startup and resident payload supersede the remaining
cost observations above; factory-cold shader/driver work stays separate.
