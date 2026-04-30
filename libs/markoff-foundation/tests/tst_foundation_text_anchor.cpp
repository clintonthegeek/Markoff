// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff-foundation/TextAnchor.h>

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
};

QTEST_MAIN(TstFoundationTextAnchor)
#include "tst_foundation_text_anchor.moc"
