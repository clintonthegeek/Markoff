// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>
#include <markoff/MarkoffDocument.h>
#include <markoff/CursorPosition.h>
#include <markoff/MarkdownDelta.h>

class TstCursorAnchor : public QObject {
    Q_OBJECT
private slots:
    void precedingInsert_shiftsAnchor();
    void followingInsert_preservesAnchor();
    void straddlingDelta_leftBias_clampsToStart();
    void straddlingDelta_rightBias_clampsToEnd();
    // Edge-case pass — C3 landing review §3
    void anchor_survivesMacroGroupedEdits();
    void anchor_stableUnderUndoRedo();
};

void TstCursorAnchor::precedingInsert_shiftsAnchor() {
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("hello world"), Markoff::Origin::FirstOpen);
    auto p = doc.trackCursor(6, Markoff::CursorBias::Left);
    doc.undoStack()->push(new Markoff::MarkdownDelta(&doc, 0, 0, QStringLiteral("> ")));
    QCOMPARE(doc.resolveCursor(p), qsizetype(8));
}

void TstCursorAnchor::followingInsert_preservesAnchor() {
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("hello world"), Markoff::Origin::FirstOpen);
    auto p = doc.trackCursor(5, Markoff::CursorBias::Left);
    doc.undoStack()->push(new Markoff::MarkdownDelta(&doc, 11, 0, QStringLiteral("!")));
    QCOMPARE(doc.resolveCursor(p), qsizetype(5));
}

void TstCursorAnchor::straddlingDelta_leftBias_clampsToStart() {
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("abcdef"), Markoff::Origin::FirstOpen);
    auto p = doc.trackCursor(3, Markoff::CursorBias::Left);
    doc.undoStack()->push(new Markoff::MarkdownDelta(&doc, 2, 2, QStringLiteral("XY")));
    QCOMPARE(doc.resolveCursor(p), qsizetype(2));
}

void TstCursorAnchor::straddlingDelta_rightBias_clampsToEnd() {
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("abcdef"), Markoff::Origin::FirstOpen);
    auto p = doc.trackCursor(3, Markoff::CursorBias::Right);
    doc.undoStack()->push(new Markoff::MarkdownDelta(&doc, 2, 2, QStringLiteral("XY")));
    QCOMPARE(doc.resolveCursor(p), qsizetype(4));
}

// --- Edge-case pass (C3 landing review §3) ---

void TstCursorAnchor::anchor_survivesMacroGroupedEdits() {
    // Anchor at 6 (Left) in "hello world"; two preceding inserts in a macro:
    // "> " (2 chars) then ">> " (3 chars) both at offset 0.
    // Each shifts the anchor: 6→8→11.
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("hello world"), Markoff::Origin::FirstOpen);
    auto p = doc.trackCursor(6, Markoff::CursorBias::Left);
    doc.undoStack()->beginMacro(QStringLiteral("Macro"));
    doc.undoStack()->push(new Markoff::MarkdownDelta(&doc, 0, 0, QStringLiteral("> ")));
    doc.undoStack()->push(new Markoff::MarkdownDelta(&doc, 0, 0, QStringLiteral(">> ")));
    doc.undoStack()->endMacro();
    QCOMPARE(doc.resolveCursor(p), qsizetype(11));
}

void TstCursorAnchor::anchor_stableUnderUndoRedo() {
    // Anchor at 6 (Left) in "hello world"; insert "> " at offset 0 shifts it to 8.
    // Undo reverses the insert (removes 2 chars at 0) — anchor returns to 6.
    // Redo re-applies the insert — anchor returns to 8.
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("hello world"), Markoff::Origin::FirstOpen);
    auto p = doc.trackCursor(6, Markoff::CursorBias::Left);
    doc.undoStack()->push(new Markoff::MarkdownDelta(&doc, 0, 0, QStringLiteral("> ")));
    QCOMPARE(doc.resolveCursor(p), qsizetype(8));
    doc.undoStack()->undo();
    QCOMPARE(doc.resolveCursor(p), qsizetype(6));
    doc.undoStack()->redo();
    QCOMPARE(doc.resolveCursor(p), qsizetype(8));
}

QTEST_GUILESS_MAIN(TstCursorAnchor)
#include "tst_cursor_anchor.moc"
