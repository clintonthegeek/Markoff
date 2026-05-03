// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live-render/BlockHole.h>
#include <markoff/live-render/LiveHoleLayer.h>
#include <markoff/live-render/LiveEditBinding.h>
#include <markoff/live-render/LiveListModelBinding.h>
#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/Origin.h>
#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>

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

    void layer_idle_timer_starts_on_setBuffer() {
        Markoff::MarkoffDocument doc(1);
        doc.resetContent("hello", Markoff::Origin::FirstOpen);

        Markoff::LiveRender::LiveHoleLayer layer(&doc, nullptr, nullptr);
        QSignalSpy idleSpy(&layer, SIGNAL(idleCommitDue(quint64)));
        quint64 id = layer.createBlockHole(HoleKind::Paragraph,
                                            doc.textAnchorAt(5, false));
        layer.setBlockHoleBuffer(id, "x");

        QVERIFY(idleSpy.wait(400));
        QCOMPARE(idleSpy.count(), 1);
        QCOMPARE(idleSpy.first().at(0).toULongLong(), id);
    }

    void layer_idle_timer_restarts_on_each_setBuffer() {
        Markoff::MarkoffDocument doc(1);
        doc.resetContent("hello", Markoff::Origin::FirstOpen);

        Markoff::LiveRender::LiveHoleLayer layer(&doc, nullptr, nullptr);
        QSignalSpy idleSpy(&layer, SIGNAL(idleCommitDue(quint64)));
        quint64 id = layer.createBlockHole(HoleKind::Paragraph,
                                            doc.textAnchorAt(5, false));

        layer.setBlockHoleBuffer(id, "a");
        QTest::qWait(150);
        QCOMPARE(idleSpy.count(), 0);
        layer.setBlockHoleBuffer(id, "ab");
        QTest::qWait(150);
        QCOMPARE(idleSpy.count(), 0);
        QVERIFY(idleSpy.wait(200));
        QCOMPARE(idleSpy.count(), 1);
    }

    void layer_idle_timer_paused_during_composition() {
        Markoff::MarkoffDocument doc(1);
        doc.resetContent("hello", Markoff::Origin::FirstOpen);

        Markoff::LiveRender::LiveHoleLayer layer(&doc, nullptr, nullptr);
        QSignalSpy idleSpy(&layer, SIGNAL(idleCommitDue(quint64)));
        quint64 id = layer.createBlockHole(HoleKind::Paragraph,
                                            doc.textAnchorAt(5, false));

        layer.setHoleComposition(id, true);
        layer.setBlockHoleBuffer(id, "preedit");
        QTest::qWait(400);
        QCOMPARE(idleSpy.count(), 0);

        layer.setHoleComposition(id, false);
        QVERIFY(idleSpy.wait(400));
        QCOMPARE(idleSpy.count(), 1);
    }

    void layer_idle_does_not_fire_for_empty_buffer() {
        Markoff::MarkoffDocument doc(1);
        doc.resetContent("hello", Markoff::Origin::FirstOpen);

        Markoff::LiveRender::LiveHoleLayer layer(&doc, nullptr, nullptr);
        QSignalSpy idleSpy(&layer, SIGNAL(idleCommitDue(quint64)));
        // Hole created but bufferText left empty — timer must not fire.
        (void)layer.createBlockHole(HoleKind::Paragraph,
                                    doc.textAnchorAt(5, false));
        QTest::qWait(400);
        QCOMPARE(idleSpy.count(), 0);
    }

    void layer_commit_applies_local_edit_and_drops_hole() {
        Markoff::MarkoffDocument doc(1);
        doc.resetContent("hello", Markoff::Origin::FirstOpen);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        QVERIFY(parseSpy.wait(2000));

        Markoff::LiveRender::LiveHoleLayer layer(&doc, nullptr, nullptr);
        QSignalSpy reifiedSpy(&layer, SIGNAL(holeReified(quint64, Markoff::TextAnchor)));
        QSignalSpy abandonedSpy(&layer, SIGNAL(holeAbandoned(quint64)));

        quint64 id = layer.createBlockHole(HoleKind::Paragraph,
                                            doc.textAnchorAt(5, false));
        layer.setBlockHoleBuffer(id, "world");
        layer.commitBlockHole(id);

        QCOMPARE(layer.holeCount(), 0);
        QCOMPARE(doc.toMarkdown(), QString("hello\n\nworld"));
        QCOMPARE(abandonedSpy.count(), 1);
        QCOMPARE(reifiedSpy.count(), 1);
    }

    void layer_commit_with_empty_buffer_is_abandon() {
        Markoff::MarkoffDocument doc(1);
        doc.resetContent("hello", Markoff::Origin::FirstOpen);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        QVERIFY(parseSpy.wait(2000));

        Markoff::LiveRender::LiveHoleLayer layer(&doc, nullptr, nullptr);
        QSignalSpy reifiedSpy(&layer, SIGNAL(holeReified(quint64, Markoff::TextAnchor)));
        QSignalSpy abandonedSpy(&layer, SIGNAL(holeAbandoned(quint64)));

        quint64 id = layer.createBlockHole(HoleKind::Paragraph,
                                            doc.textAnchorAt(5, false));
        // No buffer.
        layer.commitBlockHole(id);

        QCOMPARE(layer.holeCount(), 0);
        QCOMPARE(doc.toMarkdown(), QString("hello"));   // F5 mitigation
        QCOMPARE(abandonedSpy.count(), 1);
        QCOMPARE(reifiedSpy.count(), 0);
    }

    void binding_routes_writes_to_hole_layer_when_holeId_set() {
        Markoff::MarkoffDocument doc(1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        doc.resetContent("hello", Markoff::Origin::FirstOpen);
        QVERIFY(parseSpy.wait(2000));
        QCOMPARE(binding.model()->rowCount(), 1);

        auto *layer   = binding.holeLayer();
        auto *handler = binding.structuralKeyHandler();

        // Create a hole at EOB.
        QVERIFY(handler->tryHandle(Qt::Key_Return, Qt::NoModifier, 0, 5, true, "hello"));
        const quint64 holeId = layer->holesInOrder().first();

        // Build a LiveEditBinding wired to the hole row.
        LiveEditBinding eb;
        eb.setBinding(&binding);
        eb.setModelIndex(1);   // proxy row 1 (the hole)
        eb.setHoleId(holeId);
        QTextEdit editor;
        eb.setText("");        // mirror QML ordering — text bound first
        eb.setRawTextDocument(editor.document());

        // Type 'x' — should route to layer, NOT to applyLocalEdit.
        const quint64 seqBefore = doc.editSequence();
        QTextCursor cur(editor.document());
        cur.setPosition(0);
        cur.insertText("x");

        QCOMPARE(doc.editSequence(), seqBefore);          // CRDT untouched
        QCOMPARE(layer->bufferText(holeId), QString("x")); // hole layer received it
    }

    void binding_ime_composing_propagates_to_layer() {
        Markoff::MarkoffDocument doc(1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        doc.resetContent("hello", Markoff::Origin::FirstOpen);
        QVERIFY(parseSpy.wait(2000));
        QCOMPARE(binding.model()->rowCount(), 1);

        auto *layer   = binding.holeLayer();
        auto *handler = binding.structuralKeyHandler();

        QVERIFY(handler->tryHandle(Qt::Key_Return, Qt::NoModifier, 0, 5, true, "hello"));
        const quint64 holeId = layer->holesInOrder().first();

        LiveEditBinding eb;
        eb.setBinding(&binding);
        eb.setModelIndex(1);
        eb.setHoleId(holeId);
        QTextEdit editor;
        eb.setText("");
        eb.setRawTextDocument(editor.document());

        QSignalSpy idleSpy(layer, SIGNAL(idleCommitDue(quint64)));

        layer->setBlockHoleBuffer(holeId, "preedit");
        eb.setComposing(true);    // should pause idle timer via layer

        QTest::qWait(400);
        QCOMPARE(idleSpy.count(), 0);   // timer didn't fire while composing

        eb.setComposing(false);
        QVERIFY(idleSpy.wait(400));
        QCOMPARE(idleSpy.count(), 1);
    }

    void idle_commit_due_signal_triggers_reification() {
        Markoff::MarkoffDocument doc(1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        doc.resetContent("hello", Markoff::Origin::FirstOpen);
        QVERIFY(parseSpy.wait(2000));
        QCOMPARE(binding.model()->rowCount(), 1);

        auto *layer   = binding.holeLayer();
        auto *handler = binding.structuralKeyHandler();

        QVERIFY(handler->tryHandle(Qt::Key_Return, Qt::NoModifier, 0, 5, true, "hello"));
        const quint64 holeId = layer->holesInOrder().first();
        layer->setBlockHoleBuffer(holeId, "world");

        // Wait for idleCommitDue → commitBlockHole (wired in LiveListModelBinding).
        QSignalSpy abandonedSpy(layer, SIGNAL(holeAbandoned(quint64)));
        QVERIFY(abandonedSpy.wait(500));

        QTRY_COMPARE(doc.toMarkdown(), QString("hello\n\nworld"));
        QCOMPARE(layer->holeCount(), 0);
    }

    void layer_commit_all_pending_in_anchor_order() {
        Markoff::MarkoffDocument doc(1);
        doc.resetContent("para1\n\npara2", Markoff::Origin::FirstOpen);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        QVERIFY(parseSpy.wait(2000));

        Markoff::LiveRender::LiveHoleLayer layer(&doc, nullptr, nullptr);

        quint64 idLate  = layer.createBlockHole(HoleKind::Paragraph,
                                                 doc.textAnchorAt(13, false)); // end of para2
        quint64 idEarly = layer.createBlockHole(HoleKind::Paragraph,
                                                 doc.textAnchorAt(5, false));  // end of para1
        layer.setBlockHoleBuffer(idLate,  "late");
        layer.setBlockHoleBuffer(idEarly, "early");

        layer.commitAllPendingHoles();

        QCOMPARE(layer.holeCount(), 0);
        const QString s = doc.toMarkdown();
        QVERIFY(s.contains("early"));
        QVERIFY(s.contains("late"));
        QVERIFY(s.indexOf("early") < s.indexOf("late"));
    }
};

QTEST_MAIN(TstHolesLayer)
#include "tst_live_render_holes_layer.moc"
