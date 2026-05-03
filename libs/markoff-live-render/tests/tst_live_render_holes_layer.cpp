// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live-render/BlockHole.h>
#include <markoff-foundation/MarkoffDocument.h>
#include <QtTest/QtTest>

using namespace Markoff::LiveRender;

class TstHolesLayer : public QObject {
    Q_OBJECT

private slots:
    void block_hole_value_type_default_construction() {
        BlockHole h;
        QCOMPARE(h.kind, HoleKind::Paragraph);
        QCOMPARE(h.bufferText, QString());
        QCOMPARE(h.holeId, quint64(0));
    }

    void hole_block_id_disambiguates_from_block_anchor() {
        HoleBlockId h1{42};
        HoleBlockId h2{42};
        HoleBlockId h3{99};
        QCOMPARE(h1.holeId, h2.holeId);
        QVERIFY(h1.holeId != h3.holeId);
    }

    // Tests filled in over Tasks 5–14.
};

QTEST_MAIN(TstHolesLayer)
#include "tst_live_render_holes_layer.moc"
