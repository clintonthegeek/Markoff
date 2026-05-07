// SPDX-License-Identifier: GPL-3.0-or-later
#include <QPalette>
#include <QTest>

#include <markoff/source/Editor.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Theme.h>

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
        // D2: loadFromMarkdown populates the per-block structure the binding
        // reads; resetContent writes only to the legacy flat buffer.
        doc.loadFromMarkdown(QByteArray("hello world"));
        e.setDocument(&doc);
        // setDocument triggers syncQtDocumentFromMarkoff synchronously;
        // d2DocumentChanged is deferred — let it settle.
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
