# M3 runtime UI text foundation

`Phlosion::UI` now includes `UiTextLayout.h`, a renderer-free text contract.
`UiStringTable::replaceAll` atomically swaps a project-supplied set of stable
ASCII IDs and UTF-8 values. It rejects duplicate/invalid IDs, malformed UTF-8,
control characters and oversized tables while retaining the last accepted
table. Projects choose the language and supply the strings; the engine does not
choose a locale, translate, pluralize or format dialogue.

`layoutUiText` and `layoutUiString` accept one to sixteen `UiFontMetrics`
providers in fallback order. Each provider reports its line height and whether
it supplies a codepoint's advance. Layout rejects a missing glyph or invalid
metric. Its output contains ordered glyph codepoints, chosen font indices,
scaled positions and advances, plus line ranges and bounds. This lets a later
renderer use the same measured positions on OpenGL, Vulkan and Direct3D 12.

Layout recognizes explicit LF, CRLF and Unicode line/paragraph separators.
It wraps at ASCII spaces, omits leading/trailing spaces on each line and breaks an
overlong word at codepoint boundaries. Width, scale, text size, scalar count,
font count and table bytes are bounded. Empty text produces one empty line;
failed layout leaves the caller's prior result unchanged.

The current contract is for simple left-to-right text. It rejects common
combining-mark, bidirectional and joining ranges rather than placing those
glyphs incorrectly. It does not shape scripts, segment grapheme clusters,
apply localized line-break rules, kern glyph pairs or rasterize text. The
existing `TextRenderer` is still OpenGL-only and byte-oriented; it does not
consume these placements. Actual font files and coverage, subtitle controls,
drawn text and HUD/dialogue images in the runtime and editor remain separate
M3 qualification work on all three APIs.

`PhlosionUiTextContracts` uses synthetic font metrics to check strict UTF-8,
transactional string-table replacement, fallback selection, analytic wraps,
scaled placement, explicit line breaks, unsupported text and failure
preservation. Its font-selection result is an algorithmic check, not evidence
that any shipped font contains a particular glyph.
