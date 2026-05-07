// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QJsonObject>

#include <markoff-foundation/AnchorJson.h>
#include <crdt/Anchor.h>

using namespace Markoff;
using CollabText::Crdt::Anchor;
using CollabText::Crdt::Bias;

class TstAnchorJson : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void roundtrip_basic() {
        Anchor a(7, 42, Bias::Left);
        const QJsonObject json = anchorToJson(a);
        const Anchor b = anchorFromJson(json);
        QCOMPARE(b.replica_id, a.replica_id);
        QCOMPARE(b.char_value, a.char_value);
        QCOMPARE(static_cast<int>(b.bias), static_cast<int>(a.bias));
    }

    void roundtrip_right_bias() {
        Anchor a(99, 1234, Bias::Right);
        const QJsonObject json = anchorToJson(a);
        const Anchor b = anchorFromJson(json);
        QCOMPARE(b.replica_id, a.replica_id);
        QCOMPARE(b.char_value, a.char_value);
        QCOMPARE(static_cast<int>(b.bias), static_cast<int>(a.bias));
    }

    void roundtrip_min_sentinel() {
        const Anchor a = Anchor::min();
        const QJsonObject json = anchorToJson(a);
        const Anchor b = anchorFromJson(json);
        QVERIFY(b.is_min());
    }

    void roundtrip_max_sentinel() {
        const Anchor a = Anchor::max();
        const QJsonObject json = anchorToJson(a);
        const Anchor b = anchorFromJson(json);
        QVERIFY(b.is_max());
    }
};

QTEST_MAIN(TstAnchorJson)
#include "tst_anchor_json.moc"
