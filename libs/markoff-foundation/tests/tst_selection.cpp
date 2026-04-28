// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QColor>

#include <markoff-foundation/Selection.h>
#include <crdt/Anchor.h>

using namespace Markoff;
using CollabText::Crdt::Anchor;
using CollabText::Crdt::Bias;

class TstSelection : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void empty_when_anchor_equals_active() {
        Selection s;
        s.anchor = Anchor(1, 10, Bias::Left);
        s.active = Anchor(1, 10, Bias::Left);
        QVERIFY(s.isEmpty());
    }

    void not_empty_when_different() {
        Selection s;
        s.anchor = Anchor(1, 10, Bias::Left);
        s.active = Anchor(1, 12, Bias::Right);
        QVERIFY(!s.isEmpty());
    }

    void default_kind_is_primary() {
        Selection s;
        QCOMPARE(s.kind, Selection::Kind::Primary);
    }

    void presence_carries_metadata() {
        Selection s;
        s.kind = Selection::Kind::Presence;
        s.participantId = QStringLiteral("alice");
        s.participantLabel = QStringLiteral("Alice");
        s.presenceColor = QColor(Qt::magenta);
        s.cursorVersion = 42;
        QCOMPARE(s.kind, Selection::Kind::Presence);
        QCOMPARE(s.participantId, QStringLiteral("alice"));
        QCOMPARE(s.cursorVersion, quint64(42));
    }

    void json_roundtrip_primary() {
        Selection s;
        s.anchor = Anchor(2, 100, Bias::Left);
        s.active = Anchor(2, 105, Bias::Right);
        s.kind = Selection::Kind::Primary;
        const QJsonObject json = s.toJson();
        const Selection r = Selection::fromJson(json);
        QCOMPARE(r.anchor.replica_id, s.anchor.replica_id);
        QCOMPARE(r.anchor.char_value, s.anchor.char_value);
        QCOMPARE(r.active.replica_id, s.active.replica_id);
        QCOMPARE(r.active.char_value, s.active.char_value);
        QCOMPARE(r.kind, s.kind);
    }

    void json_roundtrip_presence() {
        Selection s;
        s.anchor = Anchor(7, 50, Bias::Left);
        s.active = Anchor(7, 50, Bias::Left);
        s.kind = Selection::Kind::Presence;
        s.participantId = QStringLiteral("bob");
        s.participantLabel = QStringLiteral("Bob");
        s.presenceColor = QColor(0, 255, 0);
        s.cursorVersion = 123;
        const QJsonObject json = s.toJson();
        const Selection r = Selection::fromJson(json);
        QCOMPARE(r.kind, Selection::Kind::Presence);
        QCOMPARE(r.participantId, s.participantId);
        QCOMPARE(r.participantLabel, s.participantLabel);
        QCOMPARE(r.presenceColor.name(), s.presenceColor.name());
        QCOMPARE(r.cursorVersion, s.cursorVersion);
    }
};

QTEST_MAIN(TstSelection)
#include "tst_selection.moc"
