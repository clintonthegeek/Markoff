// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>
#include <QTextCursor>
#include <QUndoStack>
#include <markoff/source/SourceEditor.h>
#include <markoff/MarkoffDocument.h>
#include <markoff/MarkdownDelta.h>

class TstSourceCanonicalAttach : public QObject {
    Q_OBJECT
private slots:
    void setDocument_loadsInitialText();
    void externalDelta_splicesIntoQutepart();
    void documentReloaded_replacesContent();
    void setDocumentNull_detaches();
    void localEdit_pushesCanonicalDelta();
    void localEdit_multipleCoalesce();
    void localDelete_pushesCanonicalDelta();
    void localReplace_pushesCanonicalDelta();
};

void TstSourceCanonicalAttach::setDocument_loadsInitialText()
{
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("# hello"), Markoff::Origin::FirstOpen);
    Markoff::Source::SourceEditor src;
    src.setDocument(&doc);
    QCOMPARE(src.toPlainText(), QStringLiteral("# hello"));
}

void TstSourceCanonicalAttach::externalDelta_splicesIntoQutepart()
{
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("# hello"), Markoff::Origin::FirstOpen);
    Markoff::Source::SourceEditor src;
    src.setDocument(&doc);
    doc.undoStack()->push(new Markoff::MarkdownDelta(&doc, 7, 0, QStringLiteral(" world")));
    QCOMPARE(src.toPlainText(), QStringLiteral("# hello world"));
}

void TstSourceCanonicalAttach::documentReloaded_replacesContent()
{
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("first"), Markoff::Origin::FirstOpen);
    Markoff::Source::SourceEditor src;
    src.setDocument(&doc);
    doc.resetContent(QStringLiteral("second"), Markoff::Origin::ExternalReloadClean);
    QCOMPARE(src.toPlainText(), QStringLiteral("second"));
}

void TstSourceCanonicalAttach::setDocumentNull_detaches()
{
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("# hi"), Markoff::Origin::FirstOpen);
    Markoff::Source::SourceEditor src;
    src.setDocument(&doc);
    src.setDocument(nullptr);
    // No crash, no dangling connections.
    // Further edits to doc should not affect src.
    doc.undoStack()->push(new Markoff::MarkdownDelta(&doc, 0, 0, QStringLiteral("X")));
    QCOMPARE(src.toPlainText(), QStringLiteral("# hi"));  // unchanged
}

void TstSourceCanonicalAttach::localEdit_pushesCanonicalDelta()
{
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("hello"), Markoff::Origin::FirstOpen);
    Markoff::Source::SourceEditor src;
    src.setDocument(&doc);

    // Programmatic edit via QTextCursor on Qutepart's inner QTextDocument.
    QTextCursor c(src.innerDocument());
    c.setPosition(5);
    c.insertText(QStringLiteral(" world"));

    QCOMPARE(doc.toMarkdown(), QStringLiteral("hello world"));
    QCOMPARE(doc.undoStack()->count(), 1);
    QCOMPARE(src.toPlainText(), QStringLiteral("hello world"));
}

void TstSourceCanonicalAttach::localEdit_multipleCoalesce()
{
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("hel"), Markoff::Origin::FirstOpen);
    doc.setCoalescingIdleMs(10000);  // no idle-based cutoff in tests
    Markoff::Source::SourceEditor src;
    src.setDocument(&doc);

    QTextCursor c(src.innerDocument());
    c.setPosition(3);
    c.insertText(QStringLiteral("l"));
    c.insertText(QStringLiteral("o"));

    QCOMPARE(doc.toMarkdown(), QStringLiteral("hello"));
    // Adjacent pure inserts coalesce via MarkdownDelta::mergeWith.
    QCOMPARE(doc.undoStack()->count(), 1);
}

void TstSourceCanonicalAttach::localDelete_pushesCanonicalDelta()
{
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("hello"), Markoff::Origin::FirstOpen);
    Markoff::Source::SourceEditor src;
    src.setDocument(&doc);

    QTextCursor c(src.innerDocument());
    c.setPosition(5);
    c.deletePreviousChar();  // deletes 'o'

    QCOMPARE(doc.toMarkdown(), QStringLiteral("hell"));
    QCOMPARE(doc.undoStack()->count(), 1);
}

void TstSourceCanonicalAttach::localReplace_pushesCanonicalDelta()
{
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("hello"), Markoff::Origin::FirstOpen);
    Markoff::Source::SourceEditor src;
    src.setDocument(&doc);

    QTextCursor c(src.innerDocument());
    c.setPosition(0);
    c.setPosition(5, QTextCursor::KeepAnchor);
    c.insertText(QStringLiteral("HELLO"));  // replaces

    QCOMPARE(doc.toMarkdown(), QStringLiteral("HELLO"));
    // Qt typically fires this as contentsChange(0, 5, 5) — one MarkdownDelta.
    // If Qt splits into delete+insert, acceptable outcome is 2 deltas.
    QVERIFY(doc.undoStack()->count() >= 1);
    QVERIFY(doc.undoStack()->count() <= 2);
}

QTEST_MAIN(TstSourceCanonicalAttach)
#include "tst_source_canonical_attach.moc"
