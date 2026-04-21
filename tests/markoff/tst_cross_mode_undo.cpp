// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>
#include <QSignalSpy>
#include <markoff/MarkoffDocument.h>
#include <markoff/MarkdownDelta.h>
#include <markoff/Editor.h>
#include <markoff/source/SourceEditor.h>
#include <QUndoStack>
#include <QTextCursor>

using namespace Markoff;

class TstCrossModeUndo : public QObject {
    Q_OBJECT
private slots:
    void editInSource_swapToLive_edit_undoReversesLifo();
    void undo_allTheWayToOriginal();
};

// ---------------------------------------------------------------------------
// Slot 1 — cross-mode LIFO undo
// ---------------------------------------------------------------------------
// Edit via Source, detach Source, attach Live, edit via Live, then Ctrl+Z
// twice. The Live edit must reverse first (LIFO), then the Source edit —
// the canonical QUndoStack is the single history regardless of which leaf
// was active at edit time.
//
// The two inserts are at non-adjacent offsets (Source inserts at 5, Live
// inserts at 0 — far from 5+8=13) so mergeWith never coalesces them into
// a single undo step.
// ---------------------------------------------------------------------------
void TstCrossModeUndo::editInSource_swapToLive_edit_undoReversesLifo()
{
    MarkoffDocument doc;
    doc.resetContent(QStringLiteral("hello world"), Origin::FirstOpen);

    // Edit in Source.
    Source::SourceEditor src;
    src.setDocument(&doc);
    {
        QTextCursor c(src.innerDocument());
        c.setPosition(5);
        c.insertText(QStringLiteral(", friend"));
    }
    QCOMPARE(doc.toMarkdown(), QStringLiteral("hello, friend world"));

    // Detach Source (simulating a mode swap).
    src.setDocument(nullptr);

    // Attach Live and wait for the parse to settle.
    Editor live;
    live.setDocument(&doc);
    QSignalSpy parsed(&doc, &MarkoffDocument::parseUpdated);
    QVERIFY(parsed.wait(2000));

    // Edit via Live: push a MarkdownDelta that prepends "> " at offset 0.
    // offset 0 is not adjacent to Source's insert end (offset 5+8=13),
    // so mergeWith returns false and these are two separate undo steps.
    doc.undoStack()->push(new MarkdownDelta(&doc, 0, 0, QStringLiteral("> ")));
    QCOMPARE(doc.toMarkdown(), QStringLiteral("> hello, friend world"));

    // Ctrl+Z #1 — reverses Live's edit (LIFO order).
    doc.undoStack()->undo();
    QCOMPARE(doc.toMarkdown(), QStringLiteral("hello, friend world"));

    // Ctrl+Z #2 — reverses Source's edit.
    doc.undoStack()->undo();
    QCOMPARE(doc.toMarkdown(), QStringLiteral("hello world"));
}

// ---------------------------------------------------------------------------
// Slot 2 — undo all the way back to original
// ---------------------------------------------------------------------------
// Four sequential MarkdownDeltas are pushed via the canonical stack (as all
// four leaves would do for a local user edit). Inserting at offset 0 each
// time ensures no two consecutive deltas are offset-adjacent, so mergeWith
// never coalesces them. Undoing four times returns the document to "base".
// ---------------------------------------------------------------------------
void TstCrossModeUndo::undo_allTheWayToOriginal()
{
    MarkoffDocument doc;
    doc.resetContent(QStringLiteral("base"), Origin::FirstOpen);

    // Four inserts, each prepended at the current start of the document.
    // After each push the document grows, so offset 0 is never equal to
    // (prevOffset + prevInserted.size()), preventing mergeWith coalescing.
    doc.undoStack()->push(new MarkdownDelta(&doc, 0, 0, QStringLiteral("A ")));
    QCOMPARE(doc.toMarkdown(), QStringLiteral("A base"));

    doc.undoStack()->push(new MarkdownDelta(&doc, 0, 0, QStringLiteral("B ")));
    QCOMPARE(doc.toMarkdown(), QStringLiteral("B A base"));

    doc.undoStack()->push(new MarkdownDelta(&doc, 0, 0, QStringLiteral("C ")));
    QCOMPARE(doc.toMarkdown(), QStringLiteral("C B A base"));

    doc.undoStack()->push(new MarkdownDelta(&doc, 0, 0, QStringLiteral("D ")));
    QCOMPARE(doc.toMarkdown(), QStringLiteral("D C B A base"));

    // Undo all four in reverse order.
    doc.undoStack()->undo();
    QCOMPARE(doc.toMarkdown(), QStringLiteral("C B A base"));

    doc.undoStack()->undo();
    QCOMPARE(doc.toMarkdown(), QStringLiteral("B A base"));

    doc.undoStack()->undo();
    QCOMPARE(doc.toMarkdown(), QStringLiteral("A base"));

    doc.undoStack()->undo();
    QCOMPARE(doc.toMarkdown(), QStringLiteral("base"));
}

QTEST_MAIN(TstCrossModeUndo)
#include "tst_cross_mode_undo.moc"
