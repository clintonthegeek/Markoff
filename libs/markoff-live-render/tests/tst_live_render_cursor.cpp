// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>

#include <markoff/live-render/Cursor.h>
#include <markoff/live-render/LiveCursorState.h>
#include <markoff/live-render/BlockHitTester.h>
#include <markoff/live-render/BlockKind.h>
#include <markoff/live-render/BlockKindRegistry.h>
#include <markoff/live-render/LiveBlockModel.h>
#include <markoff/live-render/AstBlockDiff.h>

using namespace Markoff::LiveRender;

// ---- helpers ----

static BlockRecord makeRec(const QString &kind, const QString &text,
                            int headingLevel = 0)
{
    BlockRecord r;
    r.kind = kind;
    r.text = text;
    r.headingLevel = headingLevel;
    return r;
}

static BlockKey keyOf(const BlockRecord &r)
{
    return BlockKey{ r.kind, r.blockAnchor };
}

// ---- BlockHitTester mock helpers (file-scope; Q_OBJECT requires top-level) ----

class MockItem : public QObject {
    Q_OBJECT
    Q_PROPERTY(double x      MEMBER m_x      CONSTANT)
    Q_PROPERTY(double y      MEMBER m_y      CONSTANT)
    Q_PROPERTY(double width  MEMBER m_width  CONSTANT)
    Q_PROPERTY(double height MEMBER m_height CONSTANT)
    Q_PROPERTY(int    modelIndex MEMBER m_modelIndex CONSTANT)
public:
    double m_x = 0, m_y = 0, m_width = 600, m_height = 24;
    int    m_modelIndex = 0;
    int    m_positionAtResult = 5;
    Q_INVOKABLE int positionAt(double, double) { return m_positionAtResult; }
};

class MockListView : public QObject {
    Q_OBJECT
    Q_PROPERTY(int    count         MEMBER m_count         CONSTANT)
    Q_PROPERTY(double contentX      MEMBER m_contentX      CONSTANT)
    Q_PROPERTY(double contentY      MEMBER m_contentY      CONSTANT)
    Q_PROPERTY(double contentHeight MEMBER m_contentHeight CONSTANT)
    Q_PROPERTY(double width         MEMBER m_width         CONSTANT)
    Q_PROPERTY(double height        MEMBER m_height        CONSTANT)
public:
    int    m_count        = 1;
    double m_contentX     = 0, m_contentY = 0, m_contentHeight = 24;
    double m_width        = 600, m_height = 600;
    MockItem *m_item      = nullptr;

    Q_INVOKABLE QObject* itemAt(double /*cx*/, double cy) {
        if (!m_item) return nullptr;
        return (cy >= m_item->m_y && cy < m_item->m_y + m_item->m_height)
               ? m_item : nullptr;
    }
};

// ---- Test class ----

class TstLiveRenderCursor : public QObject {
    Q_OBJECT

private Q_SLOTS:

    // ---- LiveCursorState tests ----

    void cursor_starts_with_no_focus() {
        BlockKindRegistry reg;
        LiveBlockModel model;
        LiveCursorState cs(&reg, &model);
        QVERIFY(std::holds_alternative<NoCursor>(cs.cursor()));
        QCOMPARE(cs.cursorKind(), QStringLiteral("none"));
    }

    void request_text_caret_emits_cursor_changed() {
        BlockKindRegistry reg;
        LiveBlockModel model;
        const auto recs = QList<BlockRecord>{ makeRec(BlockKind::Paragraph, "hello") };
        model.applyOps(AstBlockDiff::diff({}, { keyOf(recs[0]) }), recs);

        LiveCursorState cs(&reg, &model);
        QSignalSpy spy(&cs, &LiveCursorState::cursorChanged);

        TextCaret tc;
        tc.block = recs[0].blockAnchor;
        tc.cachedByteOffset = 0;
        cs.request(tc);

        QCOMPARE(spy.count(), 1);
        QVERIFY(std::holds_alternative<TextCaret>(cs.cursor()));
        QCOMPARE(cs.cursorKind(), QStringLiteral("TextCaret"));
    }

    void request_block_selected_for_hr() {
        BlockKindRegistry reg;
        LiveBlockModel model;
        const auto recs = QList<BlockRecord>{ makeRec(BlockKind::HorizontalRule, "---") };
        model.applyOps(AstBlockDiff::diff({}, { keyOf(recs[0]) }), recs);

        LiveCursorState cs(&reg, &model);
        QSignalSpy spy(&cs, &LiveCursorState::cursorChanged);

        BlockSelected bs;
        bs.block = recs[0].blockAnchor;
        cs.request(bs);

        QCOMPARE(spy.count(), 1);
        QVERIFY(std::holds_alternative<BlockSelected>(cs.cursor()));
        QCOMPARE(cs.cursorKind(), QStringLiteral("BlockSelected"));
    }

    void request_invalid_variant_for_kind_is_rejected() {
        BlockKindRegistry reg;
        LiveBlockModel model;
        const auto recs = QList<BlockRecord>{ makeRec(BlockKind::Paragraph, "hello") };
        model.applyOps(AstBlockDiff::diff({}, { keyOf(recs[0]) }), recs);

        LiveCursorState cs(&reg, &model);
        QSignalSpy spy(&cs, &LiveCursorState::cursorChanged);

        BlockSelected bs;
        bs.block = recs[0].blockAnchor;
        cs.request(bs);  // BlockSelected on paragraph = invalid

        QCOMPARE(spy.count(), 0);
        QVERIFY(std::holds_alternative<NoCursor>(cs.cursor()));
    }

    void request_same_cursor_emits_no_signal() {
        BlockKindRegistry reg;
        LiveBlockModel model;
        const auto recs = QList<BlockRecord>{ makeRec(BlockKind::Paragraph, "hi") };
        model.applyOps(AstBlockDiff::diff({}, { keyOf(recs[0]) }), recs);

        LiveCursorState cs(&reg, &model);
        TextCaret tc;
        tc.block = recs[0].blockAnchor;
        tc.cachedByteOffset = 0;
        cs.request(tc);

        QSignalSpy spy(&cs, &LiveCursorState::cursorChanged);
        cs.request(tc);
        QCOMPARE(spy.count(), 0);
    }

    void clear_resets_to_no_cursor() {
        BlockKindRegistry reg;
        LiveBlockModel model;
        const auto recs = QList<BlockRecord>{ makeRec(BlockKind::Paragraph, "hi") };
        model.applyOps(AstBlockDiff::diff({}, { keyOf(recs[0]) }), recs);

        LiveCursorState cs(&reg, &model);
        TextCaret tc;
        tc.block = recs[0].blockAnchor;
        cs.request(tc);
        QVERIFY(!std::holds_alternative<NoCursor>(cs.cursor()));

        cs.clear();
        QVERIFY(std::holds_alternative<NoCursor>(cs.cursor()));
    }

    // ---- LiveRenderSelection type tests ----

    void selection_collapsed_caret() {
        TextCaret tc;
        tc.cachedByteOffset = 3;
        LiveRenderSelection sel;
        sel.anchor = tc;
        sel.active = tc;
        QVERIFY(sel.isCaret());
        QVERIFY(sel.isCollapsed());
    }

    void selection_non_collapsed() {
        TextCaret a, b;
        a.cachedByteOffset = 0;
        b.cachedByteOffset = 5;
        LiveRenderSelection sel;
        sel.anchor = a;
        sel.active = b;
        QVERIFY(!sel.isCollapsed());
        QVERIFY(!sel.isCaret());
    }

    void selection_no_focus() {
        LiveRenderSelection sel;
        QVERIFY(sel.hasNoFocus());
    }

    // ---- BlockHitTester tests ----

    void hit_tester_direct_hit_returns_block_and_offset() {
        MockItem item;
        item.m_y = 0; item.m_height = 24;
        item.m_modelIndex = 0;
        item.m_positionAtResult = 7;

        MockListView lv;
        lv.m_item = &item;
        lv.m_contentHeight = 24;

        BlockHitTester ht;
        ht.setListView(&lv);

        const QVariantMap r = ht.hit(100, 12, 600);
        QVERIFY(r.value(QStringLiteral("blockIndex"), -1).toInt() >= 0);
        QCOMPARE(r.value(QStringLiteral("blockIndex")).toInt(), 0);
        QCOMPARE(r.value(QStringLiteral("qtPos")).toInt(), 7);
    }

    void hit_tester_below_content_snaps_to_last_block() {
        MockItem item;
        item.m_y = 0; item.m_height = 24;
        item.m_modelIndex = 0;
        item.m_positionAtResult = 3;

        MockListView lv;
        lv.m_item = &item;
        lv.m_contentHeight = 24;

        BlockHitTester ht;
        ht.setListView(&lv);

        const QVariantMap r = ht.hit(100, 500, 600);
        QVERIFY(r.value(QStringLiteral("blockIndex"), -1).toInt() >= 0);
    }

    void hit_tester_no_listview_returns_miss() {
        BlockHitTester ht;
        const QVariantMap r = ht.hit(100, 100, 600);
        QCOMPARE(r.value(QStringLiteral("blockIndex"), -1).toInt(), -1);
    }

    void hit_tester_empty_model_returns_miss() {
        MockListView lv;
        lv.m_count = 0;

        BlockHitTester ht;
        ht.setListView(&lv);

        const QVariantMap r = ht.hit(100, 100, 600);
        QCOMPARE(r.value(QStringLiteral("blockIndex"), -1).toInt(), -1);
    }
};

QTEST_APPLESS_MAIN(TstLiveRenderCursor)
#include "tst_live_render_cursor.moc"
