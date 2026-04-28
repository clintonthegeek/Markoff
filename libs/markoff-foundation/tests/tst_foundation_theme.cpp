// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QColor>

#include <markoff-foundation/Theme.h>

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
};

QTEST_APPLESS_MAIN(TstFoundationTheme)
#include "tst_foundation_theme.moc"
