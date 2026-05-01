// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>

using namespace Markoff;

class TstFoundationBlockAnchorStability : public QObject {
    Q_OBJECT

    /// Returns the QList<BlockAnchor> from the most recent parseUpdated.
    static QList<BlockAnchor> latestAnchors(QSignalSpy &spy)
    {
        return spy.last().at(2).value<QList<BlockAnchor>>();
    }

    /// Apply an edit and wait for the parse to return.
    static void apply(MarkoffDocument &doc, QSignalSpy &spy,
                      quint32 oldStart, quint32 oldEnd, const QByteArray &newText)
    {
        spy.clear();
        MarkoffEdit e; e.oldStart = oldStart; e.oldEnd = oldEnd; e.newText = newText;
        doc.applyLocalEdit({e});
        QVERIFY(spy.wait(2000));
    }

    /// Set up a doc with three paragraphs: "p1\n\np2\n\np3".
    static void setupThreeParagraphs(MarkoffDocument &doc, QSignalSpy &spy)
    {
        doc.resetContent("p1\n\np2\n\np3", Origin::TestFixture);
        QVERIFY(spy.wait(2000));
    }

private Q_SLOTS:
    void edit_within_block_preserves_all_anchors() {
        MarkoffDocument doc{1};
        QSignalSpy spy(&doc, &MarkoffDocument::parseUpdated);
        setupThreeParagraphs(doc, spy);
        const auto before = latestAnchors(spy);
        QCOMPARE(before.size(), 3);

        // Insert "X" inside p2 (byte 5 is between "p" and "2").
        apply(doc, spy, 5, 5, "X");
        const auto after = latestAnchors(spy);
        QCOMPARE(after.size(), 3);
        QCOMPARE(after[0], before[0]);
        QCOMPARE(after[1], before[1]);
        QCOMPARE(after[2], before[2]);
    }

    void split_preserves_upper_half_introduces_new_lower() {
        MarkoffDocument doc{1};
        QSignalSpy spy(&doc, &MarkoffDocument::parseUpdated);
        doc.resetContent("aaaa", Origin::TestFixture);
        QVERIFY(spy.wait(2000));
        const auto before = latestAnchors(spy);
        QCOMPARE(before.size(), 1);
        const BlockAnchor upper = before[0];

        // Split "aaaa" → "aa\n\naa" by inserting "\n\n" at byte 2.
        apply(doc, spy, 2, 2, "\n\n");
        const auto after = latestAnchors(spy);
        QCOMPARE(after.size(), 2);
        QCOMPARE(after[0], upper);
        QVERIFY(after[1] != upper);
    }

    void merge_preserves_surviving_block_anchor() {
        MarkoffDocument doc{1};
        QSignalSpy spy(&doc, &MarkoffDocument::parseUpdated);
        doc.resetContent("aa\n\nbb", Origin::TestFixture);
        QVERIFY(spy.wait(2000));
        const auto before = latestAnchors(spy);
        QCOMPARE(before.size(), 2);
        const BlockAnchor first = before[0];

        // Merge: delete the "\n\n" separator (bytes 2..4).
        apply(doc, spy, 2, 4, "");
        const auto after = latestAnchors(spy);
        QCOMPARE(after.size(), 1);
        QCOMPARE(after[0], first);
    }

    void add_block_at_top_preserves_existing() {
        MarkoffDocument doc{1};
        QSignalSpy spy(&doc, &MarkoffDocument::parseUpdated);
        doc.resetContent("middle\n\nlast", Origin::TestFixture);
        QVERIFY(spy.wait(2000));
        const auto before = latestAnchors(spy);
        QCOMPARE(before.size(), 2);

        apply(doc, spy, 0, 0, "first\n\n");
        const auto after = latestAnchors(spy);
        QCOMPARE(after.size(), 3);
        QCOMPARE(after[1], before[0]);
        QCOMPARE(after[2], before[1]);
    }

    void add_block_at_bottom_preserves_existing() {
        MarkoffDocument doc{1};
        QSignalSpy spy(&doc, &MarkoffDocument::parseUpdated);
        doc.resetContent("first\n\nmiddle", Origin::TestFixture);
        QVERIFY(spy.wait(2000));
        const auto before = latestAnchors(spy);
        QCOMPARE(before.size(), 2);

        apply(doc, spy, 13, 13, "\n\nlast");
        const auto after = latestAnchors(spy);
        QCOMPARE(after.size(), 3);
        QCOMPARE(after[0], before[0]);
        QCOMPARE(after[1], before[1]);
    }

    void delete_entire_block_preserves_neighbours() {
        MarkoffDocument doc{1};
        QSignalSpy spy(&doc, &MarkoffDocument::parseUpdated);
        setupThreeParagraphs(doc, spy);
        const auto before = latestAnchors(spy);
        QCOMPARE(before.size(), 3);

        // Delete p2 plus its trailing separator (bytes 4..8 = "p2\n\n").
        apply(doc, spy, 4, 8, "");
        const auto after = latestAnchors(spy);
        QCOMPARE(after.size(), 2);
        QCOMPARE(after[0], before[0]);
        QCOMPARE(after[1], before[2]);
    }

    void delete_first_byte_of_block_orphans_old_anchor() {
        MarkoffDocument doc{1};
        QSignalSpy spy(&doc, &MarkoffDocument::parseUpdated);
        doc.resetContent("aaaa\n\nbbbb", Origin::TestFixture);
        QVERIFY(spy.wait(2000));
        const auto before = latestAnchors(spy);
        QCOMPARE(before.size(), 2);
        const BlockAnchor secondBlockOrig = before[1];

        // Delete the first 'b' of block 2 (byte 6).
        apply(doc, spy, 6, 7, "");
        const auto after = latestAnchors(spy);
        QCOMPARE(after.size(), 2);
        QCOMPARE(after[0], before[0]);
        // The old anchor for the second block was at the deleted char's
        // position; the new anchor for the second block is at a new
        // (still-present) char. They should differ.
        QVERIFY(after[1] != secondBlockOrig);
    }
};

QTEST_MAIN(TstFoundationBlockAnchorStability)
#include "tst_foundation_block_anchor_stability.moc"
