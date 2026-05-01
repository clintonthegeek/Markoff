// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <QElapsedTimer>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>

#include "TopLevelBlockScanner.h"
#include "BlockAnchorComputation.h"

using namespace Markoff;

class TstFoundationBlockAnchorPerf : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void scanner_under_1_ms_for_50KB_100_block_doc() {
        QByteArray src;
        src.reserve(50 * 1024);
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
        // Verify we have ~100 blocks at ~50 KB.
        QVERIFY(src.size() > 40 * 1024);
        QVERIFY(src.size() < 70 * 1024);

        // Warmup.
        (void)Markoff::Detail::scanTopLevelBlockRanges(src);

        QElapsedTimer t; t.start();
        const int iterations = 100;
        for (int i = 0; i < iterations; ++i) {
            (void)Markoff::Detail::scanTopLevelBlockRanges(src);
        }
        const qint64 nsTotal = t.nsecsElapsed();
        const double msPerIter = (nsTotal / 1e6) / iterations;
        qInfo() << "Scanner avg wall:" << msPerIter << "ms per call";
        QVERIFY(msPerIter < 1.0);
    }

    void anchor_compute_per_parse_under_2ms_on_50KB_doc() {
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
        auto bundle = Markoff::Detail::computeBlockAnchors(doc, doc.toMarkdownUtf8());
        const qint64 ns = t.nsecsElapsed();
        const double ms = ns / 1e6;
        qInfo() << "computeBlockAnchors wall:" << ms << "ms"
                << "blocks:" << bundle.anchors.size();
        QVERIFY(ms < 2.0);
    }
};

QTEST_MAIN(TstFoundationBlockAnchorPerf)
#include "tst_foundation_block_anchor_perf.moc"
