// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>
#include <QSignalSpy>
#include <markoff/reading/ReadingView.h>
#include <markoff/MarkoffDocument.h>

class TstReadingCanonicalAttach : public QObject {
    Q_OBJECT
private slots:
    void setDocument_rebuildsAfterParseUpdated();
    void documentReloaded_resetsSectionLayout();
    void hasEditing_returnsFalse();
    void setDocumentNull_detaches();
};

void TstReadingCanonicalAttach::setDocument_rebuildsAfterParseUpdated()
{
    Markoff::MarkoffDocument doc;
    Markoff::Reading::ReadingView rv;
    rv.setDocument(&doc);

    QSignalSpy parsed(&doc, &Markoff::MarkoffDocument::parseUpdated);
    doc.resetContent(QStringLiteral("# Heading\n\nBody."),
                     Markoff::Origin::FirstOpen);
    QVERIFY(parsed.wait(2000));

    // After parseUpdated, Reading should have built some section layout.
    QVERIFY(rv.sectionCount() > 0);
}

void TstReadingCanonicalAttach::documentReloaded_resetsSectionLayout()
{
    Markoff::MarkoffDocument doc;
    Markoff::Reading::ReadingView rv;
    rv.setDocument(&doc);

    // Populate initial content and wait for first parse.
    QSignalSpy parsed(&doc, &Markoff::MarkoffDocument::parseUpdated);
    doc.resetContent(QStringLiteral("# first"), Markoff::Origin::FirstOpen);
    QVERIFY(parsed.wait(2000));
    parsed.clear();

    // Reload with new content.
    QSignalSpy reload(&doc, &Markoff::MarkoffDocument::documentReloaded);
    doc.resetContent(QStringLiteral("# second content\n\nwith body"),
                     Markoff::Origin::ExternalReloadClean);
    QCOMPARE(reload.count(), 1);

    // Wait for reparse after reload.
    QVERIFY(parsed.wait(2000));

    // After reload + reparse, layout corresponds to new content.
    QVERIFY(rv.sectionCount() > 0);
}

void TstReadingCanonicalAttach::hasEditing_returnsFalse()
{
    Markoff::Reading::ReadingView rv;
    QVERIFY(!rv.hasEditing());
    QVERIFY(rv.isReadOnly());
}

void TstReadingCanonicalAttach::setDocumentNull_detaches()
{
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("# hi"), Markoff::Origin::FirstOpen);
    Markoff::Reading::ReadingView rv;
    rv.setDocument(&doc);

    // Detach — must not crash.
    rv.setDocument(nullptr);

    // Future doc events must not affect rv (no crash, no dangling
    // connection). Drive a content change to confirm.
    doc.resetContent(QStringLiteral("# after detach"),
                     Markoff::Origin::FirstOpen);
}

QTEST_MAIN(TstReadingCanonicalAttach)
#include "tst_reading_canonical_attach.moc"
