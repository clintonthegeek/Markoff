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
};

QTEST_APPLESS_MAIN(TstFoundationTheme)
#include "tst_foundation_theme.moc"
