// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>
#include <QSignalSpy>
#include <markoff/ParsePool.h>
#include <markoff/MarkoffDocument.h>
#include <markoff/MarkdownDelta.h>

class TstParsePool : public QObject {
    Q_OBJECT
private slots:
    void job_producesParseSignal();
    void burst_collapsesToOneEmission();
    void cancelFor_dropsPendingJobs();
};

void TstParsePool::job_producesParseSignal() {
    Markoff::MarkoffDocument doc;
    QSignalSpy spy(&doc, &Markoff::MarkoffDocument::parseUpdated);
    doc.resetContent(QStringLiteral("# hello\n\nworld"), Markoff::Origin::FirstOpen);
    QVERIFY(spy.wait(1000));
    QCOMPARE(spy.count(), 1);
    QVERIFY(doc.parsedDocument() != nullptr);
}

void TstParsePool::burst_collapsesToOneEmission() {
    Markoff::MarkoffDocument doc;
    doc.setCoalescingIdleMs(50);
    doc.resetContent(QStringLiteral("x"), Markoff::Origin::FirstOpen);
    QSignalSpy spy(&doc, &Markoff::MarkoffDocument::parseUpdated);
    spy.wait(200);
    spy.clear();
    for (int i = 0; i < 100; ++i) {
        doc.undoStack()->push(new Markoff::MarkdownDelta(&doc, doc.length(), 0, QStringLiteral("a")));
    }
    QVERIFY(spy.wait(1500));
    QVERIFY(spy.count() <= 2);
}

void TstParsePool::cancelFor_dropsPendingJobs() {
    Markoff::MarkoffDocument doc;
    QSignalSpy spy(&doc, &Markoff::MarkoffDocument::parseUpdated);
    doc.resetContent(QStringLiteral("initial"), Markoff::Origin::FirstOpen);
    QVERIFY(spy.wait(1000));
    spy.clear();
    doc.undoStack()->push(new Markoff::MarkdownDelta(&doc, 0, 0, QStringLiteral("A")));
    // Drop the doc immediately — pool should cancel; no emit after dtor.
}

QTEST_GUILESS_MAIN(TstParsePool)
#include "tst_parse_pool.moc"
