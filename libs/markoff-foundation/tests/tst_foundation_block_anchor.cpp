// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff-foundation/BlockAnchor.h>
#include <markoff-foundation/TextAnchor.h>

using namespace Markoff;

class TstFoundationBlockAnchor : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void default_constructed_blocks_are_equal() {
        BlockAnchor a;
        BlockAnchor b;
        QVERIFY(a == b);
    }

    void blocks_with_same_raw_id_are_equal() {
        BlockAnchor a = BlockId::fromRaw(42);
        BlockAnchor b = BlockId::fromRaw(42);
        QVERIFY(a == b);
    }

    void blocks_with_different_raw_id_unequal() {
        BlockAnchor a = BlockId::fromRaw(42);
        BlockAnchor b = BlockId::fromRaw(43);
        QVERIFY(!(a == b));
    }

    void block_is_distinct_type_from_text_anchor() {
        // Static-assert at compile time that BlockAnchor and TextAnchor
        // are distinct types — BlockAnchor is now a typedef for BlockId,
        // which is a different type from TextAnchor.
        static_assert(!std::is_same_v<BlockAnchor, TextAnchor>);
    }
};

QTEST_MAIN(TstFoundationBlockAnchor)
#include "tst_foundation_block_anchor.moc"
