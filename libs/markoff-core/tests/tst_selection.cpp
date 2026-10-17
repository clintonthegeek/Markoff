// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QColor>

#include <markoff/core/Selection.h>
#include <array>

using namespace Markoff;

// Helper: construct a TextAnchor with explicit field values (replaces old
// aggregate-init now that TextAnchor is a class with private fields).
static ScopedTextPosition pos(quint8 id, const char *token)
{
    std::array<uint8_t, 16> actor{};
    actor[0] = id;
    return {*BlockId::fromStructuralIdentity(actor, 1), QByteArray(token)};
}

class TstSelection : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void empty_when_anchor_equals_active() {
        Selection s;
        s.anchorPosition = pos(1, "same");
        s.activePosition = pos(1, "same");
        QVERIFY(s.isEmpty());
    }

    void not_empty_when_different() {
        Selection s;
        s.anchorPosition = pos(1, "a");
        s.activePosition = pos(1, "b");
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
        s.anchorPosition = pos(2, "a");
        s.activePosition = pos(2, "b");
        s.kind = Selection::Kind::Primary;
        const QJsonObject json = s.toJson();
        const Selection r = Selection::fromJson(json);
        QCOMPARE(r.anchorPosition, s.anchorPosition);
        QCOMPARE(r.activePosition, s.activePosition);
        QVERIFY(!json.contains("anchor"));
        QCOMPARE(r.kind, s.kind);
    }

    void json_roundtrip_presence() {
        Selection s;
        s.anchorPosition = pos(7, "presence");
        s.activePosition = pos(7, "presence");
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

    void legacy_anchor_json_is_not_accepted() {
        const QJsonObject legacy{
            {"anchor", QJsonObject{{"version", 1}, {"replica", "1"},
                                    {"character", "12"}}},
            {"active", QJsonObject{{"version", 1}, {"replica", "1"},
                                    {"character", "12"}}},
        };
        const Selection restored = Selection::fromJson(legacy);
        QVERIFY(!restored.anchorPosition);
        QVERIFY(!restored.activePosition);
        QVERIFY(restored.isEmpty());
        const QJsonObject encoded = restored.toJson();
        QVERIFY(!encoded.contains("anchor"));
        QVERIFY(!encoded.contains("active"));
    }
};

QTEST_MAIN(TstSelection)
#include "tst_selection.moc"
