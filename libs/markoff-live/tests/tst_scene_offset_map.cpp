// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QtTest>
#include <QSignalSpy>
#include <markoff/Editor.h>
#include <markoff/MarkoffDocument.h>
#include "SceneCoordinator.h"   // private src header (via include path in CMakeLists)

using namespace Markoff;

class TstSceneOffsetMap : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void itemMap_coversAllBlocks_contiguously();
    void findItemIndexForOffset_locatesBlocks();
    void shiftItemsAfter_shiftsSubsequent();
};

/// Wait for the parseUpdated signal from a freshly loaded document.
static bool waitForParse(MarkoffDocument &doc, const QString &md, int timeoutMs = 2000)
{
    QSignalSpy spy(&doc, &MarkoffDocument::parseUpdated);
    doc.resetContent(md, Origin::FirstOpen);
    return spy.wait(timeoutMs);
}

/// All items' ranges must cover the full canonical buffer contiguously.
/// Contiguous means: first item starts at 0, each item's canonicalEnd equals
/// the next item's canonicalStart, and the last item's canonicalEnd equals
/// the full toMarkdown() length.
void TstSceneOffsetMap::itemMap_coversAllBlocks_contiguously()
{
    // Two text segments separated by an image so we have multiple items.
    const QString md = QStringLiteral("# Heading\n\n![](img.png)\n\nparagraph");

    MarkoffDocument doc;
    Editor ed;
    ed.setDocument(&doc);
    QVERIFY(waitForParse(doc, md));

    auto *coord = ed.coordinatorForTesting();
    QVERIFY(coord != nullptr);

    const auto &map = coord->itemMap();
    QVERIFY(!map.isEmpty());

    // First item must start at 0.
    QCOMPARE(map.first().canonicalStart, 0);

    // Contiguous: each canonicalEnd == next canonicalStart.
    for (int i = 1; i < map.size(); ++i) {
        QCOMPARE(map[i - 1].canonicalEnd, map[i].canonicalStart);
    }

    // Total coverage: last canonicalEnd must equal the toMarkdown() length.
    // toMarkdown() == md when round-trip holds.
    QCOMPARE(map.back().canonicalEnd, coord->toMarkdown().length());
}

/// findItemIndexForOffset must return the correct item index for several
/// offsets spread across the document.
void TstSceneOffsetMap::findItemIndexForOffset_locatesBlocks()
{
    const QString md = QStringLiteral("para1\n\n![](x.png)\n\npara2");

    MarkoffDocument doc;
    Editor ed;
    ed.setDocument(&doc);
    QVERIFY(waitForParse(doc, md));

    auto *coord = ed.coordinatorForTesting();
    QVERIFY(coord != nullptr);

    const auto &map = coord->itemMap();
    QVERIFY(map.size() >= 2);

    // Offset 0 → first item.
    QCOMPARE(coord->findItemIndexForOffset(0), 0);

    // Offset inside first item.
    if (map.first().canonicalEnd > map.first().canonicalStart + 1)
        QCOMPARE(coord->findItemIndexForOffset(map.first().canonicalStart + 1), 0);

    // Offset == last item's canonicalEnd → last item (end-of-buffer sentinel).
    QCOMPARE(coord->findItemIndexForOffset(map.back().canonicalEnd),
             map.size() - 1);

    // Each item's canonicalStart maps back to itself.
    for (int i = 0; i < map.size(); ++i)
        QCOMPARE(coord->findItemIndexForOffset(map[i].canonicalStart), i);

    // Offset one past canonicalEnd of the last item → -1 (out of range).
    QCOMPARE(coord->findItemIndexForOffset(map.back().canonicalEnd + 1), -1);
}

/// shiftItemsAfter(k, delta) must shift items k+1 .. end by delta, leaving
/// items 0 .. k unchanged.
void TstSceneOffsetMap::shiftItemsAfter_shiftsSubsequent()
{
    const QString md = QStringLiteral("a\n\n![](x.png)\n\nb");

    MarkoffDocument doc;
    Editor ed;
    ed.setDocument(&doc);
    QVERIFY(waitForParse(doc, md));

    auto *coord = ed.coordinatorForTesting();
    QVERIFY(coord != nullptr);

    // Need at least 2 items to test shifting.
    QVERIFY(coord->itemMap().size() >= 2);

    // Snapshot before the shift.
    const QVector<SceneCoordinator::ItemEntry> before = coord->itemMap();

    const int shiftBy = 5;
    coord->shiftItemsAfter(0, shiftBy);

    const auto &after = coord->itemMap();
    QCOMPARE(after.size(), before.size());

    // Item 0 must be unchanged.
    QCOMPARE(after[0].canonicalStart, before[0].canonicalStart);
    QCOMPARE(after[0].canonicalEnd,   before[0].canonicalEnd);

    // All subsequent items must be shifted by exactly shiftBy.
    for (int i = 1; i < before.size(); ++i) {
        QCOMPARE(after[i].canonicalStart, before[i].canonicalStart + shiftBy);
        QCOMPARE(after[i].canonicalEnd,   before[i].canonicalEnd   + shiftBy);
    }
}

QTEST_MAIN(TstSceneOffsetMap)
#include "tst_scene_offset_map.moc"
