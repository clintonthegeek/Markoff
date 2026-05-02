// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <QElapsedTimer>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>

#include "BlockAnchorComputation.h"

using namespace Markoff;

class TstFoundationBlockAnchorPerf : public QObject {
    Q_OBJECT
private Q_SLOTS:
    /// Regression guardrail (not a budget assertion). Compute on a 100-block,
    /// ~50 KB doc currently takes ~30ms wall-time, dominated by per-anchor
    /// CRDT lookups (cf. docs/handoff/2026-04-30-collabtext-crdt-join-perf-
    /// handoff.md flagging Global::join at ~20% of main-thread CPU). The
    /// 50ms ceiling here catches meaningful regressions while accepting the
    /// current CRDT-side cost as a known gap to be addressed in a separate
    /// spec/optimization pass.
    void anchor_compute_per_parse_regression_guardrail_50KB_doc() {
        MarkoffDocument doc{1};
        QSignalSpy spy(&doc, &MarkoffDocument::parseUpdated);
        QByteArray src;
        const QByteArray longContent = QByteArray(
            "Lorem ipsum dolor sit amet, consectetur adipiscing elit. "
            "Sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. "
            "Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris. "
        );
        for (int i = 0; i < 100; ++i) {
            src.append("Block ").append(QByteArray::number(i))
               .append(" content. ").append(longContent).append(longContent)
               .append(longContent).append("\n\n");
        }
        doc.resetContent(src, Origin::TestFixture);
        QVERIFY(spy.wait(2000));

        QElapsedTimer t; t.start();
        auto bundle = Markoff::Detail::computeBlockAnchors(doc, doc.parsedDocument());
        const qint64 ns = t.nsecsElapsed();
        const double ms = ns / 1e6;
        qInfo() << "computeBlockAnchors wall:" << ms << "ms"
                << "blocks:" << bundle.anchors.size();
        QVERIFY(ms < 50.0);
    }
};

QTEST_MAIN(TstFoundationBlockAnchorPerf)
#include "tst_foundation_block_anchor_perf.moc"
