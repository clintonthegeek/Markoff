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
        Markoff::Theme theme;
        const QTextCharFormat fmt = theme.charFormat(Markoff::Theme::Slot::HiddenMarker);
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
};

QTEST_APPLESS_MAIN(TstFoundationTheme)
#include "tst_foundation_theme.moc"
