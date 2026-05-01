// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>

#include <markoff-foundation/BlockAnchor.h>
#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>

using namespace Markoff;

class TstFoundationBlockAnchorCompute : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void parse_updated_payload_carries_block_anchors_for_three_paragraphs() {
        MarkoffDocument doc{1};
        QSignalSpy spy(&doc, &MarkoffDocument::parseUpdated);
        doc.resetContent("a\n\nb\n\nc", Origin::TestFixture);
        QVERIFY(spy.wait(2000));

        // The new signal signature is (Document*, quint64, QList<BlockAnchor>).
        const auto &args = spy.last();
        QCOMPARE(args.size(), 3);
        const auto anchors = args.at(2).value<QList<BlockAnchor>>();
        QCOMPARE(anchors.size(), 3);
        // Each anchor's firstByte resolves to the corresponding block's
        // start byte (0, 3, 6 in the body "a\n\nb\n\nc").
        QCOMPARE(doc.resolveTextAnchor(anchors[0].firstByte), quint32{0});
        QCOMPARE(doc.resolveTextAnchor(anchors[1].firstByte), quint32{3});
        QCOMPARE(doc.resolveTextAnchor(anchors[2].firstByte), quint32{6});
    }

    void edit_within_block_keeps_block_anchor_identity() {
        MarkoffDocument doc{1};
        QSignalSpy spy(&doc, &MarkoffDocument::parseUpdated);
        doc.resetContent("first\n\nsecond", Origin::TestFixture);
        QVERIFY(spy.wait(2000));
        const auto firstAnchors = spy.last().at(2).value<QList<BlockAnchor>>();
        QCOMPARE(firstAnchors.size(), 2);
        const BlockAnchor secondBlockBefore = firstAnchors[1];

        // Insert a character in the *first* block. Second block's identity
        // should be preserved.
        spy.clear();
        MarkoffEdit e; e.oldStart = 5; e.oldEnd = 5; e.newText = "X";
        doc.applyLocalEdit({e});
        QVERIFY(spy.wait(2000));
        const auto afterAnchors = spy.last().at(2).value<QList<BlockAnchor>>();
        QCOMPARE(afterAnchors.size(), 2);
        QCOMPARE(afterAnchors[1], secondBlockBefore);
    }
};

QTEST_MAIN(TstFoundationBlockAnchorCompute)
#include "tst_foundation_block_anchor_compute.moc"
