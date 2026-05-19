// SPDX-License-Identifier: GPL-3.0-or-later
#include <QPalette>
#include <QLineEdit>
#include <QTest>

#include <markoff/source/Editor.h>
#include <markoff/source/FindBar.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Theme.h>

class TstSourceWidgetEditor : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void editor_constructs() {
        Markoff::Source::Editor e;
        QVERIFY(e.document() == nullptr);
    }

    void setDocument_attaches_and_seed_text_appears() {
        Markoff::Source::Editor e;
        Markoff::MarkoffDocument doc(1);
        // D2: loadFromMarkdown populates the per-block structure the binding
        // reads; resetContent writes only to the legacy flat buffer.
        doc.loadFromMarkdown(QByteArray("hello world"));
        e.setDocument(&doc);
        // setDocument triggers syncQtDocumentFromMarkoff synchronously;
        // d2DocumentChanged is deferred — let it settle.
        QTest::qWait(50);
        QCOMPARE(e.plainTextEdit()->toPlainText(), QStringLiteral("hello world"));
        QCOMPARE(e.document(), &doc);
    }

    void setTheme_updates_palette_base_color() {
        Markoff::Source::Editor e;
        Markoff::Theme t = Markoff::Theme::defaultLight();
        const QColor sentinel("#abcdef");
        t.setColor(Markoff::Theme::Slot::EditorBackground, sentinel);
        e.setTheme(t);
        // Theme applies to the inner QPlainTextEdit, not the outer QWidget.
        QCOMPARE(e.plainTextEdit()->palette().color(QPalette::Base), sentinel);
    }

    void show_findbar_creates_visible_bar() {
        Markoff::Source::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("hello world\n"));
        e.setDocument(&doc);
        e.show();

        // Pre-condition: no FindBar visible.
        QVERIFY(e.findChild<Markoff::Source::FindBar *>() == nullptr);

        e.showFindBar();

        // Post-condition: a FindBar child exists and is visible.
        auto *bar = e.findChild<Markoff::Source::FindBar *>();
        QVERIFY(bar != nullptr);
        QVERIFY(bar->isVisible());
    }

    void hide_findbar_clears_highlights() {
        Markoff::Source::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("hello hello hello\n"));
        e.setDocument(&doc);
        e.show();
        e.showFindBar();

        auto *bar = e.findChild<Markoff::Source::FindBar *>();
        QVERIFY(bar != nullptr);

        // Type a needle into the input to populate matches/highlights.
        auto *input = bar->findChild<QLineEdit *>();
        QVERIFY(input != nullptr);
        QTest::keyClicks(input, QStringLiteral("hello"));
        QTRY_VERIFY(!e.plainTextEdit()->extraSelections().isEmpty());

        e.hideFindBar();

        QVERIFY(!bar->isVisible());
        QVERIFY(e.plainTextEdit()->extraSelections().isEmpty());
    }

    void showFindBar_is_idempotent() {
        Markoff::Source::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("hello\n"));
        e.setDocument(&doc);
        e.show();

        e.showFindBar();
        auto *firstBar = e.findChild<Markoff::Source::FindBar *>();
        QVERIFY(firstBar != nullptr);

        e.showFindBar();
        const auto bars = e.findChildren<Markoff::Source::FindBar *>();
        QCOMPARE(bars.size(), 1);
        QCOMPARE(bars.front(), firstBar);
    }

    void findbar_close_signal_hides() {
        Markoff::Source::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("hello\n"));
        e.setDocument(&doc);
        e.show();
        e.showFindBar();
        auto *bar = e.findChild<Markoff::Source::FindBar *>();
        QVERIFY(bar != nullptr);
        QVERIFY(bar->isVisible());

        emit bar->closed();

        QVERIFY(!bar->isVisible());
    }
};

QTEST_MAIN(TstSourceWidgetEditor)
#include "tst_source_widget_editor.moc"
