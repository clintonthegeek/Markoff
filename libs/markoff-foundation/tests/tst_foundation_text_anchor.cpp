// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>
#include <markoff-foundation/Origin.h>
#include <markoff-foundation/TextAnchor.h>
#include <markoff-foundation/BlockId.h>
#include <crdt/Anchor.h>
#include "AnchorConversion.h"  // tests reach into foundation src/ for internals

using namespace Markoff;

class TstFoundationTextAnchor : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void default_constructed_anchors_are_equal() {
        TextAnchor a;
        TextAnchor b;
        QVERIFY(a == b);
    }

    void anchors_with_same_fields_are_equal() {
        TextAnchor a = TextAnchor::make(BlockId{}, 1, 42, false);
        TextAnchor b = TextAnchor::make(BlockId{}, 1, 42, false);
        QVERIFY(a == b);
    }

    void differing_replica_id_makes_unequal() {
        TextAnchor a = TextAnchor::make(BlockId{}, 1, 42, false);
        TextAnchor b = TextAnchor::make(BlockId{}, 2, 42, false);
        QVERIFY(!(a == b));
    }

    void differing_char_value_makes_unequal() {
        TextAnchor a = TextAnchor::make(BlockId{}, 1, 42, false);
        TextAnchor b = TextAnchor::make(BlockId{}, 1, 43, false);
        QVERIFY(!(a == b));
    }

    void differing_bias_makes_unequal() {
        TextAnchor a = TextAnchor::make(BlockId{}, 1, 42, false);
        TextAnchor b = TextAnchor::make(BlockId{}, 1, 42, true);
        QVERIFY(!(a == b));
    }

    void roundtrip_basic_left_bias() {
        const CollabText::Crdt::Anchor a{7, 42, CollabText::Crdt::Bias::Left};
        const TextAnchor t = Markoff::Detail::toTextAnchor(BlockId{}, a);
        QCOMPARE(t.replicaId(), uint16_t{7});
        QCOMPARE(t.charValue(), uint64_t{42});
        QVERIFY(!t.rightBias());
        const CollabText::Crdt::Anchor back = Markoff::Detail::toCrdtAnchor(t);
        QCOMPARE(back.replica_id, a.replica_id);
        QCOMPARE(back.char_value, a.char_value);
        QCOMPARE(static_cast<int>(back.bias), static_cast<int>(a.bias));
    }

    void roundtrip_right_bias() {
        const CollabText::Crdt::Anchor a{99, 1234, CollabText::Crdt::Bias::Right};
        const TextAnchor t = Markoff::Detail::toTextAnchor(BlockId{}, a);
        QVERIFY(t.rightBias());
        const CollabText::Crdt::Anchor back = Markoff::Detail::toCrdtAnchor(t);
        QCOMPARE(static_cast<int>(back.bias), static_cast<int>(CollabText::Crdt::Bias::Right));
    }

    void roundtrip_min_sentinel() {
        const auto a = CollabText::Crdt::Anchor::min();
        const TextAnchor t = Markoff::Detail::toTextAnchor(BlockId{}, a);
        const auto back = Markoff::Detail::toCrdtAnchor(t);
        QVERIFY(back.is_min());
    }

    void roundtrip_max_sentinel() {
        const auto a = CollabText::Crdt::Anchor::max();
        const TextAnchor t = Markoff::Detail::toTextAnchor(BlockId{}, a);
        const auto back = Markoff::Detail::toCrdtAnchor(t);
        QVERIFY(back.is_max());
    }

    void document_textAnchorAt_resolves_back_to_same_byte_with_left_bias() {
        MarkoffDocument doc{1};
        doc.resetContent("hello world", Origin::TestFixture);
        const TextAnchor t = doc.textAnchorAt(6, /*rightBias*/ false);
        QCOMPARE(doc.resolveTextAnchor(t), quint32{6});
    }

    void document_textAnchorAt_resolves_back_to_same_byte_with_right_bias() {
        MarkoffDocument doc{1};
        doc.resetContent("hello world", Origin::TestFixture);
        const TextAnchor t = doc.textAnchorAt(6, /*rightBias*/ true);
        QCOMPARE(doc.resolveTextAnchor(t), quint32{6});
    }

    void document_textAnchorAt_left_bias_survives_insert_before() {
        MarkoffDocument doc{1};
        doc.resetContent("hello world", Origin::TestFixture);
        const TextAnchor t = doc.textAnchorAt(6, /*rightBias*/ false);
        // Insert "X" at position 0 — anchor at byte 6 should now resolve to 7.
        MarkoffEdit e; e.oldStart = 0; e.oldEnd = 0; e.newText = "X";
        doc.applyLocalEdit({e});
        QCOMPARE(doc.resolveTextAnchor(t), quint32{7});
    }
};

QTEST_MAIN(TstFoundationTextAnchor)
#include "tst_foundation_text_anchor.moc"
