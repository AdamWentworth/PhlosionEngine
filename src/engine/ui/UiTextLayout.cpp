#include "UiTextLayout.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace engine::ui {
namespace {

bool fail(std::string* error, const char* message) {
    if (error) *error = message;
    return false;
}

void succeed(std::string* error) {
    if (error) error->clear();
}

bool validId(std::string_view id) {
    if (id.empty() || id.size() > 96u) return false;
    for (const unsigned char c : id) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-')
            continue;
        return false;
    }
    return true;
}

bool decodeUtf8(std::string_view text, std::vector<char32_t>& out,
                std::string* error) {
    for (std::size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i++]);
        char32_t cp = 0;
        int remaining = 0;
        char32_t minimum = 0;
        if (lead < 0x80u) {
            cp = lead;
        } else if (lead >= 0xC2u && lead <= 0xDFu) {
            cp = lead & 0x1Fu;
            remaining = 1;
            minimum = 0x80u;
        } else if (lead >= 0xE0u && lead <= 0xEFu) {
            cp = lead & 0x0Fu;
            remaining = 2;
            minimum = 0x800u;
        } else if (lead >= 0xF0u && lead <= 0xF4u) {
            cp = lead & 0x07u;
            remaining = 3;
            minimum = 0x10000u;
        } else {
            return fail(error, "invalid UTF-8 lead byte");
        }
        if (i + static_cast<std::size_t>(remaining) > text.size())
            return fail(error, "truncated UTF-8 sequence");
        while (remaining-- > 0) {
            const auto byte = static_cast<unsigned char>(text[i++]);
            if ((byte & 0xC0u) != 0x80u)
                return fail(error, "invalid UTF-8 continuation byte");
            cp = (cp << 6u) | (byte & 0x3Fu);
        }
        if (cp < minimum || cp > 0x10FFFFu ||
            (cp >= 0xD800u && cp <= 0xDFFFu))
            return fail(error, "invalid UTF-8 scalar value");
        if (cp == '\r') {
            if (i == text.size() || text[i] != '\n')
                return fail(error, "bare carriage return in UI text");
            ++i;
            cp = '\n';
        }
        if (cp == 0x2028u || cp == 0x2029u) cp = '\n';
        if ((cp < 0x20u && cp != '\n') || cp == 0x7Fu)
            return fail(error, "control character in UI text");
        out.push_back(cp);
        if (out.size() > 4096u)
            return fail(error, "UI text exceeds the scalar limit");
    }
    return true;
}

bool needsShaping(char32_t cp) {
    return (cp >= 0x0300u && cp <= 0x036Fu) ||
           (cp >= 0x0590u && cp <= 0x08FFu) ||
           (cp >= 0x1AB0u && cp <= 0x1AFFu) ||
           (cp >= 0x1DC0u && cp <= 0x1DFFu) ||
           (cp >= 0x200Cu && cp <= 0x200Fu) ||
           (cp >= 0x202Au && cp <= 0x202Eu) ||
           (cp >= 0x2066u && cp <= 0x2069u) ||
           (cp >= 0x20D0u && cp <= 0x20FFu) ||
           (cp >= 0xFB1Du && cp <= 0xFE0Fu) ||
           (cp >= 0xFE20u && cp <= 0xFEFFu);
}

struct ResolvedGlyph {
    char32_t codepoint = 0;
    std::uint8_t fontIndex = 0;
    float advance = 0.0f;
};

} // namespace

bool UiStringTable::replaceAll(std::span<const UiStringEntry> entries,
                               std::string* error) {
    if (entries.size() > 4096u)
        return fail(error, "too many UI strings");
    std::unordered_map<std::string, std::string> candidate;
    candidate.reserve(entries.size());
    std::size_t totalBytes = 0;
    for (const UiStringEntry& entry : entries) {
        if (!validId(entry.id) || entry.text.size() > 16384u)
            return fail(error, "invalid UI string ID or length");
        totalBytes += entry.id.size() + entry.text.size();
        if (totalBytes > 4u * 1024u * 1024u)
            return fail(error, "UI string table exceeds the byte limit");
        std::vector<char32_t> decoded;
        if (!decodeUtf8(entry.text, decoded, error)) return false;
        if (!candidate.emplace(entry.id, entry.text).second)
            return fail(error, "duplicate UI string ID");
    }
    strings_.swap(candidate);
    succeed(error);
    return true;
}

bool UiStringTable::lookup(std::string_view id, std::string& out,
                           std::string* error) const {
    if (!validId(id)) return fail(error, "invalid UI string ID");
    const auto it = strings_.find(std::string(id));
    if (it == strings_.end()) return fail(error, "UI string ID is unavailable");
    out = it->second;
    succeed(error);
    return true;
}

bool layoutUiText(std::string_view text,
                  std::span<const UiFontMetrics* const> fonts,
                  float maxWidth, float scale, UiTextLayout& out,
                  std::string* error) {
    if (text.size() > 16384u || fonts.empty() || fonts.size() > 16u ||
        !std::isfinite(maxWidth) || maxWidth < 1.0f || maxWidth > 10000.0f ||
        !std::isfinite(scale) || scale < 0.5f || scale > 4.0f)
        return fail(error, "invalid UI text layout bounds");
    float lineHeight = 0.0f;
    for (const UiFontMetrics* font : fonts) {
        if (!font) return fail(error, "invalid UI font metrics");
        const float fontHeight = font->lineHeight();
        if (!std::isfinite(fontHeight) ||
            fontHeight <= 0.0f || fontHeight > 1000.0f)
            return fail(error, "invalid UI font metrics");
        lineHeight = (std::max)(lineHeight, fontHeight);
    }
    lineHeight *= scale;
    std::vector<char32_t> decoded;
    if (!decodeUtf8(text, decoded, error)) return false;
    std::vector<ResolvedGlyph> resolved;
    resolved.reserve(decoded.size());
    for (const char32_t cp : decoded) {
        if (cp == '\n') {
            resolved.push_back({cp});
            continue;
        }
        if (needsShaping(cp))
            return fail(error, "UI text requires shaping or bidi layout");
        bool found = false;
        for (std::size_t fontIndex = 0; fontIndex < fonts.size(); ++fontIndex) {
            float advance = 0.0f;
            if (!fonts[fontIndex]->glyphAdvance(cp, advance)) continue;
            if (!std::isfinite(advance) || advance < 0.0f || advance > 1000.0f)
                return fail(error, "invalid UI glyph advance");
            resolved.push_back({cp, static_cast<std::uint8_t>(fontIndex),
                                advance * scale});
            found = true;
            break;
        }
        if (!found) return fail(error, "UI font set lacks a required glyph");
    }

    UiTextLayout candidate;
    candidate.lineHeight = lineHeight;
    std::vector<ResolvedGlyph> line;
    std::vector<ResolvedGlyph> spaces;
    double lineWidth = 0.0;
    double spacesWidth = 0.0;
    const auto append = [&](const ResolvedGlyph& glyph) {
        line.push_back(glyph);
        lineWidth += glyph.advance;
    };
    const auto flush = [&] {
        const float topY = static_cast<float>(candidate.lines.size()) * lineHeight;
        const std::size_t first = candidate.glyphs.size();
        float x = 0.0f;
        for (const ResolvedGlyph& glyph : line) {
            candidate.glyphs.push_back({glyph.codepoint, glyph.fontIndex,
                                        x, topY, glyph.advance});
            x += glyph.advance;
        }
        candidate.lines.push_back({first, line.size(),
                                   static_cast<float>(lineWidth), topY});
        candidate.width = (std::max)(candidate.width,
                                     static_cast<float>(lineWidth));
        line.clear();
        lineWidth = 0.0;
    };
    constexpr double epsilon = 1.0e-4;
    for (std::size_t i = 0; i < resolved.size();) {
        if (resolved[i].codepoint == '\n') {
            flush();
            spaces.clear();
            spacesWidth = 0.0;
            ++i;
            continue;
        }
        if (resolved[i].codepoint == ' ') {
            if (!line.empty()) {
                spaces.push_back(resolved[i]);
                spacesWidth += resolved[i].advance;
            }
            ++i;
            continue;
        }
        std::size_t end = i;
        double wordWidth = 0.0;
        while (end < resolved.size() && resolved[end].codepoint != ' ' &&
               resolved[end].codepoint != '\n') {
            wordWidth += resolved[end].advance;
            ++end;
        }
        if (!line.empty() &&
            lineWidth + spacesWidth + wordWidth > maxWidth + epsilon)
            flush();
        if (!line.empty()) {
            for (const ResolvedGlyph& space : spaces) append(space);
        }
        spaces.clear();
        spacesWidth = 0.0;
        for (; i < end; ++i) {
            const ResolvedGlyph& glyph = resolved[i];
            if (glyph.advance > maxWidth + epsilon)
                return fail(error, "UI glyph is wider than the layout width");
            if (!line.empty() && lineWidth + glyph.advance > maxWidth + epsilon)
                flush();
            append(glyph);
        }
    }
    flush();
    candidate.height = static_cast<float>(candidate.lines.size()) * lineHeight;
    out = std::move(candidate);
    succeed(error);
    return true;
}

bool layoutUiString(const UiStringTable& table, std::string_view id,
                    std::span<const UiFontMetrics* const> fonts,
                    float maxWidth, float scale, UiTextLayout& out,
                    std::string* error) {
    std::string value;
    if (!table.lookup(id, value, error)) return false;
    return layoutUiText(value, fonts, maxWidth, scale, out, error);
}

} // namespace engine::ui
