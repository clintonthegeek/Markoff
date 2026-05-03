// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live-render/AstBlockDiff.h>
#include <markoff/live-render/BlockKind.h>
#include <markoff/live-render/BlockRecord.h>
#include <markoff/live-render/LiveBlockModel.h>
#include <markoff/live-render/LiveHoleLayer.h>
#include <markoff/live-render/LiveProxyBlockModel.h>
#include <markoff-foundation/MarkoffDocument.h>
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
        LiveProxyBlockModel proxy(&inner, &layer);

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
        LiveProxyBlockModel proxy(&inner, &layer);

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
        LiveProxyBlockModel proxy(&inner, &layer);

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
};

QTEST_MAIN(TstProxyModel)
#include "tst_live_render_proxy_model.moc"
