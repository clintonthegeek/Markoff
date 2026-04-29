// SPDX-License-Identifier: GPL-3.0-or-later
#include <QPalette>
#include <QSignalSpy>
#include <QTest>

#include <markoff/source/widget/Editor.h>
#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/Origin.h>
#include <markoff-foundation/Theme.h>

class TstSourceWidgetEditor : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void editor_constructs() {
        Markoff::Source::Widget::Editor e;
        QVERIFY(e.document() == nullptr);
    }

    void setDocument_attaches_and_seed_text_appears() {
        Markoff::Source::Widget::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.setCoalescingIdleMs(0);
        doc.resetContent(QByteArray("hello world"), Markoff::Origin::FirstOpen);
        e.setDocument(&doc);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        // setDocument may itself trigger a parse-flow; allow brief settle.
        QTest::qWait(50);
        QCOMPARE(e.toPlainText(), QStringLiteral("hello world"));
        QCOMPARE(e.document(), &doc);
    }

    void setTheme_updates_palette_base_color() {
        Markoff::Source::Widget::Editor e;
        Markoff::Theme t = Markoff::Theme::defaultLight();
        const QColor sentinel("#abcdef");
        t.setColor(Markoff::Theme::Slot::EditorBackground, sentinel);
        e.setTheme(t);
        QCOMPARE(e.palette().color(QPalette::Base), sentinel);
    }
};

QTEST_MAIN(TstSourceWidgetEditor)
#include "tst_source_widget_editor.moc"
