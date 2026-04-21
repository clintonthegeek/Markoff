// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QtTest>
#include <QSignalSpy>
#include <QTextCursor>
#include <QTextDocument>
#include <QUndoStack>
#include <QGraphicsScene>
#include <markoff/Editor.h>
#include <markoff/MarkoffDocument.h>
#include <markoff/MarkdownDelta.h>
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

    // Phase C3 Task 16 — inbound canonical-delta tests
    void inboundDelta_splicesIntoItem();
    void inboundDelta_spanningMultipleItems_marksForFullRebuild();
    void documentReloaded_tearsDownScene();

    // v0.6.0-alpha.3 — regression: post-setDocument focus must be on the
    // first text item, or key events bubble from m_view back up to Editor
    // and recurse infinitely (2026-04-21 dogfood SEGV).
    void postSetDocument_firstTextItemHasFocus();
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

// -------------------------------------------------------------------------
// Phase C3 Task 16 — inbound canonical-delta tests
// -------------------------------------------------------------------------

void TstLiveCanonicalAttach::inboundDelta_splicesIntoItem()
{
    MarkoffDocument doc;
    doc.resetContent(QStringLiteral("hello world"), Origin::FirstOpen);
    Editor ed;
    ed.setDocument(&doc);
    QSignalSpy parsed(&doc, &MarkoffDocument::parseUpdated);
    QVERIFY(parsed.wait(2000));

    // External delta (not via a local edit in Live) — push directly onto
    // the canonical undo stack.
    doc.undoStack()->push(new Markoff::MarkdownDelta(&doc, 5, 0, QStringLiteral(", friend")));
    // Canonical should show it.
    QCOMPARE(doc.toMarkdown(), QStringLiteral("hello, friend world"));
    // Live's first item should have the splice applied.
    auto *coord = ed.coordinatorForTesting();
    QVERIFY(coord);
    const auto &map = coord->itemMap();
    QVERIFY(!map.isEmpty());
    auto *mti = static_cast<MarkdownTextItem *>(map[0].item);
    QCOMPARE(mti->document()->toPlainText(), QStringLiteral("hello, friend world"));
}

void TstLiveCanonicalAttach::inboundDelta_spanningMultipleItems_marksForFullRebuild()
{
    // Use markdown with an image segment so MarkdownSplitter creates >= 2 items.
    const QString md = QStringLiteral("text1\n\n![](x.png)\n\ntext2");
    MarkoffDocument doc;
    doc.resetContent(md, Origin::FirstOpen);
    Editor ed;
    ed.setDocument(&doc);
    QSignalSpy parsed(&doc, &MarkoffDocument::parseUpdated);
    QVERIFY(parsed.wait(2000));
    parsed.clear();

    auto *coord = ed.coordinatorForTesting();
    QVERIFY(coord);
    const auto &mapBefore = coord->itemMap();
    QVERIFY(mapBefore.size() >= 2);

    // Delta that spans items: delete a range that crosses item 0's canonicalEnd.
    const qsizetype cross = mapBefore[0].canonicalEnd - 1;  // one byte inside item 0
    const qsizetype len = 4;                                 // crosses into item 1
    doc.undoStack()->push(new Markoff::MarkdownDelta(&doc, cross, len, QStringLiteral("X")));
    // Accept either: scene immediately correct, or force-rebuilt after next parseUpdated.
    QVERIFY(parsed.wait(2000));
    // After reparse, coordinator's toMarkdown should match canonical.
    QCOMPARE(coord->toMarkdown(), doc.toMarkdown());
}

void TstLiveCanonicalAttach::documentReloaded_tearsDownScene()
{
    MarkoffDocument doc;
    doc.resetContent(QStringLiteral("# first"), Origin::FirstOpen);
    Editor ed;
    ed.setDocument(&doc);
    QSignalSpy parsed(&doc, &MarkoffDocument::parseUpdated);
    QVERIFY(parsed.wait(2000));
    parsed.clear();

    QSignalSpy reload(&doc, &MarkoffDocument::documentReloaded);
    doc.resetContent(QStringLiteral("# second content"), Origin::ExternalReloadClean);
    QCOMPARE(reload.count(), 1);
    QVERIFY(parsed.wait(2000));
    // Scene rebuilt with new content.
    auto *coord = ed.coordinatorForTesting();
    QCOMPARE(coord->toMarkdown(), QStringLiteral("# second content"));
}

/// v0.6.0-alpha.3 regression: when Editor::setDocument completes and the
/// scene is built, the first text item MUST have focus. Otherwise a typed
/// key event delivered to Editor bubbles through m_view (whose parent is
/// Editor), finds no accepting focus item in the scene, bubbles back to
/// Editor::event → keyPressEvent → QApplication::sendEvent(m_view, ...) →
/// infinite recursion → stack overflow SEGV. The focus-assignment loop
/// that Phase-A's rebuildScene() had at its tail was lost when Task 13
/// copied the width/font/subscribe logic into onCanonicalParseUpdated
/// without the focus block. Surfaced 2026-04-21 during v0.6.0 dogfood.
void TstLiveCanonicalAttach::postSetDocument_firstTextItemHasFocus()
{
    MarkoffDocument doc;
    doc.resetContent(QStringLiteral("hello world\n\npara2"), Origin::FirstOpen);
    Editor ed;
    ed.setDocument(&doc);
    QSignalSpy parsed(&doc, &MarkoffDocument::parseUpdated);
    if (parsed.count() == 0)
        QVERIFY(parsed.wait(2000));

    auto *coord = ed.coordinatorForTesting();
    QVERIFY(coord);
    const auto &items = coord->items();
    QVERIFY(!items.isEmpty());

    // Find the first text item; it must be the scene's focusItem after
    // setDocument, or key events bubble back up to Editor infinitely.
    SelectableItem *firstTextItem = nullptr;
    for (auto *it : items) {
        if (it->isTextItem()) { firstTextItem = it; break; }
    }
    QVERIFY(firstTextItem);
    auto *scene = firstTextItem->asGraphicsItem()->scene();
    QVERIFY(scene);
    QCOMPARE(scene->focusItem(), firstTextItem->asGraphicsItem());
}

QTEST_MAIN(TstLiveCanonicalAttach)
#include "tst_live_canonical_attach.moc"
