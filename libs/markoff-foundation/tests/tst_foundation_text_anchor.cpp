// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff-foundation/TextAnchor.h>
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
        TextAnchor a{1, 42, 0};
        TextAnchor b{1, 42, 0};
        QVERIFY(a == b);
    }

    void differing_replica_id_makes_unequal() {
        TextAnchor a{1, 42, 0};
        TextAnchor b{2, 42, 0};
        QVERIFY(!(a == b));
    }

    void differing_char_value_makes_unequal() {
        TextAnchor a{1, 42, 0};
        TextAnchor b{1, 43, 0};
        QVERIFY(!(a == b));
    }

    void differing_bias_makes_unequal() {
        TextAnchor a{1, 42, 0};
        TextAnchor b{1, 42, 1};
        QVERIFY(!(a == b));
    }

    void roundtrip_basic_left_bias() {
        const CollabText::Crdt::Anchor a{7, 42, CollabText::Crdt::Bias::Left};
        const TextAnchor t = Markoff::Detail::toTextAnchor(a);
        QCOMPARE(t.replicaId, quint16{7});
        QCOMPARE(t.charValue, quint32{42});
        QCOMPARE(t.bias, quint8{0});
        const CollabText::Crdt::Anchor back = Markoff::Detail::toCrdtAnchor(t);
        QCOMPARE(back.replica_id, a.replica_id);
        QCOMPARE(back.char_value, a.char_value);
        QCOMPARE(static_cast<int>(back.bias), static_cast<int>(a.bias));
    }

    void roundtrip_right_bias() {
        const CollabText::Crdt::Anchor a{99, 1234, CollabText::Crdt::Bias::Right};
        const TextAnchor t = Markoff::Detail::toTextAnchor(a);
        QCOMPARE(t.bias, quint8{1});
        const CollabText::Crdt::Anchor back = Markoff::Detail::toCrdtAnchor(t);
        QCOMPARE(static_cast<int>(back.bias), static_cast<int>(CollabText::Crdt::Bias::Right));
    }

    void roundtrip_min_sentinel() {
        const auto a = CollabText::Crdt::Anchor::min();
        const TextAnchor t = Markoff::Detail::toTextAnchor(a);
        const auto back = Markoff::Detail::toCrdtAnchor(t);
        QVERIFY(back.is_min());
    }

    void roundtrip_max_sentinel() {
        const auto a = CollabText::Crdt::Anchor::max();
        const TextAnchor t = Markoff::Detail::toTextAnchor(a);
        const auto back = Markoff::Detail::toCrdtAnchor(t);
        QVERIFY(back.is_max());
    }
};

QTEST_MAIN(TstFoundationTextAnchor)
#include "tst_foundation_text_anchor.moc"
