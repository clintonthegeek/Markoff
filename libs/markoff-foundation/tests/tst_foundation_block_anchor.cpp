// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff-foundation/BlockAnchor.h>

using namespace Markoff;

class TstFoundationBlockAnchor : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void default_constructed_blocks_are_equal() {
        BlockAnchor a;
        BlockAnchor b;
        QVERIFY(a == b);
    }

    void blocks_with_same_first_byte_are_equal() {
        BlockAnchor a{TextAnchor{1, 42, 0}};
        BlockAnchor b{TextAnchor{1, 42, 0}};
        QVERIFY(a == b);
    }

    void blocks_with_different_first_byte_unequal() {
        BlockAnchor a{TextAnchor{1, 42, 0}};
        BlockAnchor b{TextAnchor{1, 43, 0}};
        QVERIFY(!(a == b));
    }

    void block_is_distinct_type_from_text_anchor() {
        // Static-assert at compile time that BlockAnchor and TextAnchor
        // are distinct types — protects against the "alias" alternative
        // we explicitly rejected in the spec §10 decision 1.
        static_assert(!std::is_same_v<BlockAnchor, TextAnchor>);
    }
};

QTEST_MAIN(TstFoundationBlockAnchor)
#include "tst_foundation_block_anchor.moc"
