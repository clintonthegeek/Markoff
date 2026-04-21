// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QtTest>
#include <QSignalSpy>
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

QTEST_MAIN(TstLiveCanonicalAttach)
#include "tst_live_canonical_attach.moc"
