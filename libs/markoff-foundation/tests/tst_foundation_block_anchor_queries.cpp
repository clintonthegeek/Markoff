// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>

using namespace Markoff;

class TstFoundationBlockAnchorQueries : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void blockAnchorAt_returns_anchor_for_each_top_level_block() {
        MarkoffDocument doc{1};
        QSignalSpy spy(&doc, &MarkoffDocument::parseUpdated);
        doc.resetContent("p1\n\np2\n\np3", Origin::TestFixture);
        QVERIFY(spy.wait(2000));

        const auto a0 = doc.blockAnchorAt(0);
        const auto a1 = doc.blockAnchorAt(1);
        const auto a2 = doc.blockAnchorAt(2);
        QVERIFY(a0.has_value());
        QVERIFY(a1.has_value());
        QVERIFY(a2.has_value());
        QCOMPARE(doc.resolveTextAnchor(a0->firstByte), quint32{0});
        QCOMPARE(doc.resolveTextAnchor(a1->firstByte), quint32{4});
        QCOMPARE(doc.resolveTextAnchor(a2->firstByte), quint32{8});
    }

    void blockAnchorAt_out_of_range_returns_nullopt() {
        MarkoffDocument doc{1};
        QSignalSpy spy(&doc, &MarkoffDocument::parseUpdated);
        doc.resetContent("only", Origin::TestFixture);
        QVERIFY(spy.wait(2000));
        QVERIFY(!doc.blockAnchorAt(1).has_value());
        QVERIFY(!doc.blockAnchorAt(-1).has_value());
    }

    void blockByteRange_returns_range_for_known_anchor() {
        MarkoffDocument doc{1};
        QSignalSpy spy(&doc, &MarkoffDocument::parseUpdated);
        doc.resetContent("hello\n\nworld", Origin::TestFixture);
        QVERIFY(spy.wait(2000));
        const auto block0 = doc.blockAnchorAt(0).value();
        const auto rng = doc.blockByteRange(block0);
        QVERIFY(rng.has_value());
        QCOMPARE(rng->first,  quint32{0});
        QCOMPARE(rng->second, quint32{5});  // "hello"
    }

    void blockAt_inside_first_block_returns_first_block() {
        MarkoffDocument doc{1};
        QSignalSpy spy(&doc, &MarkoffDocument::parseUpdated);
        doc.resetContent("aaaa\n\nbbbb", Origin::TestFixture);
        QVERIFY(spy.wait(2000));
        const TextAnchor mid = doc.textAnchorAt(2, /*rightBias*/ false);
        const auto block = doc.blockAt(mid);
        QVERIFY(block.has_value());
        QCOMPARE(*block, doc.blockAnchorAt(0).value());
    }

    void blockAt_inside_separator_returns_nullopt() {
        MarkoffDocument doc{1};
        QSignalSpy spy(&doc, &MarkoffDocument::parseUpdated);
        doc.resetContent("aa\n\nbb", Origin::TestFixture);
        QVERIFY(spy.wait(2000));
        // Byte 2 is the first '\n' — one past the end of block 0.
        // The blank-line region is [2, 4). blockAt of byte 3 should be nullopt.
        const TextAnchor inSeparator = doc.textAnchorAt(3, /*rightBias*/ false);
        QVERIFY(!doc.blockAt(inSeparator).has_value());
    }

    void blockAt_past_last_block_returns_nullopt() {
        MarkoffDocument doc{1};
        QSignalSpy spy(&doc, &MarkoffDocument::parseUpdated);
        doc.resetContent("only\n", Origin::TestFixture);
        QVERIFY(spy.wait(2000));
        const TextAnchor pastEnd = doc.textAnchorAt(5, /*rightBias*/ true);
        QVERIFY(!doc.blockAt(pastEnd).has_value());
    }

    void offsetInBlock_returns_byte_offset_within_block() {
        MarkoffDocument doc{1};
        QSignalSpy spy(&doc, &MarkoffDocument::parseUpdated);
        doc.resetContent("aaaa\n\nbbbb", Origin::TestFixture);
        QVERIFY(spy.wait(2000));
        const auto block1 = doc.blockAnchorAt(1).value();
        const TextAnchor mid = doc.textAnchorAt(8, /*rightBias*/ false);  // "bb|bb"
        QCOMPARE(doc.offsetInBlock(block1, mid), 2);
    }

    void offsetInBlock_clamps_below_block_to_zero() {
        MarkoffDocument doc{1};
        QSignalSpy spy(&doc, &MarkoffDocument::parseUpdated);
        doc.resetContent("aaaa\n\nbbbb", Origin::TestFixture);
        QVERIFY(spy.wait(2000));
        const auto block1 = doc.blockAnchorAt(1).value();
        const TextAnchor before = doc.textAnchorAt(0, /*rightBias*/ false);
        QCOMPARE(doc.offsetInBlock(block1, before), 0);
    }

    void offsetInBlock_clamps_past_block_to_block_length() {
        MarkoffDocument doc{1};
        QSignalSpy spy(&doc, &MarkoffDocument::parseUpdated);
        doc.resetContent("aaaa\n\nbbbb", Origin::TestFixture);
        QVERIFY(spy.wait(2000));
        const auto block0 = doc.blockAnchorAt(0).value();
        const TextAnchor pastBlock = doc.textAnchorAt(8, /*rightBias*/ false);
        QCOMPARE(doc.offsetInBlock(block0, pastBlock), 4);
    }

    void block_local_textAnchorAt_round_trips_via_offsetInBlock() {
        MarkoffDocument doc{1};
        QSignalSpy spy(&doc, &MarkoffDocument::parseUpdated);
        doc.resetContent("aaaa\n\nbbbb", Origin::TestFixture);
        QVERIFY(spy.wait(2000));
        const auto block1 = doc.blockAnchorAt(1).value();
        const TextAnchor t = doc.textAnchorAt(block1, /*offset*/ 3, /*rightBias*/ false);
        QCOMPARE(doc.offsetInBlock(block1, t), 3);
    }
};

QTEST_MAIN(TstFoundationBlockAnchorQueries)
#include "tst_foundation_block_anchor_queries.moc"
