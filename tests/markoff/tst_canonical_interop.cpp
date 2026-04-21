// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QSignalSpy>
#include <QTextCursor>
#include <QUndoStack>
#include <QtTest>

#include <markoff/Editor.h>
#include <markoff/MarkdownDelta.h>
#include <markoff/MarkoffDocument.h>
#include <markoff/reading/ReadingView.h>
#include <markoff/source/SourceEditor.h>
// SceneCoordinator is forward-declared in Editor.h; its definition is needed
// to call methods on the coordinatorForTesting() return value. The src/
// include path is added in CMakeLists.txt.
#include "SceneCoordinator.h"

using namespace Markoff;

class TstCanonicalInterop : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void allThreeLeaves_boundToOneDoc_observeEachOthersEdits();
    void parseUpdated_firesOncePerBurst();
};

// ---------------------------------------------------------------------------
// Slot 1 — cross-leaf edit propagation
// ---------------------------------------------------------------------------
// All three leaves are bound to one MarkoffDocument.
// An edit through Source's inner QTextDocument must:
//   * propagate to the canonical buffer immediately (synchronous MarkdownDelta path)
//   * cause parseUpdated to fire (after the coalescing timer)
//   * cause Live's scene to rebuild (coordinator matches canonical)
//   * cause Reading's section layout to rebuild (sectionCount > 0)
// ---------------------------------------------------------------------------
void TstCanonicalInterop::allThreeLeaves_boundToOneDoc_observeEachOthersEdits()
{
    MarkoffDocument doc;
    Source::SourceEditor src;
    Editor live;
    Reading::ReadingView rv;

    // Bind all three before loading content.
    src.setDocument(&doc);
    live.setDocument(&doc);
    rv.setDocument(&doc);

    // Load content and wait for the first parse to complete.
    QSignalSpy parsed(&doc, &MarkoffDocument::parseUpdated);
    doc.resetContent(QStringLiteral("# Heading\n\nBody."), Origin::FirstOpen);
    QVERIFY(parsed.wait(2000));
    parsed.clear();

    // Edit through Source's inner QTextDocument — idiomatic per tst_source_canonical_attach.
    {
        QTextCursor c(src.innerDocument());
        c.movePosition(QTextCursor::End);
        c.insertText(QStringLiteral(" More."));
    }

    // Canonical must reflect the edit synchronously (MarkdownDelta path).
    QCOMPARE(doc.toMarkdown(), QStringLiteral("# Heading\n\nBody. More."));

    // Source's own buffer is consistent (no double-apply).
    QCOMPARE(src.toPlainText(), QStringLiteral("# Heading\n\nBody. More."));

    // Wait for the coalesced reparse.
    QVERIFY(parsed.wait(2000));

    // Live's coordinator must have rebuilt from the new canonical text.
    auto *coord = live.coordinatorForTesting();
    QVERIFY(coord != nullptr);
    QCOMPARE(coord->toMarkdown(), doc.toMarkdown());

    // Reading must have rebuilt its section layout (at least one section).
    QVERIFY(rv.sectionCount() > 0);
}

// ---------------------------------------------------------------------------
// Slot 2 — burst coalescing
// ---------------------------------------------------------------------------
// 100 rapid MarkdownDelta pushes within the coalescing window must collapse
// to ≤ 3 parseUpdated emissions. This is a loose bound: the invariant is
// "not 100 emissions", with +1 margin for scheduler jitter.
// ---------------------------------------------------------------------------
void TstCanonicalInterop::parseUpdated_firesOncePerBurst()
{
    MarkoffDocument doc;
    // Long coalescing window — all 100 edits land well within it.
    doc.setCoalescingIdleMs(200);

    Editor live;
    live.setDocument(&doc);

    // Seed document and absorb the FirstOpen parse.
    QSignalSpy parsed(&doc, &MarkoffDocument::parseUpdated);
    doc.resetContent(QStringLiteral("x"), Origin::FirstOpen);
    QVERIFY(parsed.wait(2000));
    parsed.clear();

    // Burst of 100 pure-insert deltas — each appends "a" at the current end.
    for (int i = 0; i < 100; ++i) {
        doc.undoStack()->push(new MarkdownDelta(&doc, doc.length(), 0, QStringLiteral("a")));
    }

    // Wait for the debounce window to close plus margin.
    // setCoalescingIdleMs(200) means the timer fires at most ~200 ms after the
    // last edit; 2000 ms is a generous test timeout.
    QVERIFY(parsed.wait(2000));

    // Loose bound: coalescing should produce far fewer than 100 emissions.
    // ≤ 3 accommodates a pathological double-fire under scheduling jitter.
    QVERIFY2(parsed.count() <= 3,
             qPrintable(QStringLiteral("Expected ≤ 3 parseUpdated emissions for 100-edit burst, got %1")
                            .arg(parsed.count())));
}

QTEST_MAIN(TstCanonicalInterop)
#include "tst_canonical_interop.moc"
