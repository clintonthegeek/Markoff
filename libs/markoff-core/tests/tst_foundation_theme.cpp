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

    // --- M7: value equality, content hash, and the slots the styled leaf's
    // colour sites needed ---

    void equality_compares_content_not_identity() {
        const Theme a = Theme::defaultLight();
        const Theme b = a;
        QVERIFY(a == b);
        QVERIFY(!(a != b));

        // A same-content theme built independently (a copy through JSON, as
        // a persisted theme comes back) must compare equal.
        const Theme roundTripped = Theme::fromJson(a.toJson());
        QVERIFY(a == roundTripped);

        // One colour change must break equality.
        Theme c = a;
        c.setColor(Theme::Slot::Link, QColor("#ff0000"));
        QVERIFY(a != c);
        // A font change too.
        Theme d = a;
        d.setFont(Theme::FontRole::Body, QFont("monospace", 12));
        QVERIFY(a != d);
    }

    void hasColor_distinguishes_set_from_fallback() {
        Theme t;
        // Unset: color() falls back (invalid for background slots,
        // TextDefault for foreground ones) — hasColor must say "not set".
        QVERIFY(!t.hasColor(Theme::Slot::CodeBlockBackground));
        QVERIFY(!t.hasColor(Theme::Slot::Link));
        QVERIFY(!t.color(Theme::Slot::CodeBlockBackground).isValid());

        t.setColor(Theme::Slot::Link, QColor(Qt::black));
        QVERIFY(t.hasColor(Theme::Slot::Link));
        // Set-to-black must be distinguishable from unset-fell-back-to-black.
        QVERIFY(t.color(Theme::Slot::Link).isValid());
    }

    void content_hash_tracks_every_changed_dimension() {
        const Theme a = Theme::defaultLight();

        QVERIFY(a.contentHash() == a.contentHash());  // stable across reads
        const Theme same = Theme::fromJson(a.toJson());
        QCOMPARE(same.contentHash(), a.contentHash()); // equal content, equal hash

        Theme colourChanged = a;
        colourChanged.setColor(Theme::Slot::Link, QColor("#ff0000"));
        QVERIFY(colourChanged.contentHash() != a.contentHash());

        Theme fontChanged = a;
        fontChanged.setFont(Theme::FontRole::Body, QFont("monospace", 12));
        QVERIFY(fontChanged.contentHash() != a.contentHash());

        Theme boldChanged = a;
        boldChanged.setBold(Theme::Slot::Link, true);
        QVERIFY(boldChanged.contentHash() != a.contentHash());

        Theme sizeChanged = a;
        sizeChanged.setFontSizeMultiplier(Theme::Slot::Heading1, 2.5);
        QVERIFY(sizeChanged.contentHash() != a.contentHash());

        // An order-insensitive fold: two themes that set the same slots in a
        // different ORDER hash equal.
        Theme orderA; Theme orderB;
        orderA.setColor(Theme::Slot::Link, QColor("#0066cc"));
        orderA.setColor(Theme::Slot::Tag,  QColor("#a04080"));
        orderB.setColor(Theme::Slot::Tag,  QColor("#a04080"));
        orderB.setColor(Theme::Slot::Link, QColor("#0066cc"));
        QCOMPARE(orderB.contentHash(), orderA.contentHash());

        // A fresh, empty theme signs differently from a populated one.
        QVERIFY(Theme().contentHash() != a.contentHash());
    }

    void promoted_styled_slots_round_trip_and_default_to_the_old_literals() {
        // The three slots added for the styled leaf's colour sites must
        // carry the exact values the leaf hardcoded before M7, so a
        // theme-less render is unchanged.
        const Theme light = Theme::defaultLight();
        QCOMPARE(light.color(Theme::Slot::InlineCodeBackground), QColor(245, 245, 245));
        QCOMPARE(light.color(Theme::Slot::FootnoteRef),          QColor(150, 90, 150));
        QCOMPARE(light.color(Theme::Slot::HorizontalRule),       QColor(180, 180, 180));

        // And they survive a JSON round trip (slots serialize by number; a
        // persisted theme must come back with them).
        const Theme back = Theme::fromJson(light.toJson());
        QCOMPARE(back.color(Theme::Slot::InlineCodeBackground), QColor(245, 245, 245));
        QCOMPARE(back.color(Theme::Slot::FootnoteRef),          QColor(150, 90, 150));
        QCOMPARE(back.color(Theme::Slot::HorizontalRule),       QColor(180, 180, 180));

        // The dark theme redefines them for its background.
        const Theme dark = Theme::defaultDark();
        QVERIFY(dark.color(Theme::Slot::InlineCodeBackground)
                    != light.color(Theme::Slot::InlineCodeBackground));
        QVERIFY(dark.color(Theme::Slot::HorizontalRule).lightness()
                    < light.color(Theme::Slot::HorizontalRule).lightness());
    }
};

QTEST_APPLESS_MAIN(TstFoundationTheme)
#include "tst_foundation_theme.moc"
