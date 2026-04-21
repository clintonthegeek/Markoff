// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>
#include <QSignalSpy>
#include <markoff/MarkoffDocument.h>
#include <markoff/MarkdownDelta.h>

class TstOriginReset : public QObject {
    Q_OBJECT
private slots:
    void firstOpen_noDocumentReloaded();
    void externalReloadClean_clearsStack_emitsReloaded();
    void externalReloadResolved_clearsStack_emitsReloaded();
    void testFixture_clearsStack_emitsReloaded();
    void userRevertToSaved_pushesUndoable_emitsContentsChanged_notReloaded();
};

void TstOriginReset::firstOpen_noDocumentReloaded() {
    Markoff::MarkoffDocument doc;
    QSignalSpy reload(&doc, &Markoff::MarkoffDocument::documentReloaded);
    QSignalSpy contents(&doc, &Markoff::MarkoffDocument::contentsChanged);
    doc.resetContent(QStringLiteral("hello"), Markoff::Origin::FirstOpen);
    QCOMPARE(reload.count(), 0);
    QCOMPARE(contents.count(), 1);
    QCOMPARE(doc.undoStack()->count(), 0);
    QCOMPARE(doc.toMarkdown(), QStringLiteral("hello"));
}

void TstOriginReset::externalReloadClean_clearsStack_emitsReloaded() {
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("hello"), Markoff::Origin::FirstOpen);
    doc.undoStack()->push(new Markoff::MarkdownDelta(&doc, 5, 0, QStringLiteral(" world")));
    QCOMPARE(doc.undoStack()->count(), 1);
    QSignalSpy reload(&doc, &Markoff::MarkoffDocument::documentReloaded);
    doc.resetContent(QStringLiteral("from disk"), Markoff::Origin::ExternalReloadClean);
    QCOMPARE(reload.count(), 1);
    QCOMPARE(doc.undoStack()->count(), 0);
    QCOMPARE(doc.toMarkdown(), QStringLiteral("from disk"));
}

void TstOriginReset::externalReloadResolved_clearsStack_emitsReloaded() {
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("hello"), Markoff::Origin::FirstOpen);
    doc.undoStack()->push(new Markoff::MarkdownDelta(&doc, 5, 0, QStringLiteral(" world")));
    QSignalSpy reload(&doc, &Markoff::MarkoffDocument::documentReloaded);
    doc.resetContent(QStringLiteral("merged"), Markoff::Origin::ExternalReloadResolved);
    QCOMPARE(reload.count(), 1);
    QCOMPARE(doc.undoStack()->count(), 0);
}

void TstOriginReset::testFixture_clearsStack_emitsReloaded() {
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("hello"), Markoff::Origin::FirstOpen);
    doc.undoStack()->push(new Markoff::MarkdownDelta(&doc, 5, 0, QStringLiteral(" x")));
    QSignalSpy reload(&doc, &Markoff::MarkoffDocument::documentReloaded);
    doc.resetContent(QStringLiteral("fixture"), Markoff::Origin::TestFixture);
    QCOMPARE(reload.count(), 1);
    QCOMPARE(doc.undoStack()->count(), 0);
}

void TstOriginReset::userRevertToSaved_pushesUndoable_emitsContentsChanged_notReloaded() {
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("saved"), Markoff::Origin::FirstOpen);
    doc.undoStack()->push(new Markoff::MarkdownDelta(&doc, 5, 0, QStringLiteral(" edits")));
    QCOMPARE(doc.toMarkdown(), QStringLiteral("saved edits"));
    QSignalSpy reload(&doc, &Markoff::MarkoffDocument::documentReloaded);
    QSignalSpy contents(&doc, &Markoff::MarkoffDocument::contentsChanged);
    doc.resetContent(QStringLiteral("saved"), Markoff::Origin::UserRevertToSaved);
    QCOMPARE(reload.count(), 0);
    QVERIFY(contents.count() >= 1);
    QCOMPARE(doc.toMarkdown(), QStringLiteral("saved"));
    // Ctrl+Z reverses the revert.
    doc.undoStack()->undo();
    QCOMPARE(doc.toMarkdown(), QStringLiteral("saved edits"));
}

QTEST_GUILESS_MAIN(TstOriginReset)
#include "tst_origin_reset.moc"
