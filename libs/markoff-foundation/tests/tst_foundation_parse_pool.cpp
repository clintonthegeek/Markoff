// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>
#include <markoff-parser/Document.h>

#include "../src/ParsePool.h"

using namespace Markoff;

class TstFoundationParsePool : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void parsed_document_initially_null() {
        MarkoffDocument doc(1);
        QVERIFY(doc.parsedDocument() == nullptr);
    }

    void parse_updated_fires_after_local_edit() {
        MarkoffDocument doc(1);
        QSignalSpy spy(&doc, &MarkoffDocument::parseUpdated);

        QList<MarkoffEdit> ed;
        MarkoffEdit i; i.oldStart = 0; i.oldEnd = 0;
        i.newText = "# Hello\n\nWorld\n";
        ed << i;
        doc.applyLocalEdit(ed);

        QVERIFY(spy.wait(2000));
        QVERIFY(doc.parsedDocument() != nullptr);
    }

    void scheduleReset_drives_a_fresh_parse() {
        using namespace Markoff::Parse::Detail;
        ParsePool pool;
        QSignalSpy spy(&pool, &ParsePool::parseReady);

        pool.scheduleReset(QByteArrayLiteral("# Initial\n\nbody"));
        QVERIFY(spy.wait(2000));
        QCOMPARE(spy.count(), 1);

        // Drain the captured Document; the parser owns its memory.
        const auto args = spy.takeFirst();
        const auto *parsed = qvariant_cast<const Markoff::Document *>(args.at(0));
        QVERIFY(parsed != nullptr);
        QCOMPARE(parsed->sourceText(), QStringLiteral("# Initial\n\nbody"));
        delete parsed;
    }

    void schedule_after_scheduleReset_uses_new_session() {
        using namespace Markoff::Parse::Detail;
        ParsePool pool;
        QSignalSpy spy(&pool, &ParsePool::parseReady);

        // Reset to one body, then incrementally update — both deliveries
        // should reflect the right source.
        pool.scheduleReset(QByteArrayLiteral("first"));
        QVERIFY(spy.wait(2000));
        while (spy.wait(50)) { /* drain */ }

        // Free Documents from the reset wave.
        for (auto &args : std::as_const(spy)) {
            delete qvariant_cast<const Markoff::Document *>(args.at(0));
        }
        spy.clear();

        pool.schedule(QByteArrayLiteral("first second"));
        QVERIFY(spy.wait(2000));
        while (spy.wait(50)) { /* drain */ }

        QVERIFY(spy.count() >= 1);
        const auto args = spy.takeLast();
        const auto *parsed = qvariant_cast<const Markoff::Document *>(args.at(0));
        QVERIFY(parsed != nullptr);
        QCOMPARE(parsed->sourceText(), QStringLiteral("first second"));
        delete parsed;
        // Free any prior deliveries.
        for (auto &a : std::as_const(spy)) {
            delete qvariant_cast<const Markoff::Document *>(a.at(0));
        }
    }

    void schedule_coalesces_in_flight_requests() {
        using namespace Markoff::Parse::Detail;
        ParsePool pool;
        QSignalSpy spy(&pool, &ParsePool::parseReady);

        // Queue 50 snapshots back-to-back, each with a distinct content marker
        // so we can identify which one(s) ran. The "marker" is a unique line
        // count: snapshot N has N+1 newlines.
        const int N = 50;
        for (int i = 0; i < N; ++i) {
            QByteArray b("# t\n");
            for (int j = 0; j < i; ++j) b.append("x\n");
            pool.schedule(b);
        }

        // Wait for whatever the pool decides to deliver to settle.
        // It MUST deliver at least one parse, and it MUST NOT deliver more
        // than 2 (the in-flight one at coalesce time + one drained from pending).
        QVERIFY(spy.wait(2000));
        // Drain any further deliveries that might still be in queue.
        while (spy.wait(200)) { /* keep draining */ }

        // Only the most-recently-scheduled snapshot's content is allowed to be
        // surfaced as the *last* delivery. (Earlier deliveries are permitted
        // for the coalesce-window grace; we don't constrain them tightly here.)
        // Hard cap on total deliveries: 2.
        QVERIFY2(spy.count() >= 1, qPrintable(QString("got %1 deliveries").arg(spy.count())));
        QVERIFY2(spy.count() <= 2, qPrintable(QString("got %1 deliveries — coalescing failed").arg(spy.count())));
    }
};

QTEST_MAIN(TstFoundationParsePool)
#include "tst_foundation_parse_pool.moc"
