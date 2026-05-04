// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live-render/AstBlockDiff.h>
#include <markoff/live-render/BlockKind.h>
#include <markoff/live-render/BlockRecord.h>
#include <markoff/live-render/LiveBlockModel.h>
#include <markoff/live-render/LiveHoleLayer.h>
#include <markoff/live-render/LiveListModelBinding.h>
#include <markoff/live-render/LiveProxyBlockModel.h>
#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/Origin.h>
#include <QtTest/QtTest>
#include <QSignalSpy>

using namespace Markoff::LiveRender;

namespace {
// Copied from tst_live_render_block_model.cpp (Task 8 brief).
static BlockRecord makeRecord(const QString &kind, const QString &text,
                               int headingLevel = 0, const QString &codeLang = {})
{
    BlockRecord r;
    r.kind         = kind;
    r.text         = text;
    r.headingLevel = headingLevel;
    r.codeLanguage = codeLang;
    return r;
}

static BlockKey keyOf(const BlockRecord &r)
{
    return BlockKey{ r.kind, r.blockAnchor };
}

// Helper: reset content via binding and wait for the model to have expectedRows.
// Returns true if the model reaches the expected row count within timeoutMs.
static bool waitForModelRows(LiveListModelBinding &binding,
                              Markoff::MarkoffDocument &doc,
                              const QByteArray &content,
                              int expectedRows,
                              int timeoutMs = 2000)
{
    QSignalSpy spy(&doc, &Markoff::MarkoffDocument::parseUpdated);
    doc.resetContent(content, Markoff::Origin::FirstOpen);
    if (binding.model()->rowCount() == expectedRows)
        return true;
    if (!spy.wait(timeoutMs))
        return false;
    return binding.model()->rowCount() == expectedRows;
}
}  // namespace

class TstProxyModel : public QObject {
    Q_OBJECT

private slots:
    void proxy_passthrough_with_no_holes_mirrors_inner() {
        // Drive inner directly via applyOps (no MarkoffDocument needed).
        LiveBlockModel inner;
        const QList<BlockRecord> recs = {
            makeRecord(BlockKind::Paragraph, "para1"),
            makeRecord(BlockKind::Paragraph, "para2"),
        };
        QList<BlockKey> keys = { keyOf(recs[0]), keyOf(recs[1]) };
        inner.applyOps(AstBlockDiff::diff({}, keys), recs);
        QCOMPARE(inner.rowCount(), 2);

        // The hole layer needs a MarkoffDocument for resolveTextAnchor in
        // holesInOrder(); for the passthrough test we have zero holes so
        // we can supply a default-constructed doc that's never resolved.
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveHoleLayer layer(&doc, &inner, nullptr);
        LiveProxyBlockModel proxy(&doc, &inner, &layer);

        QCOMPARE(proxy.rowCount(), inner.rowCount());
        for (int r = 0; r < inner.rowCount(); ++r) {
            QCOMPARE(proxy.data(proxy.index(r, 0), LiveBlockModel::TextRole),
                     inner.data(inner.index(r, 0), LiveBlockModel::TextRole));
            QCOMPARE(proxy.data(proxy.index(r, 0),
                                LiveProxyBlockModel::IsHoleRole).toBool(),
                     false);
        }
    }

    void proxy_role_names_include_hole_roles() {
        LiveBlockModel inner;
        Markoff::MarkoffDocument doc(1);
        LiveHoleLayer layer(&doc, &inner, nullptr);
        LiveProxyBlockModel proxy(&doc, &inner, &layer);

        const auto names = proxy.roleNames();
        QVERIFY(names.contains(LiveProxyBlockModel::IsHoleRole));
        QVERIFY(names.contains(LiveProxyBlockModel::BufferTextRole));
        QVERIFY(names.contains(LiveProxyBlockModel::HoleIdRole));
        QCOMPARE(names.value(LiveProxyBlockModel::IsHoleRole), QByteArray("isHole"));
        QCOMPARE(names.value(LiveProxyBlockModel::BufferTextRole), QByteArray("bufferText"));
        QCOMPARE(names.value(LiveProxyBlockModel::HoleIdRole), QByteArray("holeId"));
    }

    void proxy_inner_data_changed_propagates() {
        LiveBlockModel inner;
        const QList<BlockRecord> recs = {
            makeRecord(BlockKind::Paragraph, "first"),
        };
        QList<BlockKey> keys = { keyOf(recs[0]) };
        inner.applyOps(AstBlockDiff::diff({}, keys), recs);

        Markoff::MarkoffDocument doc(1);
        LiveHoleLayer layer(&doc, &inner, nullptr);
        LiveProxyBlockModel proxy(&doc, &inner, &layer);

        QSignalSpy proxySpy(&proxy, &QAbstractListModel::dataChanged);

        // Trigger a dataChanged on inner via applyOps with the same key
        // but a changed text payload.
        const QList<BlockRecord> recs2 = {
            makeRecord(BlockKind::Paragraph, "first edited"),
        };
        // Same key (no insert/delete), data changed.
        inner.applyOps(AstBlockDiff::diff(keys, keys), recs2);

        QVERIFY(proxySpy.count() >= 1);
    }

    // ---- Task 9: anchor-ordered hole insertion tests ----

    void proxy_inserts_hole_row_at_anchor_position() {
        // Use LiveListModelBinding to populate inner with real BlockAnchors.
        Markoff::MarkoffDocument doc(1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        // "para1\n\npara2" → 2 top-level paragraphs.
        QVERIFY(waitForModelRows(binding, doc, "para1\n\npara2", 2));

        LiveBlockModel *inner = binding.model();
        QCOMPARE(inner->rowCount(), 2);

        LiveHoleLayer layer(&doc, inner, nullptr);
        LiveProxyBlockModel proxy(&doc, inner, &layer);
        QCOMPARE(proxy.rowCount(), 2);

        // onHoleInserted uses targeted beginInsertRows/endInsertRows so the
        // QML ListView keeps focus and scroll position around hole creation.
        // (Earlier R5.5 work used beginResetModel as a workaround for an
        // empty-KindRole bug at the data() level; that data() bug was fixed,
        // but the reset workaround stayed and broke the dogfood UX. Targeted
        // insert is the spec-§4.3 contract.)
        QSignalSpy resetSpy(&proxy,    &QAbstractItemModel::modelReset);
        QSignalSpy insertSpy(&proxy,   &QAbstractItemModel::rowsInserted);

        // Anchor at byte 5 = end of "para1"; hole should land between row 0
        // (para1) and row 1 (para2).
        quint64 id = layer.createBlockHole(HoleKind::Paragraph,
                                            doc.textAnchorAt(5, false));

        QCOMPARE(proxy.rowCount(), 3);
        QVERIFY(proxy.proxyRowIsHole(1));
        QCOMPARE(proxy.proxyRowForHole(id), 1);
        QCOMPARE(proxy.proxyRowForInner(1), 2);   // para2 shifted to proxy row 2
        QCOMPARE(resetSpy.count(),  0);
        QCOMPARE(insertSpy.count(), 1);
    }

    void proxy_drops_hole_row_on_abandon() {
        Markoff::MarkoffDocument doc(1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QVERIFY(waitForModelRows(binding, doc, "para1\n\npara2", 2));

        LiveBlockModel *inner = binding.model();
        LiveHoleLayer layer(&doc, inner, nullptr);
        LiveProxyBlockModel proxy(&doc, inner, &layer);

        quint64 id = layer.createBlockHole(HoleKind::Paragraph,
                                            doc.textAnchorAt(5, false));
        QCOMPARE(proxy.rowCount(), 3);

        QSignalSpy removedSpy(&proxy, &QAbstractItemModel::rowsRemoved);
        layer.abandonBlockHole(id);

        QCOMPARE(proxy.rowCount(), 2);
        QCOMPARE(removedSpy.count(), 1);
    }

    void proxy_buffer_changed_emits_dataChanged() {
        Markoff::MarkoffDocument doc(1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QVERIFY(waitForModelRows(binding, doc, "para1\n\npara2", 2));

        LiveBlockModel *inner = binding.model();
        LiveHoleLayer layer(&doc, inner, nullptr);
        LiveProxyBlockModel proxy(&doc, inner, &layer);

        quint64 id = layer.createBlockHole(HoleKind::Paragraph,
                                            doc.textAnchorAt(5, false));
        QCOMPARE(proxy.rowCount(), 3);

        QSignalSpy dcSpy(&proxy, &QAbstractItemModel::dataChanged);
        layer.setBlockHoleBuffer(id, "typing");

        QCOMPARE(dcSpy.count(), 1);
        QCOMPARE(proxy.data(proxy.index(1, 0), LiveProxyBlockModel::BufferTextRole)
                     .toString(),
                 QString("typing"));
    }

    void proxy_ties_break_by_holeId() {
        // Two holes at SAME anchor byte; earlier-created id (lower value) should
        // appear first in proxy row order.
        Markoff::MarkoffDocument doc(1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QVERIFY(waitForModelRows(binding, doc, "para1\n\npara2", 2));

        LiveBlockModel *inner = binding.model();
        LiveHoleLayer layer(&doc, inner, nullptr);
        LiveProxyBlockModel proxy(&doc, inner, &layer);

        quint64 a = layer.createBlockHole(HoleKind::Paragraph,
                                           doc.textAnchorAt(5, false));
        quint64 b = layer.createBlockHole(HoleKind::Paragraph,
                                           doc.textAnchorAt(5, false));

        QVERIFY(proxy.proxyRowForHole(a) < proxy.proxyRowForHole(b));
    }

    void proxy_model_reset_drops_all_holes() {
        Markoff::MarkoffDocument doc(1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QVERIFY(waitForModelRows(binding, doc, "para1\n\npara2", 2));

        LiveBlockModel *inner = binding.model();
        LiveHoleLayer layer(&doc, inner, nullptr);
        LiveProxyBlockModel proxy(&doc, inner, &layer);

        quint64 id = layer.createBlockHole(HoleKind::Paragraph,
                                            doc.textAnchorAt(5, false));
        Q_UNUSED(id);
        QCOMPARE(layer.holeCount(), 1);
        QCOMPARE(proxy.rowCount(), 3);

        // resetContent emits MarkoffDocument::documentReloaded synchronously,
        // which the proxy wires to abandon all open holes. Then the new parse
        // arrives asynchronously and populates the inner model.
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        doc.resetContent("different", Markoff::Origin::FirstOpen);

        // Holes are dropped synchronously on documentReloaded (no wait needed).
        QCOMPARE(layer.holeCount(), 0);

        // Wait for parse to land so the proxy reflects the new content.
        QVERIFY(parseSpy.wait(2000));
        // "different" is a single paragraph → 1 row.
        QCOMPARE(proxy.rowCount(), 1);
    }
};

QTEST_MAIN(TstProxyModel)
#include "tst_live_render_proxy_model.moc"
