#include "engine/ui/UiTextLayout.h"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

using namespace engine::ui;

namespace {

#define CHECK(condition) do { if (!(condition)) throw std::runtime_error( \
    std::string(__func__) + ":" + std::to_string(__LINE__) + " " #condition); } while (false)

bool near(float a, float b) { return std::abs(a - b) < 1.0e-4f; }

class PrimaryFont final : public UiFontMetrics {
public:
    float lineHeight() const override { return 20.0f; }
    bool glyphAdvance(char32_t cp, float& out) const override {
        if (cp < 0x20 || cp > 0x7E) return false;
        out = cp == ' ' ? 5.0f : 10.0f;
        return true;
    }
};

class AccentFont final : public UiFontMetrics {
public:
    float lineHeight() const override { return 22.0f; }
    bool glyphAdvance(char32_t cp, float& out) const override {
        if (cp == U'\u00E9') {
            out = 12.0f;
            return true;
        }
        if (cp == 0x1F642u) {
            out = 14.0f;
            return true;
        }
        return false;
    }
};

class InvalidFont final : public UiFontMetrics {
public:
    float lineHeight() const override { return 20.0f; }
    bool glyphAdvance(char32_t, float& out) const override {
        out = (std::numeric_limits<float>::quiet_NaN)();
        return true;
    }
};

void checkStringTableReplacement() {
    UiStringTable table;
    const std::array<UiStringEntry, 2> english{{
        {"dialogue.rider", "Welcome, rider."},
        {"battle.prompt", "Choose an action"}}};
    std::string error;
    CHECK(table.replaceAll(english, &error));
    CHECK(error.empty());
    std::string value = "unchanged";
    CHECK(table.lookup("dialogue.rider", value, &error));
    CHECK(value == "Welcome, rider.");
    const std::array<UiStringEntry, 2> duplicate{{
        {"battle.prompt", "One"}, {"battle.prompt", "Two"}}};
    CHECK(!table.replaceAll(duplicate, &error));
    CHECK(table.lookup("battle.prompt", value, &error));
    CHECK(value == "Choose an action");
    const std::array<UiStringEntry, 1> badUtf8{{
        {"battle.prompt", std::string("\xC0\xAF", 2)}}};
    CHECK(!table.replaceAll(badUtf8, &error));
    CHECK(table.lookup("battle.prompt", value, &error));
    CHECK(value == "Choose an action");
    const std::array<UiStringEntry, 1> badId{{
        {"battle prompt", "Text"}}};
    CHECK(!table.replaceAll(badId, &error));
    const std::array<UiStringEntry, 1> localized{{
        {"battle.prompt", "caf\xC3\xA9"}}};
    CHECK(table.replaceAll(localized, &error));
    CHECK(table.lookup("battle.prompt", value, &error));
    CHECK(value == "caf\xC3\xA9");
    value = "sentinel";
    CHECK(!table.lookup("dialogue.rider", value, &error));
    CHECK(value == "sentinel" && !error.empty());
    CHECK(!table.lookup("bad id", value, &error));
    CHECK(value == "sentinel");
}

void checkWrappingScalingAndFallback() {
    PrimaryFont primary;
    AccentFont accent;
    const UiFontMetrics* fonts[] = {&primary, &accent};
    UiTextLayout layout;
    std::string error;
    CHECK(layoutUiText("ab caf\xC3\xA9 done", fonts, 70.0f,
                       1.0f, layout, &error));
    CHECK(error.empty());
    CHECK(layout.lines.size() == 2 && layout.glyphs.size() == 11);
    CHECK(near(layout.lines[0].width, 67.0f));
    CHECK(near(layout.lines[1].width, 40.0f));
    CHECK(near(layout.width, 67.0f) && near(layout.height, 44.0f));
    CHECK(layout.glyphs[6].codepoint == U'\u00E9');
    CHECK(layout.glyphs[6].fontIndex == 1u);
    CHECK(near(layout.glyphs[6].x, 55.0f));
    CHECK(near(layout.glyphs[7].x, 0.0f));
    CHECK(near(layout.glyphs[7].y, 22.0f));
    CHECK(layoutUiText("ab caf\xC3\xA9 done", fonts, 140.0f,
                       2.0f, layout, &error));
    CHECK(layout.lines.size() == 2);
    CHECK(near(layout.lines[0].width, 134.0f));
    CHECK(near(layout.height, 88.0f));
    CHECK(near(layout.glyphs[6].x, 110.0f));
    CHECK(layoutUiText("abcdef", fonts, 25.0f, 1.0f, layout, &error));
    CHECK(layout.lines.size() == 3 && layout.glyphs.size() == 6);
    for (const UiTextLine& line : layout.lines)
        CHECK(near(line.width, 20.0f) && line.glyphCount == 2);
    CHECK(layoutUiText("a\n\nb", fonts, 70.0f, 1.0f, layout, &error));
    CHECK(layout.lines.size() == 3);
    CHECK(near(layout.lines[1].width, 0.0f));
    CHECK(near(layout.height, 66.0f));
    CHECK(layoutUiText("a\r\nb", fonts, 70.0f, 1.0f, layout, &error));
    CHECK(layout.lines.size() == 2);
    CHECK(layoutUiText("  a  ", fonts, 70.0f, 1.0f, layout, &error));
    CHECK(layout.glyphs.size() == 1 && near(layout.width, 10.0f));
    CHECK(layoutUiText("A \xF0\x9F\x99\x82", fonts, 70.0f,
                       1.0f, layout, &error));
    CHECK(layout.glyphs.size() == 3);
    CHECK(layout.glyphs[2].codepoint == 0x1F642u);
    CHECK(layout.glyphs[2].fontIndex == 1u);
    CHECK(layoutUiText("", fonts, 70.0f, 1.0f, layout, &error));
    CHECK(layout.lines.size() == 1 && layout.glyphs.empty());
    CHECK(near(layout.height, 22.0f));
}

void checkFailurePreservesOutput() {
    PrimaryFont primary;
    AccentFont accent;
    const UiFontMetrics* fonts[] = {&primary, &accent};
    UiTextLayout layout;
    std::string error;
    CHECK(layoutUiText("accepted", fonts, 100.0f, 1.0f, layout, &error));
    const UiTextLayout prior = layout;
    const auto preserved = [&] {
        CHECK(layout.glyphs.size() == prior.glyphs.size());
        CHECK(layout.lines.size() == prior.lines.size());
        CHECK(near(layout.width, prior.width));
        CHECK(layout.glyphs[0].codepoint == prior.glyphs[0].codepoint);
    };
    CHECK(!layoutUiText("\xED\xA0\x80", fonts, 100.0f,
                        1.0f, layout, &error));
    preserved();
    CHECK(!layoutUiText("\xF4\x90\x80\x80", fonts, 100.0f,
                        1.0f, layout, &error));
    preserved();
    CHECK(!layoutUiText("e\xCC\x81", fonts, 100.0f,
                        1.0f, layout, &error));
    preserved();
    CHECK(!layoutUiText("\xE2\x98\x83", fonts, 100.0f,
                        1.0f, layout, &error));
    preserved();
    CHECK(!layoutUiText("a", fonts, 5.0f, 1.0f, layout, &error));
    preserved();
    CHECK(!layoutUiText("a", fonts, 100.0f, 0.0f, layout, &error));
    preserved();
    InvalidFont invalid;
    const UiFontMetrics* badFonts[] = {&invalid};
    CHECK(!layoutUiText("a", badFonts, 100.0f, 1.0f, layout, &error));
    preserved();
    UiStringTable table;
    CHECK(!layoutUiString(table, "missing", fonts, 100.0f,
                          1.0f, layout, &error));
    preserved();
    const std::array<UiStringEntry, 1> localized{{
        {"dialogue.line", "caf\xC3\xA9"}}};
    CHECK(table.replaceAll(localized, &error));
    CHECK(layoutUiString(table, "dialogue.line", fonts, 100.0f,
                         1.0f, layout, &error));
    CHECK(layout.glyphs.size() == 4);
    CHECK(layout.glyphs[3].fontIndex == 1u);
}

} // namespace

int main() {
    try {
        checkStringTableReplacement();
        checkWrappingScalingAndFallback();
        checkFailurePreservesOutput();
        std::cout << "[PASS] UI text contracts\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "[FAIL] UI text contracts: " << exception.what() << '\n';
        return 1;
    }
}
