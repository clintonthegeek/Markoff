// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QtTest>
#include <QSignalSpy>
#include <QTextCursor>
#include <QTextDocument>
#include <QUndoStack>
#include <markoff/Editor.h>
#include <markoff/MarkoffDocument.h>
#include "MarkdownTextItem.h"   // private src header (added via include path in CMakeLists)
#include "SceneCoordinator.h"   // private src header

using namespace Markoff;

class TstLiveCanonicalAttach : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void setDocument_buildsSceneAfterParseUpdated();
    void setDocument_buildsImmediatelyIfAlreadyParsed();
    void setDocumentNull_detaches();
    void setDocument_undoDisabledOnBlocks();

    // Phase C3 Task 15 — outbound delta tests
    void localEdit_pushesCanonicalDelta();
    void localEdit_deleteRange();
    void localEdit_replaceRange();
};

/// Attach before the first parse fires: scene is empty until parseUpdated
/// fires, then the coordinator has at least one rendered block.
void TstLiveCanonicalAttach::setDocument_buildsSceneAfterParseUpdated()
{
    MarkoffDocument doc;
    Editor ed;
    ed.setDocument(&doc);

    // No parse yet — scene is empty.
    QCOMPARE(ed.blockCount(), 0);

    QSignalSpy parsed(&doc, &MarkoffDocument::parseUpdated);
    doc.resetContent(QStringLiteral("# hello\n\nworld"), Origin::FirstOpen);
    QVERIFY(parsed.wait(2000));

    // After parseUpdated the scene has been built.
    // "# hello\n\nworld" is a single text segment (heading + paragraph),
    // so blockCount == 1.
    QVERIFY(ed.blockCount() >= 1);
}

/// Attach after parse is already complete: scene built synchronously.
void TstLiveCanonicalAttach::setDocument_buildsImmediatelyIfAlreadyParsed()
{
    MarkoffDocument doc;
    doc.resetContent(QStringLiteral("# hi"), Origin::FirstOpen);

    // Wait for the parse to complete before attaching.
    QSignalSpy parsed(&doc, &MarkoffDocument::parseUpdated);
    QVERIFY(parsed.wait(2000));
    QVERIFY(doc.parsedDocument() != nullptr);

    Editor ed;
    ed.setDocument(&doc);

    // parsedDocument() was non-null at attach time — scene is built synchronously.
    QVERIFY(ed.blockCount() >= 1);
}

/// setDocument(nullptr) detaches cleanly — no crash, document pointer cleared.
void TstLiveCanonicalAttach::setDocumentNull_detaches()
{
    MarkoffDocument doc;
    doc.resetContent(QStringLiteral("# hi"), Origin::FirstOpen);

    QSignalSpy parsed(&doc, &MarkoffDocument::parseUpdated);
    QVERIFY(parsed.wait(2000));

    Editor ed;
    ed.setDocument(&doc);
    QVERIFY(ed.blockCount() >= 1);

    ed.setDocument(nullptr);
    QCOMPARE(ed.document(), nullptr);
    // No crash is the primary assertion; block count should be 0 or empty
    // (loadMarkdown("") may produce one empty item depending on splitter).
    QVERIFY(ed.blockCount() <= 1);
}

/// Per-block QTextDocuments must have undo disabled after a scene build.
void TstLiveCanonicalAttach::setDocument_undoDisabledOnBlocks()
{
    MarkoffDocument doc;
    doc.resetContent(QStringLiteral("# heading\n\nparagraph"), Origin::FirstOpen);

    QSignalSpy parsed(&doc, &MarkoffDocument::parseUpdated);
    QVERIFY(parsed.wait(2000));

    Editor ed;
    ed.setDocument(&doc);
    QVERIFY(ed.blockCount() >= 1);

    // Every text block's QTextDocument must have undo disabled.
    auto *coordinator = ed.coordinatorForTesting();
    QVERIFY(coordinator != nullptr);
    int textItemCount = 0;
    for (auto *si : coordinator->items()) {
        if (si->isTextItem()) {
            ++textItemCount;
            auto *mti = static_cast<MarkdownTextItem *>(si);
            QVERIFY(!mti->document()->isUndoRedoEnabled());
        }
    }
    QVERIFY(textItemCount >= 1);
}

// -------------------------------------------------------------------------
// Phase C3 Task 15 — outbound delta tests
// -------------------------------------------------------------------------

/// Helper: load markdown into a doc+editor pair and wait for the scene to be
/// built. Returns true on success.
static bool setupEditorWithDoc(MarkoffDocument &doc, Editor &ed, const QString &md)
{
    ed.setDocument(&doc);
    QSignalSpy parsed(&doc, &MarkoffDocument::parseUpdated);
    doc.resetContent(md, Origin::FirstOpen);
    if (!parsed.wait(2000))
        return false;
    auto *coord = ed.coordinatorForTesting();
    return coord && !coord->itemMap().isEmpty();
}

/// Inserting text into a per-item QTextDocument must push a MarkdownDelta
/// onto the bound document's undo stack and update the canonical buffer.
void TstLiveCanonicalAttach::localEdit_pushesCanonicalDelta()
{
    MarkoffDocument doc;
    Editor ed;
    QVERIFY(setupEditorWithDoc(doc, ed, QStringLiteral("hello world")));

    auto *coord = ed.coordinatorForTesting();
    QVERIFY(coord);
    const auto &map = coord->itemMap();
    QVERIFY(!map.isEmpty());

    // The first item covers "hello world". Access its inner QTextDocument.
    auto *entry0item = map[0].item;
    QVERIFY(entry0item && entry0item->isTextItem());
    auto *mti = static_cast<MarkdownTextItem *>(entry0item);
    QTextDocument *td = mti->document();

    // Insert " there," after "hello" (position 5).
    QTextCursor c(td);
    c.setPosition(5);
    c.insertText(QStringLiteral(" there,"));

    // The canonical buffer should now contain the inserted text.
    QCOMPARE(doc.toMarkdown(), QStringLiteral("hello there, world"));

    // At least one delta must have been pushed onto the undo stack.
    QVERIFY(doc.undoStack()->count() >= 1);
}

/// Deleting a range in a per-item QTextDocument must push a MarkdownDelta
/// that removes the expected characters from the canonical buffer.
void TstLiveCanonicalAttach::localEdit_deleteRange()
{
    MarkoffDocument doc;
    Editor ed;
    // "hello world" — we'll delete " world" (6 chars starting at position 5).
    QVERIFY(setupEditorWithDoc(doc, ed, QStringLiteral("hello world")));

    auto *coord = ed.coordinatorForTesting();
    const auto &map = coord->itemMap();
    QVERIFY(!map.isEmpty());

    auto *mti = static_cast<MarkdownTextItem *>(map[0].item);
    QTextDocument *td = mti->document();

    // Select and delete " world" (positions 5..11).
    QTextCursor c(td);
    c.setPosition(5);
    c.setPosition(11, QTextCursor::KeepAnchor);
    c.deleteChar();

    // Canonical buffer should now just be "hello".
    QCOMPARE(doc.toMarkdown(), QStringLiteral("hello"));
    QVERIFY(doc.undoStack()->count() >= 1);
}

/// Replacing a range (delete + insert in one cursor operation) must push a
/// MarkdownDelta that reflects the replacement in the canonical buffer.
void TstLiveCanonicalAttach::localEdit_replaceRange()
{
    MarkoffDocument doc;
    Editor ed;
    // "hello world" — replace "world" with "Qt".
    QVERIFY(setupEditorWithDoc(doc, ed, QStringLiteral("hello world")));

    auto *coord = ed.coordinatorForTesting();
    const auto &map = coord->itemMap();
    QVERIFY(!map.isEmpty());

    auto *mti = static_cast<MarkdownTextItem *>(map[0].item);
    QTextDocument *td = mti->document();

    // Select "world" (positions 6..11) and replace with "Qt".
    QTextCursor c(td);
    c.setPosition(6);
    c.setPosition(11, QTextCursor::KeepAnchor);
    c.insertText(QStringLiteral("Qt"));

    // Canonical buffer should now be "hello Qt".
    QCOMPARE(doc.toMarkdown(), QStringLiteral("hello Qt"));
    QVERIFY(doc.undoStack()->count() >= 1);
}

QTEST_MAIN(TstLiveCanonicalAttach)
#include "tst_live_canonical_attach.moc"
