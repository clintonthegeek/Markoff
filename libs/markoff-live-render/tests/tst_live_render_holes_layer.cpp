// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live-render/BlockHole.h>
#include <markoff/live-render/LiveHoleLayer.h>
#include <markoff-foundation/MarkoffDocument.h>
#include <QtTest/QtTest>
#include <QSignalSpy>

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

    void layer_create_emits_hole_inserted_and_assigns_id() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        doc.resetContent("hello", Markoff::Origin::FirstOpen);

        Markoff::LiveRender::LiveHoleLayer layer(&doc, /*blockModel=*/nullptr,
                                                 /*undoCoalescer=*/nullptr);
        QSignalSpy spy(&layer, &Markoff::LiveRender::LiveHoleLayer::holeInserted);

        Markoff::TextAnchor anchor = doc.textAnchorAt(5, /*rightBias=*/false);
        quint64 id = layer.createBlockHole(HoleKind::Paragraph, anchor);

        QVERIFY(id != 0);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toULongLong(), id);
        QCOMPARE(layer.holeCount(), 1);
        QCOMPARE(layer.bufferText(id), QString());
        QVERIFY(layer.exists(id));
        QCOMPARE(layer.kind(id), HoleKind::Paragraph);
    }

    void layer_setBuffer_emits_buffer_changed() {
        Markoff::MarkoffDocument doc(1);
        doc.resetContent("hello", Markoff::Origin::FirstOpen);

        Markoff::LiveRender::LiveHoleLayer layer(&doc, nullptr, nullptr);
        quint64 id = layer.createBlockHole(HoleKind::Paragraph,
                                            doc.textAnchorAt(5, false));

        QSignalSpy spy(&layer, &Markoff::LiveRender::LiveHoleLayer::holeBufferChanged);
        layer.setBlockHoleBuffer(id, "world");
        QCOMPARE(spy.count(), 1);
        QCOMPARE(layer.bufferText(id), QString("world"));
    }

    void layer_abandon_drops_hole_no_source_mutation() {
        Markoff::MarkoffDocument doc(1);
        doc.resetContent("hello", Markoff::Origin::FirstOpen);

        Markoff::LiveRender::LiveHoleLayer layer(&doc, nullptr, nullptr);
        quint64 id = layer.createBlockHole(HoleKind::Paragraph,
                                            doc.textAnchorAt(5, false));
        layer.setBlockHoleBuffer(id, "buffered");

        QSignalSpy spy(&layer, &Markoff::LiveRender::LiveHoleLayer::holeAbandoned);
        layer.abandonBlockHole(id);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(layer.holeCount(), 0);
        QVERIFY(!layer.exists(id));

        // CRITICAL: no source mutation — F5 mitigation.
        QCOMPARE(doc.toMarkdown(), QString("hello"));
    }

    // Tests filled in over Tasks 6–14.
};

QTEST_MAIN(TstHolesLayer)
#include "tst_live_render_holes_layer.moc"
