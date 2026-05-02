// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>

#include <markoff-foundation/BlockAnchor.h>
#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>
#include <markoff-parser/Document.h>

using namespace Markoff;

class TstFoundationBlockAnchorCompute : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void parse_updated_payload_carries_block_anchors_for_three_paragraphs() {
        MarkoffDocument doc{1};
        QSignalSpy spy(&doc, &MarkoffDocument::parseUpdated);
        doc.resetContent("a\n\nb\n\nc", Origin::TestFixture);
        QVERIFY(spy.wait(2000));

        // Signal signature: (Document*, quint64 parseSeq, QList<BlockAnchor>, quint64 inputEditSeq).
        const auto &args = spy.last();
        QCOMPARE(args.size(), 4);
        const auto anchors = args.at(2).value<QList<BlockAnchor>>();
        QCOMPARE(anchors.size(), 3);
        // Each anchor's firstByte resolves to the corresponding block's
        // start byte (0, 3, 6 in the body "a\n\nb\n\nc").
        QCOMPARE(doc.resolveTextAnchor(anchors[0].firstByte), quint32{0});
        QCOMPARE(doc.resolveTextAnchor(anchors[1].firstByte), quint32{3});
        QCOMPARE(doc.resolveTextAnchor(anchors[2].firstByte), quint32{6});
    }

    void anchors_align_with_parsed_top_level_blocks_for_mixed_kinds() {
        // The bug fix this test guards: foundation BlockAnchors must be
        // 1:1 with parsed->topLevelBlocks() so view-qml's records[i]
        // and the foundation's blockAnchors[i] describe the same block.
        // Mixed kinds (heading, paragraph, list, blockquote, code-block,
        // hr) are precisely the cases the old regex scanner classified
        // differently from tree-sitter — exercising them catches any
        // future drift between the two enumerations.
        MarkoffDocument doc{1};
        QSignalSpy spy(&doc, &MarkoffDocument::parseUpdated);
        const QByteArray src =
            "# Heading\n\n"
            "A paragraph.\n\n"
            "- list item one\n- list item two\n\n"
            "> a blockquote\n\n"
            "```\ncode line\n```\n\n"
            "---\n\n"
            "Trailing paragraph.\n";
        doc.resetContent(src, Origin::TestFixture);
        QVERIFY(spy.wait(2000));

        const auto *parsed = doc.parsedDocument();
        QVERIFY(parsed);
        const auto blocks  = parsed->topLevelBlocks();
        const auto anchors = spy.last().at(2).value<QList<BlockAnchor>>();

        // Same count (the fix's load-bearing invariant).
        QCOMPARE(anchors.size(), blocks.size());
        QVERIFY(anchors.size() >= 6);

        // Each anchor resolves to its block's start byte in source
        // coordinates. No frontmatter here, so body bytes == source bytes.
        for (qsizetype i = 0; i < blocks.size(); ++i) {
            const quint32 resolved = doc.resolveTextAnchor(anchors[i].firstByte);
            QCOMPARE(resolved, static_cast<quint32>(blocks[i].byteStart));
        }
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
