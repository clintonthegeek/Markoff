// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/core/BlockId.h>
#include <markoff/core/OpaqueBlockRenderer.h>
#include <unordered_set>

class TstBlockId : public QObject {
    Q_OBJECT
private slots:
    void defaultConstructed_isNull();
    void hashable_canKeyQHash();
    void structuralIdentity_usesActorAndCounter();
    void structuralIdentity_rejectsNullComponents();
    void opaqueViewKey_preservesStructuralIdentity();
};

void TstBlockId::defaultConstructed_isNull() { QVERIFY(Markoff::BlockId{}.isNull()); }
void TstBlockId::hashable_canKeyQHash() {
    std::array<uint8_t, 16> actor{};
    actor[0] = 1;
    const auto id = Markoff::BlockId::fromStructuralIdentity(actor, 1);
    QVERIFY(id);
    QHash<Markoff::BlockId, QString> map;
    map.insert(*id, QStringLiteral("one"));
    QCOMPARE(map.value(*id), QStringLiteral("one"));
}
void TstBlockId::structuralIdentity_usesActorAndCounter() {
    std::array<uint8_t, 16> actorA{};
    std::array<uint8_t, 16> actorB{};
    actorA[15] = 1;
    actorB[15] = 2;
    const auto a = Markoff::BlockId::fromStructuralIdentity(actorA, 7);
    const auto same = Markoff::BlockId::fromStructuralIdentity(actorA, 7);
    const auto otherActor = Markoff::BlockId::fromStructuralIdentity(actorB, 7);
    const auto otherCounter = Markoff::BlockId::fromStructuralIdentity(actorA, 8);
    QVERIFY(a && same && otherActor && otherCounter);
    QVERIFY(*a == *same);
    QVERIFY(*a != *otherActor);
    QVERIFY(*a != *otherCounter);

    QHash<Markoff::BlockId, int> map;
    map.insert(*a, 1);
    map.insert(*otherActor, 2);
    QCOMPARE(map.size(), 2);
    QCOMPARE(map.value(*a), 1);
    struct Hash {
        size_t operator()(const Markoff::BlockId &id) const noexcept { return id.identityHash(); }
    };
    std::unordered_set<Markoff::BlockId, Hash> set{*a, *otherActor, *otherCounter};
    QCOMPARE(set.size(), size_t(3));
}
void TstBlockId::structuralIdentity_rejectsNullComponents() {
    std::array<uint8_t, 16> zero{};
    QVERIFY(!Markoff::BlockId::fromStructuralIdentity(zero, 1));
    zero[0] = 1;
    QVERIFY(!Markoff::BlockId::fromStructuralIdentity(zero, 0));
}
void TstBlockId::opaqueViewKey_preservesStructuralIdentity() {
    std::array<uint8_t, 16> actorA{};
    std::array<uint8_t, 16> actorB{};
    actorA[15] = 1;
    actorB[15] = 2;
    const auto a = Markoff::BlockId::fromStructuralIdentity(actorA, 9);
    const auto b = Markoff::BlockId::fromStructuralIdentity(actorB, 9);
    const auto nextCounter = Markoff::BlockId::fromStructuralIdentity(actorA, 10);
    QVERIFY(a && b && nextCounter);
    const QString keyA = Markoff::opaqueBlockIdentityKey(*a);
    const QString keyB = Markoff::opaqueBlockIdentityKey(*b);
    const QString keyNextCounter = Markoff::opaqueBlockIdentityKey(*nextCounter);
    QVERIFY(!keyA.isEmpty());
    QVERIFY(keyA != keyB);
    QVERIFY(keyA != keyNextCounter);
}

QTEST_GUILESS_MAIN(TstBlockId)
#include "tst_block_id.moc"
