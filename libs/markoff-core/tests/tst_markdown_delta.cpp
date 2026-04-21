// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>
#include <QUndoStack>
#include <markoff/MarkdownDelta.h>
#include <markoff/MarkoffDocument.h>

class TstMarkdownDelta : public QObject {
    Q_OBJECT
private slots:
    void redo_appliesInsert();
    void undo_reverses();
    void mergeWith_coalescesAdjacentInsert();
    void mergeWith_rejectsNonAdjacent();
    void mergeWith_rejectsCrossType();
};

void TstMarkdownDelta::redo_appliesInsert() {
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("hello"), Markoff::Origin::FirstOpen);
    doc.undoStack()->push(new Markoff::MarkdownDelta(&doc, 5, 0, QStringLiteral(" world")));
    QCOMPARE(doc.toMarkdown(), QStringLiteral("hello world"));
}

void TstMarkdownDelta::undo_reverses() {
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("hello"), Markoff::Origin::FirstOpen);
    doc.undoStack()->push(new Markoff::MarkdownDelta(&doc, 5, 0, QStringLiteral(" world")));
    doc.undoStack()->undo();
    QCOMPARE(doc.toMarkdown(), QStringLiteral("hello"));
}

void TstMarkdownDelta::mergeWith_coalescesAdjacentInsert() {
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("hel"), Markoff::Origin::FirstOpen);
    doc.setCoalescingIdleMs(10000);
    doc.undoStack()->push(new Markoff::MarkdownDelta(&doc, 3, 0, QStringLiteral("l")));
    doc.undoStack()->push(new Markoff::MarkdownDelta(&doc, 4, 0, QStringLiteral("o")));
    QCOMPARE(doc.toMarkdown(), QStringLiteral("hello"));
    QCOMPARE(doc.undoStack()->count(), 1);
    doc.undoStack()->undo();
    QCOMPARE(doc.toMarkdown(), QStringLiteral("hel"));
}

void TstMarkdownDelta::mergeWith_rejectsNonAdjacent() {
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("abc def"), Markoff::Origin::FirstOpen);
    doc.setCoalescingIdleMs(10000);
    doc.undoStack()->push(new Markoff::MarkdownDelta(&doc, 0, 0, QStringLiteral("X")));
    doc.undoStack()->push(new Markoff::MarkdownDelta(&doc, 8, 0, QStringLiteral("Y")));
    QCOMPARE(doc.undoStack()->count(), 2);
}

void TstMarkdownDelta::mergeWith_rejectsCrossType() {
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("abcdef"), Markoff::Origin::FirstOpen);
    doc.setCoalescingIdleMs(10000);
    doc.undoStack()->push(new Markoff::MarkdownDelta(&doc, 3, 0, QStringLiteral("Z")));
    doc.undoStack()->push(new Markoff::MarkdownDelta(&doc, 4, 1, QString()));
    QCOMPARE(doc.undoStack()->count(), 2);
}

QTEST_GUILESS_MAIN(TstMarkdownDelta)
#include "tst_markdown_delta.moc"
