// SPDX-License-Identifier: GPL-3.0-or-later
#include <QSignalSpy>
#include <QTest>
#include <QTextDocument>
#include <QtCore/qnamespace.h>

#include <markoff/core/BlockKind.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/SourceTextDocumentBinding.h>

using namespace Markoff;

class TstBindingStructuralKey : public QObject {
    Q_OBJECT
    static void wire(MarkoffDocument &doc, QTextDocument &qdoc,
                     SourceTextDocumentBinding &b) {
        b.setTextDocument(&qdoc);
        b.setMarkoffDocument(&doc);
    }
private Q_SLOTS:
    void enter_at_end_of_first_bullet_keeps_heading_and_inserts_item() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("### H\n\n- one\n- two\n"));
        QTextDocument qdoc;
        SourceTextDocumentBinding b;
        wire(doc, qdoc, b);

        // qtPos at end of "one": "### H"(5) + "\n"(1) + "one"(3) = 9.
        const QString flat = QString::fromUtf8(doc.widgetFlatView());
        QCOMPARE(qdoc.toPlainText(), flat);
        const int qtPos = 9;

        QSignalSpy caretSpy(&b, &SourceTextDocumentBinding::caretResolved);
        const bool handled = b.handleStructuralKey(Qt::Key_Return, Qt::NoModifier,
                                                   qtPos, qtPos);
        QVERIFY(handled);
        QTRY_VERIFY(caretSpy.count() >= 1);

        const auto blocks = doc.iterateBlocks();
        QCOMPARE(doc.blockText(blocks[0]), QByteArrayLiteral("### H"));      // heading untouched
        QCOMPARE(doc.blockKind(blocks[2]), BlockKind::ListItem);
        QCOMPARE(doc.blockText(blocks[2]), QByteArrayLiteral(""));          // new empty item
    }

    void typing_key_returns_false() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("hello"));
        QTextDocument qdoc;
        SourceTextDocumentBinding b;
        wire(doc, qdoc, b);
        QVERIFY(!b.handleStructuralKey(Qt::Key_A, Qt::NoModifier, 5, 5));   // not structural
    }

    void selection_on_plain_text_collapses() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("hello"));
        QTextDocument qdoc;
        SourceTextDocumentBinding b;
        wire(doc, qdoc, b);
        // Select "ell" = [1,4), press Delete (not a boundary op, but selection
        // is deleted and the key consumed).
        QSignalSpy caretSpy(&b, &SourceTextDocumentBinding::caretResolved);
        const bool handled = b.handleStructuralKey(Qt::Key_Delete, Qt::NoModifier, 4, 1);
        QVERIFY(handled);
        QTRY_VERIFY(caretSpy.count() >= 1);
        QCOMPARE(doc.blockText(doc.iterateBlocks()[0]), QByteArrayLiteral("ho"));
    }

    void enter_with_selection_collapses_then_splits() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("- on=DELETE=two\n"));
        QTextDocument qdoc;
        SourceTextDocumentBinding b;
        wire(doc, qdoc, b);
        // Single ListItem; buffer = "on=DELETE=two" (marker stripped).
        // Positions: o0 n1 =2 D3 E4 L5 E6 T7 E8 =9 t10 w11 o12.
        // Select "=DELETE=" = [2,10), then Enter.
        const int selStart = 2, selEnd = 10;
        QSignalSpy caretSpy(&b, &SourceTextDocumentBinding::caretResolved);
        const bool handled = b.handleStructuralKey(Qt::Key_Return, Qt::NoModifier,
                                                   selEnd, selStart);  // qtPos, qtAnchor
        QVERIFY(handled);
        QTRY_VERIFY(caretSpy.count() >= 1);
        const auto blocks = doc.iterateBlocks();
        QCOMPARE(int(blocks.size()), 2);
        QCOMPARE(doc.blockText(blocks[0]), QByteArrayLiteral("on"));
        QCOMPARE(doc.blockText(blocks[1]), QByteArrayLiteral("two"));
    }

    void backspace_with_selection_deletes_then_no_op() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("hello world"));  // one paragraph
        QTextDocument qdoc;
        SourceTextDocumentBinding b;
        wire(doc, qdoc, b);
        // Select " world" = [5,11), Backspace. Selection deleted; caret at 5.
        // Backspace at byte 5 (not start) is NOT a structural op, but the
        // selection was already deleted, so handled==true with caret at 5.
        QSignalSpy caretSpy(&b, &SourceTextDocumentBinding::caretResolved);
        const bool handled = b.handleStructuralKey(Qt::Key_Backspace, Qt::NoModifier,
                                                   11, 5);
        QVERIFY(handled);
        QTRY_VERIFY(caretSpy.count() >= 1);
        const auto blocks = doc.iterateBlocks();
        QCOMPARE(int(blocks.size()), 1);
        QCOMPARE(doc.blockText(blocks[0]), QByteArrayLiteral("hello"));
    }
};

QTEST_MAIN(TstBindingStructuralKey)
#include "tst_binding_structural_key.moc"
