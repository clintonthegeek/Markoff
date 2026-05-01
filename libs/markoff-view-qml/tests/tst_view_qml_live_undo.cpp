// SPDX-License-Identifier: GPL-3.0-or-later
#include <QObject>
#include <QQmlApplicationEngine>
#include <QQuickTextDocument>
#include <QSignalSpy>
#include <QTest>
#include <QTextCursor>
#include <QTextDocument>

#include <markoff/view/qml/LiveEditBinding.h>
#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/Origin.h>

using Markoff::MarkoffDocument;
using Markoff::BlockAnchor;
using Markoff::View::Qml::LiveEditBinding;

namespace {

// Load a QQuickTextDocument from a minimal QML TextEdit.
// Returns nullptr if offscreen rendering is unavailable.
QQuickTextDocument *seedQQuickTextDocument(QQmlApplicationEngine &engine)
{
    engine.loadData(
        R"qml(
            import QtQuick
            import QtQuick.Controls
            ApplicationWindow {
                visible: false
                TextEdit { id: te; objectName: "te" }
            }
        )qml"
    );
    QObject *root = engine.rootObjects().value(0);
    if (!root) return nullptr;
    QObject *te = root->findChild<QObject *>("te");
    if (!te) return nullptr;
    return qvariant_cast<QQuickTextDocument *>(te->property("textDocument"));
}

// Seed the document and wait for the parse to arrive. Returns the first
// BlockAnchor (nullopt becomes {}).
BlockAnchor seedDocumentAndWaitForParse(MarkoffDocument &doc,
                                        const QByteArray &content)
{
    doc.resetContent(content, Markoff::Origin::TestFixture);
    QSignalSpy parseSpy(&doc, &MarkoffDocument::parseUpdated);
    if (parseSpy.isEmpty())
        parseSpy.wait(2000);
    const auto anchorOpt = doc.blockAnchorAt(0);
    return anchorOpt.value_or(BlockAnchor{});
}

// Insert a single character at the end of the QTextDocument via cursor.
// The LiveEditBinding's contentsChange slot fires synchronously during this call.
void insertChar(QTextDocument *textDoc, QChar ch)
{
    QTextCursor c(textDoc);
    c.movePosition(QTextCursor::End);
    c.insertText(QString(ch));
}

// Simulate a single-char deletion (backspace) at the end.
void deleteLastChar(QTextDocument *textDoc)
{
    QTextCursor c(textDoc);
    c.movePosition(QTextCursor::End);
    c.deletePreviousChar();
}

}  // namespace

class TstViewQmlLiveUndo : public QObject {
    Q_OBJECT

private Q_SLOTS:

    // 1. Typing 5 single characters rapidly → they should all coalesce into 1
    //    undo entry (undoDepth() == 1 after 5 edits).
    void five_printable_chars_coalesce_to_one_undo_entry()
    {
        QQmlApplicationEngine engine;
        QQuickTextDocument *qqtd = seedQQuickTextDocument(engine);
        if (!qqtd) QSKIP("QML engine failed to load — offscreen QtQuick unavailable");

        MarkoffDocument doc(1);
        const BlockAnchor anchor = seedDocumentAndWaitForParse(doc, "hello");
        if (!doc.blockByteRange(anchor))
            QSKIP("blockByteRange returned nullopt — parse may not have arrived");

        // Seed QTextDocument with the same content.
        QTextCursor seed(qqtd->textDocument());
        seed.insertText(QStringLiteral("hello"));

        LiveEditBinding binding;
        binding.setDocument(&doc);
        binding.setBlockAnchor(anchor);
        binding.setTextDocument(qqtd);

        // Type 5 characters rapidly (no sleep — well within 1 s window).
        for (char ch : {'a', 'b', 'c', 'd', 'e'})
            insertChar(qqtd->textDocument(), QLatin1Char(ch));

        // All 5 edits should coalesce with the preceding one into a single
        // undo entry.  The first character creates entry #1; chars 2-5 each
        // call coalesceLastUndo(), so undoDepth stays at 1.
        QCOMPARE(doc.undoDepth(), 1);
    }

    // 2. Type a character, wait > 1 s, type another → 2 undo entries.
    void elapsed_time_breaks_coalesce_chain()
    {
        QQmlApplicationEngine engine;
        QQuickTextDocument *qqtd = seedQQuickTextDocument(engine);
        if (!qqtd) QSKIP("QML engine failed to load — offscreen QtQuick unavailable");

        MarkoffDocument doc(1);
        const BlockAnchor anchor = seedDocumentAndWaitForParse(doc, "hello");
        if (!doc.blockByteRange(anchor))
            QSKIP("blockByteRange returned nullopt");

        QTextCursor seed(qqtd->textDocument());
        seed.insertText(QStringLiteral("hello"));

        LiveEditBinding binding;
        binding.setDocument(&doc);
        binding.setBlockAnchor(anchor);
        binding.setTextDocument(qqtd);

        insertChar(qqtd->textDocument(), QLatin1Char('a'));   // entry 1
        QTest::qSleep(1100);                                   // > 1 s gap
        insertChar(qqtd->textDocument(), QLatin1Char('b'));   // entry 2 (chain broken)

        QCOMPARE(doc.undoDepth(), 2);
    }

    // 3. Type a char, do a backspace (non-printable), type a char → 3 entries.
    void backspace_breaks_coalesce_chain()
    {
        QQmlApplicationEngine engine;
        QQuickTextDocument *qqtd = seedQQuickTextDocument(engine);
        if (!qqtd) QSKIP("QML engine failed to load — offscreen QtQuick unavailable");

        MarkoffDocument doc(1);
        const BlockAnchor anchor = seedDocumentAndWaitForParse(doc, "hello");
        if (!doc.blockByteRange(anchor))
            QSKIP("blockByteRange returned nullopt");

        QTextCursor seed(qqtd->textDocument());
        seed.insertText(QStringLiteral("hello"));

        LiveEditBinding binding;
        binding.setDocument(&doc);
        binding.setBlockAnchor(anchor);
        binding.setTextDocument(qqtd);

        insertChar(qqtd->textDocument(), QLatin1Char('a'));  // entry 1
        deleteLastChar(qqtd->textDocument());                // entry 2 (deletion, breaks chain)
        insertChar(qqtd->textDocument(), QLatin1Char('b'));  // entry 3 (chain broken by deletion)

        QCOMPARE(doc.undoDepth(), 3);
    }

    // 4. Type a char, change blockAnchor (focus moves), type a char → 2 entries.
    void anchor_change_breaks_coalesce_chain()
    {
        QQmlApplicationEngine engine;
        QQuickTextDocument *qqtd = seedQQuickTextDocument(engine);
        if (!qqtd) QSKIP("QML engine failed to load — offscreen QtQuick unavailable");

        MarkoffDocument doc(1);
        // Seed two paragraphs so we get two distinct anchors.
        doc.resetContent("first\n\nsecond", Markoff::Origin::TestFixture);
        QSignalSpy parseSpy(&doc, &MarkoffDocument::parseUpdated);
        if (parseSpy.isEmpty()) parseSpy.wait(2000);
        const auto anchor0 = doc.blockAnchorAt(0);
        const auto anchor1 = doc.blockAnchorAt(1);
        if (!anchor0 || !anchor1)
            QSKIP("Could not obtain two block anchors — parse may not have arrived");

        QTextCursor seed(qqtd->textDocument());
        seed.insertText(QStringLiteral("first"));

        LiveEditBinding binding;
        binding.setDocument(&doc);
        binding.setBlockAnchor(*anchor0);
        binding.setTextDocument(qqtd);

        insertChar(qqtd->textDocument(), QLatin1Char('a'));  // entry 1 on block 0

        // Simulate focus moving to a different block.
        binding.setBlockAnchor(*anchor1);

        insertChar(qqtd->textDocument(), QLatin1Char('b'));  // entry 2 on block 1

        QCOMPARE(doc.undoDepth(), 2);
    }
};

QTEST_MAIN(TstViewQmlLiveUndo)
#include "tst_view_qml_live_undo.moc"
