#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine::ui {

struct UiStringEntry {
    std::string id;
    std::string text;
};

// The project supplies one complete table per language. Replacement is atomic;
// IDs are stable ASCII keys, while values are validated UTF-8 text.
class UiStringTable {
public:
    bool replaceAll(std::span<const UiStringEntry> entries,
                    std::string* error = nullptr);
    bool lookup(std::string_view id, std::string& out,
                std::string* error = nullptr) const;

private:
    std::unordered_map<std::string, std::string> strings_;
};

// A font adapter reports glyph coverage and unscaled advance. The first font
// that provides a codepoint wins. Shaping and rasterization are separate work.
class UiFontMetrics {
public:
    virtual ~UiFontMetrics() = default;
    virtual float lineHeight() const = 0;
    virtual bool glyphAdvance(char32_t codepoint, float& out) const = 0;
};

struct UiTextGlyph {
    char32_t codepoint = 0;
    std::uint8_t fontIndex = 0;
    float x = 0.0f;
    float y = 0.0f;
    float advance = 0.0f;
};

struct UiTextLine {
    std::size_t firstGlyph = 0;
    std::size_t glyphCount = 0;
    float width = 0.0f;
    float topY = 0.0f;
};

struct UiTextLayout {
    std::vector<UiTextGlyph> glyphs;
    std::vector<UiTextLine> lines;
    float width = 0.0f;
    float height = 0.0f;
    float lineHeight = 0.0f;
};

bool layoutUiText(std::string_view text,
                  std::span<const UiFontMetrics* const> fonts,
                  float maxWidth, float scale, UiTextLayout& out,
                  std::string* error = nullptr);

bool layoutUiString(const UiStringTable& table, std::string_view id,
                    std::span<const UiFontMetrics* const> fonts,
                    float maxWidth, float scale, UiTextLayout& out,
                    std::string* error = nullptr);

} // namespace engine::ui
