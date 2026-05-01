// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QColor>

#include <markoff-foundation/Selection.h>
#include <markoff-foundation/TextAnchor.h>

using namespace Markoff;

class TstSelection : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void empty_when_anchor_equals_active() {
        Selection s;
        s.anchor = TextAnchor{1, 10, 0};
        s.active = TextAnchor{1, 10, 0};
        QVERIFY(s.isEmpty());
    }

    void not_empty_when_different() {
        Selection s;
        s.anchor = TextAnchor{1, 10, 0};
        s.active = TextAnchor{1, 12, 1};
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
        s.anchor = TextAnchor{2, 100, 0};
        s.active = TextAnchor{2, 105, 1};
        s.kind = Selection::Kind::Primary;
        const QJsonObject json = s.toJson();
        const Selection r = Selection::fromJson(json);
        QCOMPARE(r.anchor.replicaId, s.anchor.replicaId);
        QCOMPARE(r.anchor.charValue, s.anchor.charValue);
        QCOMPARE(r.active.replicaId, s.active.replicaId);
        QCOMPARE(r.active.charValue, s.active.charValue);
        QCOMPARE(r.kind, s.kind);
    }

    void json_roundtrip_presence() {
        Selection s;
        s.anchor = TextAnchor{7, 50, 0};
        s.active = TextAnchor{7, 50, 0};
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
