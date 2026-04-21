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

QTEST_GUILESS_MAIN(TstCursorAnchor)
#include "tst_cursor_anchor.moc"
