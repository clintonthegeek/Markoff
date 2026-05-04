// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff-foundation/KindTagMap.h>
#include <markoff-foundation/BlockAttrsMap.h>
#include <markoff-foundation/FrontmatterMap.h>
#include <markoff-foundation/LinkRefMap.h>
#include <markoff-foundation/FootnoteDefMap.h>
#include <markoff-foundation/BlockId.h>
#include <markoff-foundation/BlockKind.h>

using namespace Markoff;

class TstD2SiblingMaps : public QObject {
    Q_OBJECT
private Q_SLOTS:
    // KindTagMap
    void kindTagMap_setAndGet();
    void kindTagMap_latterStampWins();

    // BlockAttrsMap
    void blockAttrsMap_setAndGet();
    void blockAttrsMap_differentBlocksDontCollide();

    // FrontmatterMap
    void frontmatterMap_setAndGet();

    // LinkRefMap
    void linkRefMap_setAndGet();

    // FootnoteDefMap
    void footnoteDefMap_setAndGet();
};

void TstD2SiblingMaps::kindTagMap_setAndGet()
{
    KindTagMap m(/*replicaId=*/1);
    BlockId id = BlockId::fromRaw(42);
    m.setWithNextStamp(id, BlockKind::Heading);
    QCOMPARE(m.get(id).value(), BlockKind::Heading);
}

void TstD2SiblingMaps::kindTagMap_latterStampWins()
{
    // Two replicas set conflicting kinds; the one with the later stamp wins.
    KindTagMap m(/*replicaId=*/1);
    BlockId id = BlockId::fromRaw(1);
    CausalStamp early{1, 10};
    CausalStamp late{2, 20};
    m.set(id, BlockKind::Paragraph, early);
    m.set(id, BlockKind::Heading,   late);
    QCOMPARE(m.get(id).value(), BlockKind::Heading);
    // Reversed order — later stamp should still win
    KindTagMap m2(1);
    m2.set(id, BlockKind::Heading,   late);
    m2.set(id, BlockKind::Paragraph, early);
    QCOMPARE(m2.get(id).value(), BlockKind::Heading);
}

void TstD2SiblingMaps::blockAttrsMap_setAndGet()
{
    BlockAttrsMap m(1);
    BlockId id = BlockId::fromRaw(7);
    BlockAttrKey key{id, "level"};
    m.setWithNextStamp(key, AttrValue{1});
    auto val = m.get(key);
    QVERIFY(val.has_value());
    QCOMPARE(std::get<int>(*val), 1);
}

void TstD2SiblingMaps::blockAttrsMap_differentBlocksDontCollide()
{
    BlockAttrsMap m(1);
    BlockId idA = BlockId::fromRaw(1);
    BlockId idB = BlockId::fromRaw(2);
    m.setWithNextStamp({idA, "level"}, AttrValue{1});
    m.setWithNextStamp({idB, "level"}, AttrValue{2});
    QCOMPARE(std::get<int>(*m.get({idA, "level"})), 1);
    QCOMPARE(std::get<int>(*m.get({idB, "level"})), 2);
}

void TstD2SiblingMaps::frontmatterMap_setAndGet()
{
    FrontmatterMap m(1);
    m.setWithNextStamp("title", "Hello World");
    QCOMPARE(m.get("title").value(), QByteArray("Hello World"));
}

void TstD2SiblingMaps::linkRefMap_setAndGet()
{
    LinkRefMap m(1);
    m.setWithNextStamp("ref1", LinkRefValue{"https://example.com", "Example"});
    auto v = m.get("ref1");
    QVERIFY(v.has_value());
    QCOMPARE(v->url, QString("https://example.com"));
}

void TstD2SiblingMaps::footnoteDefMap_setAndGet()
{
    FootnoteDefMap m(1);
    m.setWithNextStamp("fn1", "Footnote content here.");
    QCOMPARE(m.get("fn1").value(), QByteArray("Footnote content here."));
}

QTEST_GUILESS_MAIN(TstD2SiblingMaps)
#include "tst_d2_sibling_maps.moc"
