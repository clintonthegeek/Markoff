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

    void selection_returns_false_for_now() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("hello"));
        QTextDocument qdoc;
        SourceTextDocumentBinding b;
        wire(doc, qdoc, b);
        // Non-empty selection (qtPos != qtAnchor): Task 5 returns false; Task 6 adds collapse.
        QVERIFY(!b.handleStructuralKey(Qt::Key_Return, Qt::NoModifier, 3, 1));
    }
};

QTEST_MAIN(TstBindingStructuralKey)
#include "tst_binding_structural_key.moc"
