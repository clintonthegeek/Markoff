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

    void toggleBold_at_block_boundary_does_not_merge_blocks() {
        // Companion regression to setHeadingLevel_twice_does_not_merge:
        // wrapping a selection whose start sits exactly at a markoff block
        // boundary used to trip applyFlatEdit's cross-block branch via the
        // QTextCursor-mediated path. Now block-aware.
        Markoff::Source::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArray("para text\n\nhello world"));
        e.setDocument(&doc);
        QTest::qWait(50);

        // WP unification: widgetFlatView() joins blocks with a single '\n', so
        // block 2 starts at qt-pos 10 and "hello" is qt 10..15 — its start sits
        // exactly at the block boundary in sep-view.
        selectRange(e, 10, 15);
        e.toggleBold();
        QCOMPARE(e.toPlainText(),
                 QStringLiteral("para text\n**hello** world"));

        // Unwrap a selection whose start sits one byte INTO the block —
        // markers sit BEFORE selection, at the boundary. Surrounded-outside
        // detection must still see them. After the wrap "hello" is qt 12..17.
        selectRange(e, 12, 17);  // "hello" inside "**hello**"
        e.toggleBold();
        QCOMPARE(e.toPlainText(),
                 QStringLiteral("para text\nhello world"));
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

    void insertLink_at_block_boundary_stays_in_target_block() {
        // Companion regression to toggleBold_at_block_boundary_does_not_merge
        // and setHeadingLevel_twice_does_not_merge_with_previous_block.
        // insertLink was the last QTextCursor-mediated format op; the latent
        // bug class would surface if a future variant grew a removeSelectedText
        // step (cursor-insert forward-bias hid it today). Block-aware port
        // matches the heading + wrap pattern.
        Markoff::Source::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArray("para text\n\nhello world"));
        e.setDocument(&doc);
        QTest::qWait(50);

        // Cursor at qt-pos 10 — exactly at the start of block 2 in the
        // single-'\n' sep-view under WP unification.
        placeCursor(e, 10);
        e.insertLink();
        QCOMPARE(e.toPlainText(),
                 QStringLiteral("para text\n[](url)hello world"));

        // Selection whose start sits inside the new layout: "[](url)" occupies
        // qt 10..16, so "hello" is qt 17..22.
        selectRange(e, 17, 22);  // "hello" in the new layout
        e.insertLink();
        QCOMPARE(e.toPlainText(),
                 QStringLiteral("para text\n[](url)[hello](url) world"));
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

    void setHeadingLevel_twice_does_not_merge_with_previous_block() {
        // Regression for the 2026-05-21 dogfood report: a second toggle of
        // setHeadingLevel on a heading block preceded by another block was
        // merging the heading into the previous block via applyFlatEdit's
        // cross-block-edit branch (the Qt range edit started at the
        // markoff-block boundary; sep→no-sep translation lost direction).
        Markoff::Source::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArray("para text\n\nHello"));
        e.setDocument(&doc);
        QTest::qWait(50);

        // WP unification single-'\n' view: "para text\nHello", "Hello" at
        // qt 10..15.
        placeCursor(e, 12);  // inside "Hello"
        e.setHeadingLevel(2);
        QCOMPARE(e.toPlainText(),
                 QStringLiteral("para text\n## Hello"));

        // Second invocation must not merge with the previous block.
        e.setHeadingLevel(3);
        QCOMPARE(e.toPlainText(),
                 QStringLiteral("para text\n### Hello"));

        // And again — strip path also operates at the block boundary.
        e.setHeadingLevel(0);
        QCOMPARE(e.toPlainText(),
                 QStringLiteral("para text\nHello"));
    }

    void setHeadingLevel_only_affects_current_line() {
        Markoff::Source::Editor e;
        Markoff::MarkoffDocument doc(1);
        // Three separate paragraph blocks ("\n\n"-delimited in source). Pre-WP
        // this used single '\n's, but buildD2FromBytes now collapses soft line
        // breaks within a paragraph to spaces, so single-'\n'-joined lines load
        // as ONE block. widgetFlatView() then renders the three blocks
        // single-'\n'-joined, which is what toPlainText() shows.
        doc.loadFromMarkdown(QByteArray("first line\n\nsecond line\n\nthird line"));
        e.setDocument(&doc);
        QTest::qWait(50);

        // Place cursor in the second line (qt 11..22 in the single-'\n' view).
        placeCursor(e, 13);
        e.setHeadingLevel(2);

        QCOMPARE(e.toPlainText(),
                 QStringLiteral("first line\n## second line\nthird line"));
    }
};

QTEST_MAIN(TstSourceFormatOps)
#include "tst_source_widget_format_ops.moc"
