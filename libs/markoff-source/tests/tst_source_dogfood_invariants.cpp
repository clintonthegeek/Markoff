// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QPlainTextEdit>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/source/Editor.h>

class TstSourceDogfoodInvariants : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void enter_at_paragraph_end_creates_block_and_places_caret() {
        Markoff::Source::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Alpha\n\nBravo"));
        e.setDocument(&doc);
        e.resize(400, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());

        QTextDocument *qdoc = e.plainTextEdit()->document();
        QTextCursor c(qdoc);
        c.setPosition(5);  // end of "Alpha"
        e.plainTextEdit()->setTextCursor(c);
        QTest::keyClick(e.plainTextEdit(), Qt::Key_Return);
        QTest::qWait(80);

        QCOMPARE(int(doc.iterateBlocks().size()), 3);
        QCOMPARE(doc.blockText(doc.iterateBlocks()[1]), QByteArrayLiteral(""));
        QCOMPARE(e.plainTextEdit()->textCursor().position(), 6);
        QCOMPARE(qdoc->toPlainText(), QStringLiteral("Alpha\n\nBravo"));
    }

    void enter_at_document_end_creates_block() {
        Markoff::Source::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Alpha"));
        e.setDocument(&doc);
        e.resize(400, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());

        QTextCursor c(e.plainTextEdit()->document());
        c.setPosition(5);
        e.plainTextEdit()->setTextCursor(c);
        QTest::keyClick(e.plainTextEdit(), Qt::Key_Return);
        QTest::qWait(80);

        QCOMPARE(int(doc.iterateBlocks().size()), 2);
        QCOMPARE(doc.blockText(doc.iterateBlocks()[1]), QByteArrayLiteral(""));
        QCOMPARE(e.plainTextEdit()->textCursor().position(), 6);
        QCOMPARE(e.plainTextEdit()->document()->toPlainText(),
                 QStringLiteral("Alpha\n"));
    }

    void enter_mid_paragraph_splits_with_caret_at_new_block() {
        Markoff::Source::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("AlphaBravo"));
        e.setDocument(&doc);
        e.resize(400, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());

        QTextCursor c(e.plainTextEdit()->document());
        c.setPosition(5);
        e.plainTextEdit()->setTextCursor(c);
        QTest::keyClick(e.plainTextEdit(), Qt::Key_Return);
        QTest::qWait(80);

        const auto blocks = doc.iterateBlocks();
        QCOMPARE(int(blocks.size()), 2);
        QCOMPARE(doc.blockText(blocks[0]), QByteArrayLiteral("Alpha"));
        QCOMPARE(doc.blockText(blocks[1]), QByteArrayLiteral("Bravo"));
        QCOMPARE(e.plainTextEdit()->textCursor().position(), 6);
    }

    void backspace_at_block_start_merges_with_caret_at_join() {
        Markoff::Source::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Alpha\n\nBravo"));
        e.setDocument(&doc);
        e.resize(400, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());

        // In WP unification widgetFlatView, "Alpha\n\nBravo" the file parses
        // to [Alpha, Bravo] and renders as "Alpha\nBravo" (single \n). Start
        // of "Bravo" is at sep-view pos 6.
        QTextCursor c(e.plainTextEdit()->document());
        c.setPosition(6);
        e.plainTextEdit()->setTextCursor(c);
        QTest::keyClick(e.plainTextEdit(), Qt::Key_Backspace);
        QTest::qWait(80);

        const auto blocks = doc.iterateBlocks();
        QCOMPARE(int(blocks.size()), 1);
        QCOMPARE(doc.blockText(blocks[0]), QByteArrayLiteral("AlphaBravo"));
        QCOMPARE(e.plainTextEdit()->textCursor().position(), 5);
    }
};

QTEST_MAIN(TstSourceDogfoodInvariants)
#include "tst_source_dogfood_invariants.moc"
