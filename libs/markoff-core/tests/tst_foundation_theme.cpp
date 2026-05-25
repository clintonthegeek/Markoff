// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QColor>
#include <QTextCharFormat>

#include <markoff/core/Theme.h>
#include <markoff/core/CodeTokenKind.h>

using namespace Markoff;

class TstFoundationTheme : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void color_set_round_trips() {
        Theme t;
        t.setColor(Theme::Slot::Heading1, QColor(Qt::red));
        QCOMPARE(t.color(Theme::Slot::Heading1).name(), QColor(Qt::red).name());
    }

    void unset_color_falls_back_to_text_default() {
        Theme t;
        t.setColor(Theme::Slot::TextDefault, QColor("#101010"));
        // An unset slot returns a sentinel-equivalent (TextDefault) color
        // rather than an invalid QColor. View code can rely on this.
        QVERIFY(t.color(Theme::Slot::Heading6).isValid());
    }

    void font_set_round_trips() {
        Theme t;
        QFont f("Monaco", 14);
        t.setFont(Theme::FontRole::Monospace, f);
        QCOMPARE(t.font(Theme::FontRole::Monospace).family(), QStringLiteral("Monaco"));
    }

    void bold_italic_size_round_trip() {
        Theme t;
        t.setBold(Theme::Slot::Heading1, true);
        t.setItalic(Theme::Slot::Quote, true);
        t.setFontSizeMultiplier(Theme::Slot::Heading1, 2.0);
        QVERIFY(t.isBold(Theme::Slot::Heading1));
        QVERIFY(t.isItalic(Theme::Slot::Quote));
        QCOMPARE(t.fontSizeMultiplier(Theme::Slot::Heading1), 2.0);
        // Defaults.
        QVERIFY(!t.isBold(Theme::Slot::TextDefault));
        QCOMPARE(t.fontSizeMultiplier(Theme::Slot::TextDefault), 1.0);
    }

    void default_light_has_dark_text_on_light_background() {
        const Theme t = Theme::defaultLight();
        const QColor bg = t.color(Theme::Slot::EditorBackground);
        const QColor fg = t.color(Theme::Slot::TextDefault);
        QVERIFY(bg.lightness() > fg.lightness());
    }

    void default_dark_has_light_text_on_dark_background() {
        const Theme t = Theme::defaultDark();
        const QColor bg = t.color(Theme::Slot::EditorBackground);
        const QColor fg = t.color(Theme::Slot::TextDefault);
        QVERIFY(bg.lightness() < fg.lightness());
    }

    void colorForCodeToken_maps_to_code_slots() {
        Theme t;
        t.setColor(Theme::Slot::CodeKeyword, QColor("#ff0000"));
        t.setColor(Theme::Slot::CodeString,  QColor("#00ff00"));
        QCOMPARE(t.colorForCodeToken(CodeTokenKind::Keyword).name(),
                 QColor("#ff0000").name());
        QCOMPARE(t.colorForCodeToken(CodeTokenKind::String).name(),
                 QColor("#00ff00").name());
    }

    void hiddenMarker_charFormat_has_negative_letter_spacing() {
        Theme theme;
        const QTextCharFormat fmt = theme.charFormat(Theme::Slot::HiddenMarker);
        QFont f = fmt.font();
        QCOMPARE(f.letterSpacingType(), QFont::AbsoluteSpacing);
        QVERIFY(f.letterSpacing() < 0.0);
    }

    void theme_json_roundtrip() {
        Theme a = Theme::defaultLight();
        a.setColor(Theme::Slot::Heading1, QColor(0xff, 0x00, 0x00));
        a.setBold(Theme::Slot::Heading1, true);
        a.setFontSizeMultiplier(Theme::Slot::Heading1, 1.5);

        const QJsonObject json = a.toJson();
        const Theme b = Theme::fromJson(json);
        QCOMPARE(b.color(Theme::Slot::Heading1).name(),
                 a.color(Theme::Slot::Heading1).name());
        QVERIFY(b.isBold(Theme::Slot::Heading1));
        QCOMPARE(b.fontSizeMultiplier(Theme::Slot::Heading1), 1.5);
    }

    // --- Task 1A: pixelSizeFor ---

    void pixel_size_for_text_default_uses_body_role() {
        Theme t;
        t.setFont(Theme::FontRole::Body, QFont("sans", 9.0));  // 9pt
        // 9pt × (96/72) × multiplier(1.0) = 12 px
        QCOMPARE(t.pixelSizeFor(Theme::Slot::TextDefault), 9.0 * 96.0 / 72.0);
    }

    void pixel_size_for_heading_uses_heading_role_and_multiplier() {
        Theme t;
        t.setFont(Theme::FontRole::Heading, QFont("sans", 10.0));
        t.setFontSizeMultiplier(Theme::Slot::Heading2, 1.5);
        QCOMPARE(t.pixelSizeFor(Theme::Slot::Heading2), 10.0 * 96.0 / 72.0 * 1.5);
    }

    void pixel_size_for_code_block_uses_monospace_role() {
        Theme t;
        QFont mono("monospace"); mono.setPointSizeF(9.75);
        t.setFont(Theme::FontRole::Monospace, mono);
        QCOMPARE(t.pixelSizeFor(Theme::Slot::CodeBlock), 9.75 * 96.0 / 72.0);
    }

    void pixel_size_for_inline_code_uses_monospace_role() {
        Theme t;
        t.setFont(Theme::FontRole::Monospace, QFont("monospace", 9.0));
        QCOMPARE(t.pixelSizeFor(Theme::Slot::InlineCode), 9.0 * 96.0 / 72.0);
    }

    void pixel_size_for_math_uses_monospace_role() {
        Theme t;
        t.setFont(Theme::FontRole::Monospace, QFont("monospace", 9.0));
        QCOMPARE(t.pixelSizeFor(Theme::Slot::Math), 9.0 * 96.0 / 72.0);
    }

    void pixel_size_for_falls_back_to_11pt_when_role_pointSize_unset() {
        Theme t;  // no setFont calls
        QCOMPARE(t.pixelSizeFor(Theme::Slot::TextDefault), 11.0 * 96.0 / 72.0);
    }

    void pixel_size_for_clamps_zero_or_negative_multiplier_to_one() {
        Theme t;
        t.setFont(Theme::FontRole::Body, QFont("sans", 10.0));
        t.setFontSizeMultiplier(Theme::Slot::TextDefault, 0.0);
        QCOMPARE(t.pixelSizeFor(Theme::Slot::TextDefault), 10.0 * 96.0 / 72.0);
        t.setFontSizeMultiplier(Theme::Slot::TextDefault, -1.0);
        QCOMPARE(t.pixelSizeFor(Theme::Slot::TextDefault), 10.0 * 96.0 / 72.0);
    }

    // --- Task 1B: familyFor ---

    void family_for_heading_uses_heading_role() {
        Theme t;
        t.setFont(Theme::FontRole::Heading, QFont("Cambria", 11));
        QCOMPARE(t.familyFor(Theme::Slot::Heading1), QStringLiteral("Cambria"));
        QCOMPARE(t.familyFor(Theme::Slot::Heading6), QStringLiteral("Cambria"));
    }

    void family_for_code_block_uses_monospace_role() {
        Theme t;
        t.setFont(Theme::FontRole::Monospace, QFont("Menlo", 11));
        QCOMPARE(t.familyFor(Theme::Slot::CodeBlock),  QStringLiteral("Menlo"));
        QCOMPARE(t.familyFor(Theme::Slot::InlineCode), QStringLiteral("Menlo"));
        QCOMPARE(t.familyFor(Theme::Slot::Math),       QStringLiteral("Menlo"));
    }

    void family_for_other_slots_uses_body_role() {
        Theme t;
        t.setFont(Theme::FontRole::Body, QFont("Helvetica", 11));
        QCOMPARE(t.familyFor(Theme::Slot::TextDefault), QStringLiteral("Helvetica"));
        QCOMPARE(t.familyFor(Theme::Slot::Quote),       QStringLiteral("Helvetica"));
        QCOMPARE(t.familyFor(Theme::Slot::Link),        QStringLiteral("Helvetica"));
    }

    // --- Task 1C: defaultLight() expansions ---

    void default_light_has_explicit_multipliers_for_all_heading_levels() {
        const Theme t = Theme::defaultLight();
        QCOMPARE(t.fontSizeMultiplier(Theme::Slot::Heading1), 1.8);
        QCOMPARE(t.fontSizeMultiplier(Theme::Slot::Heading2), 1.5);
        QCOMPARE(t.fontSizeMultiplier(Theme::Slot::Heading3), 1.3);
        QCOMPARE(t.fontSizeMultiplier(Theme::Slot::Heading4), 1.15);
        QCOMPARE(t.fontSizeMultiplier(Theme::Slot::Heading5), 1.05);
        QCOMPARE(t.fontSizeMultiplier(Theme::Slot::Heading6), 1.0);
    }

    void default_light_has_explicit_multipliers_for_math_and_quote() {
        const Theme t = Theme::defaultLight();
        QCOMPARE(t.fontSizeMultiplier(Theme::Slot::Math),  1.0);
        QCOMPARE(t.fontSizeMultiplier(Theme::Slot::Quote), 1.0);
    }

    void default_light_marks_h1_h2_h3_bold() {
        const Theme t = Theme::defaultLight();
        QVERIFY(t.isBold(Theme::Slot::Heading1));
        QVERIFY(t.isBold(Theme::Slot::Heading2));
        QVERIFY(t.isBold(Theme::Slot::Heading3));
        QVERIFY(!t.isBold(Theme::Slot::Heading4));
        QVERIFY(!t.isBold(Theme::Slot::Heading5));
        QVERIFY(!t.isBold(Theme::Slot::Heading6));
    }

    void default_light_marks_quote_italic() {
        const Theme t = Theme::defaultLight();
        QVERIFY(t.isItalic(Theme::Slot::Quote));
    }

    void default_light_body_pixel_size_lands_at_14() {
        const Theme t = Theme::defaultLight();
        // 10.5pt × 96/72 = 14
        QCOMPARE(t.pixelSizeFor(Theme::Slot::TextDefault), 10.5 * 96.0 / 72.0);
    }

    void default_light_monospace_pixel_size_lands_at_13() {
        const Theme t = Theme::defaultLight();
        // 9.75pt × 96/72 = 13
        QCOMPARE(t.pixelSizeFor(Theme::Slot::CodeBlock), 9.75 * 96.0 / 72.0);
    }

    void default_dark_inherits_multipliers_and_styles_from_light() {
        const Theme dark = Theme::defaultDark();
        QCOMPARE(dark.fontSizeMultiplier(Theme::Slot::Heading4), 1.15);
        QVERIFY(dark.isItalic(Theme::Slot::Quote));
        QVERIFY(dark.isBold(Theme::Slot::Heading3));
    }
};

QTEST_APPLESS_MAIN(TstFoundationTheme)
#include "tst_foundation_theme.moc"
