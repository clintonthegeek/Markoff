// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>
#include <markoff/source/SourceEditor.h>
#include <markoff/MarkoffDocument.h>
#include <markoff/MarkdownDelta.h>
#include <QUndoStack>

class TstSourceCanonicalAttach : public QObject {
    Q_OBJECT
private slots:
    void setDocument_loadsInitialText();
    void externalDelta_splicesIntoQutepart();
    void documentReloaded_replacesContent();
    void setDocumentNull_detaches();
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

QTEST_MAIN(TstSourceCanonicalAttach)
#include "tst_source_canonical_attach.moc"
