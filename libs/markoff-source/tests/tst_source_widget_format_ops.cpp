// SPDX-License-Identifier: GPL-3.0-or-later
//
// Markdown format operations on the source widget (parity with Live's
// LiveFormatController). Dogfood-driven 2026-05-21: in the live view
// Ctrl+B / Ctrl+I / Ctrl+1..6 work; in source view they did nothing.

#include <QPlainTextEdit>
#include <QTest>
#include <QTextCursor>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/source/Editor.h>

class TstSourceFormatOps : public QObject {
    Q_OBJECT
private:
    void selectRange(Markoff::Source::Editor &e, int start, int end) {
        QTextCursor c = e.plainTextEdit()->textCursor();
        c.setPosition(start);
        c.setPosition(end, QTextCursor::KeepAnchor);
        e.plainTextEdit()->setTextCursor(c);
    }

    void placeCursor(Markoff::Source::Editor &e, int pos) {
        QTextCursor c = e.plainTextEdit()->textCursor();
        c.setPosition(pos);
        e.plainTextEdit()->setTextCursor(c);
    }

private Q_SLOTS:
    void initTestCase() {
        // Source editor uses QPlainTextEdit internally; needs QGuiApplication.
    }

    void toggleBold_wraps_selection() {
        Markoff::Source::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArray("hello world"));
        e.setDocument(&doc);
        QTest::qWait(50);

        selectRange(e, 0, 5);  // "hello"
        e.toggleBold();

        QCOMPARE(e.toPlainText(), QStringLiteral("**hello** world"));
    }

    void toggleBold_unwraps_when_surrounded() {
        Markoff::Source::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArray("**hello** world"));
        e.setDocument(&doc);
        QTest::qWait(50);

        selectRange(e, 2, 7);  // "hello" inside the markers
        e.toggleBold();

        QCOMPARE(e.toPlainText(), QStringLiteral("hello world"));
    }

    void toggleItalic_wraps_with_underscores() {
        Markoff::Source::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArray("hello world"));
        e.setDocument(&doc);
        QTest::qWait(50);

        selectRange(e, 6, 11);  // "world"
        e.toggleItalic();

        QCOMPARE(e.toPlainText(), QStringLiteral("hello _world_"));
    }

    void toggleStrikethrough_wraps_with_tildes() {
        Markoff::Source::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArray("hello"));
        e.setDocument(&doc);
        QTest::qWait(50);

        selectRange(e, 0, 5);
        e.toggleStrikethrough();

        QCOMPARE(e.toPlainText(), QStringLiteral("~~hello~~"));
    }

    void toggleInlineCode_wraps_with_backticks() {
        Markoff::Source::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArray("call foo() here"));
        e.setDocument(&doc);
        QTest::qWait(50);

        selectRange(e, 5, 10);  // "foo()"
        e.toggleInlineCode();

        QCOMPARE(e.toPlainText(), QStringLiteral("call `foo()` here"));
    }

    void insertLink_at_cursor_inserts_template() {
        Markoff::Source::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArray("hello"));
        e.setDocument(&doc);
        QTest::qWait(50);

        placeCursor(e, 5);  // end of "hello"
        e.insertLink();

        QCOMPARE(e.toPlainText(), QStringLiteral("hello[](url)"));
    }

    void insertLink_wraps_selection_with_brackets() {
        Markoff::Source::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArray("see hello world"));
        e.setDocument(&doc);
        QTest::qWait(50);

        selectRange(e, 4, 9);  // "hello"
        e.insertLink();

        QCOMPARE(e.toPlainText(), QStringLiteral("see [hello](url) world"));
    }

    void setHeadingLevel_promotes_paragraph_line() {
        Markoff::Source::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArray("Hello world"));
        e.setDocument(&doc);
        QTest::qWait(50);

        placeCursor(e, 3);  // somewhere in "Hello"
        e.setHeadingLevel(2);

        QCOMPARE(e.toPlainText(), QStringLiteral("## Hello world"));
    }

    void setHeadingLevel_changes_existing_level() {
        Markoff::Source::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArray("# Hello"));
        e.setDocument(&doc);
        QTest::qWait(50);

        placeCursor(e, 4);  // in "Hello"
        e.setHeadingLevel(4);

        QCOMPARE(e.toPlainText(), QStringLiteral("#### Hello"));
    }

    void setHeadingLevel_zero_strips_markers() {
        Markoff::Source::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArray("### Hello"));
        e.setDocument(&doc);
        QTest::qWait(50);

        placeCursor(e, 4);
        e.setHeadingLevel(0);

        QCOMPARE(e.toPlainText(), QStringLiteral("Hello"));
    }

    void setHeadingLevel_only_affects_current_line() {
        Markoff::Source::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArray("first line\nsecond line\nthird line"));
        e.setDocument(&doc);
        QTest::qWait(50);

        // Place cursor in the second line.
        placeCursor(e, 13);
        e.setHeadingLevel(2);

        QCOMPARE(e.toPlainText(),
                 QStringLiteral("first line\n## second line\nthird line"));
    }
};

QTEST_MAIN(TstSourceFormatOps)
#include "tst_source_widget_format_ops.moc"
