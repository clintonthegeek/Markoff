// SPDX-License-Identifier: GPL-3.0-or-later
// EditorWidget's enrollment in the shared MarkdownView contract harness
// (plan P3.1, spec §4.1/§6). Runs the checks EditorWidget already backs
// for real against the BASE `Markoff::MarkdownView *` pointer — the point
// is that the contract works polymorphically, the same shape Corbomite's
// leaf-swap dispatch uses.
//
// checkReadOnlyBlocksUndoAndKeepsBytes: EditorWidget now overrides
// setReadOnly (P3.3), forwarding to the composed View — see
// set_read_only_forwards_to_composed_view below for the wiring pin, and
// tst_canvas_typing/tst_canvas_selection/tst_canvas_ime for the real
// keyboard/mouse/IME gates this check doesn't reach (it only exercises
// undo()/toggleBold() through the base MarkdownView virtuals).
// checkFontScaleSignal (P3.5): now exercises the real
// EditorWidget::setFontScale override, not just the base class's own
// storage — see font_scale_triggers_relayout_and_anchors_scroll below for
// the relayout+scroll-anchor behavior this shared check doesn't reach (it
// only asserts the signal fires and fontScale() reads back correctly).
// checkContextChangedKindGated /
// checkContextChangedOnStructuralKindChangeWithoutCaretMove (P3.5): now
// enrolled — EditorWidget emits contextChanged via recomputeContext(),
// hooked to View::caretChanged (every caret move) and a
// structuralEditSequence-gated d2DocumentChanged connection (Queue #15,
// a programmatic Cmd::changeKind on the caret's own block).
#include <QJsonArray>
#include <QJsonObject>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTest>

#include <markoff/canvas/EditorWidget.h>
#include <markoff/canvas/View.h>
#include <markoff/core/BlockId.h>
#include <markoff/core/BlockKind.h>
#include <markoff/core/MarkdownView.h>
#include <markoff/core/MarkoffDocument.h>

#include "../../markoff-core/tests/ViewContractChecks.h"

class TstCanvasViewContract : public QObject {
    Q_OBJECT
    Markoff::MarkoffDocument   *m_doc = nullptr;
    Markoff::Canvas::EditorWidget *m_ed = nullptr;
private Q_SLOTS:
    void init() {
        m_doc = new Markoff::MarkoffDocument(1);
        m_doc->loadFromMarkdown(ViewContract::fixture());
        m_ed  = new Markoff::Canvas::EditorWidget;
        m_ed->setDocument(m_doc);
    }
    void cleanup() { delete m_ed; delete m_doc; }

    void cursor_round_trip()  { ViewContract::checkCursorRoundTrip(m_ed); }
    void read_only_blocks()   { ViewContract::checkReadOnlyBlocksUndoAndKeepsBytes(m_ed, m_doc); }
    void undo_redo_via_base() { ViewContract::checkUndoRedoViaBase(m_ed, m_doc); }
    void font_scale_signal()  { ViewContract::checkFontScaleSignal(m_ed); }

    // ---- P3.5: EditorContext -----------------------------------------------
    void context_changed_kind_gated() {
        ViewContract::checkContextChangedKindGated(m_ed);
    }
    void context_changed_on_structural_kind_change_without_caret_move() {
        ViewContract::checkContextChangedOnStructuralKindChangeWithoutCaretMove(m_ed, m_doc);
    }

    // ---- P3.3: read-only gates + caretRect --------------------------------

    // EditorWidget::setReadOnly (now overridden, P3.3) must forward to the
    // composed View — the real authority its six mutation-ingress gates
    // read (tst_canvas_typing/selection/ime exercise those gates directly
    // against View; this pins the EditorWidget->View wiring specifically).
    void set_read_only_forwards_to_composed_view() {
        QVERIFY(!m_ed->view()->isReadOnly());
        m_ed->setReadOnly(true);
        QVERIFY(m_ed->isReadOnly());
        QVERIFY(m_ed->view()->isReadOnly());
        QVERIFY(!m_ed->hasEditing());
        m_ed->setReadOnly(false);
        QVERIFY(!m_ed->view()->isReadOnly());
        QVERIFY(m_ed->hasEditing());
    }

    // caretRect() (base contract's completion-popup anchor, P3.3) is valid
    // once a document is loaded and the caret is on a realized block —
    // EditorWidget coordinates, i.e. within its own local rect.
    void caret_rect_is_valid_and_in_widget_bounds() {
        m_ed->resize(220, 80);
        m_ed->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_ed));
        m_ed->setCursorPosition({1, 1});
        const QRect r = m_ed->caretRect();
        QVERIFY(r.isValid());
        QVERIFY(m_ed->rect().contains(r.topLeft()));
        m_ed->hide();
    }

    // ---- P3.2: cursor/scroll position mapping + signals -------------------

    // Shared check (new this task, general over any MarkdownView*):
    // cursorPositionChanged fires on a genuine caret move and stays silent
    // on a same-position re-set.
    void cursor_position_changed_signal() {
        ViewContract::checkCursorPositionChangedSignal(m_ed);
    }

    // Out-of-range setCursorPosition clamps rather than no-ops — the
    // task's named falsifiable check. checkCursorRoundTrip already covers
    // this generically (park on line 1, set an absurd line, verify it
    // moved); this slot pins it directly against EditorWidget so the
    // falsification below has an obvious, EditorWidget-specific target.
    void set_cursor_position_out_of_range_clamps() {
        m_ed->setCursorPosition({1, 1});
        m_ed->setCursorPosition({9999, 9999});
        QVERIFY(m_ed->cursorPosition().line > 1);
    }

    // scrollPositionChanged must fire on a programmatic set — same minimum
    // Source::Editor/Styled::Editor assert (their scrollPositionChanged_
    // fires_on_set slots): the exact resulting value may legitimately
    // differ from the requested fraction (e.g. clamped to 0 when the
    // document fits the viewport), but the signal must fire.
    void scroll_position_changed_fires_on_set() {
        QSignalSpy spy(m_ed, &Markoff::MarkdownView::scrollPositionChanged);
        QVERIFY(spy.isValid());
        m_ed->setScrollPositionVisualLine(0.5f);
        QTRY_VERIFY(spy.count() >= 1);
    }

    // A genuinely scrollable view (viewport shrunk below document height)
    // round-trips a mid-range fraction and clamps an out-of-range one —
    // never a no-op. Needs a real, shown viewport: the composed View is a
    // QAbstractScrollArea and only computes a non-zero scrollbar range
    // once it has a real size to lay out against.
    void scroll_position_round_trip_and_clamps() {
        m_ed->resize(220, 60);
        m_ed->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_ed));
        QVERIFY(m_ed->view()->verticalScrollBar()->maximum() > 0);

        m_ed->setScrollPositionVisualLine(0.5f);
        const float mid = m_ed->scrollPositionVisualLine();
        QVERIFY(mid > 0.0f);
        QVERIFY(mid < 1.0f);

        m_ed->setScrollPositionVisualLine(-1.0f);           // clamps to 0
        QCOMPARE(m_ed->scrollPositionVisualLine(), 0.0f);
        m_ed->setScrollPositionVisualLine(2.0f);             // clamps to 1
        QCOMPARE(m_ed->scrollPositionVisualLine(), 1.0f);

        m_ed->hide();
    }

    // ---- P3.1: the task's named falsifiable check -------------------------
    void attach_window_caret_write_survives() {
        ViewContract::checkAttachWindowCaretWriteSurvives(m_ed, m_doc);
    }

    // ---- setDocument/Session lifecycle (P3.1) ------------------------------

    // A second setDocument() on the SAME document (a reattach, the same
    // shape a leaf swap or a document-reload seam would exercise) must not
    // crash and must leave the caret at a well-defined, reachable position
    // — this is the scenario checkAttachWindowCaretWriteSurvives's
    // v->setDocument(doc) call inside it exercises; this slot pins the
    // "does not crash / caret still round-trips" half directly.
    void reattach_same_document_is_safe() {
        m_ed->setDocument(m_doc);
        m_ed->setCursorPosition({1, 1});
        QCOMPARE(m_ed->cursorPosition().line, 1);
    }

    // setDocument(nullptr) detaches without touching document content
    // (plan P3.1: "detach ... without touching content").
    void set_null_document_detaches_without_touching_content() {
        const QByteArray before = m_doc->serializeForSave();
        m_ed->setDocument(nullptr);
        QCOMPARE(m_ed->document(), nullptr);
        QCOMPARE(m_doc->serializeForSave(), before);
        // Re-attach so cleanup()'s delete order (widget destroyed while
        // still holding a session on m_doc) is exercised too.
        m_ed->setDocument(m_doc);
    }

    // The Session EditorWidget auto-creates in setDocument() must be torn
    // down by the time the widget is destroyed (not leaked as a ghost
    // session on the document) — same contract live documents in its
    // dtor. There's no public "session count" accessor, so this is an
    // indirect check: destroying and recreating the widget against the
    // same document repeatedly must not accumulate anything visible
    // (no crash, no growth in cost) — a leak would typically surface as
    // a dangling-pointer crash on the next document mutation, which the
    // subsequent init()/cleanup() cycle in this test file already
    // exercises every slot. This slot documents the intent explicitly.
    void destroying_widget_tears_down_its_session() {
        auto *doc = new Markoff::MarkoffDocument(2);
        doc->loadFromMarkdown(ViewContract::fixture());
        for (int i = 0; i < 3; ++i) {
            Markoff::Canvas::EditorWidget ed;
            ed.setDocument(doc);
        }
        // Document must still be perfectly usable afterward.
        doc->loadFromMarkdown(ViewContract::fixture());
        delete doc;
    }

    // ---- P3.5: EditorContext table row/col ---------------------------------

    // Contract-v2 P3.5: canvas can derive REAL table row/col from the
    // composed View's own per-cell layout (P2.3's row-major cell sequence,
    // View::caretTableCell()) — unlike the live leaf, which has no stable
    // C++ owner for a QML delegate's focused cell and reports the
    // (-1, -1) contract minimum instead. row 0 is the header row (same
    // convention as View::tableCellRect()), so the first body row is 1.
    void context_reports_table_row_and_col() {
        const QByteArray src =
            "| h0 | h1 | h2 |\n"
            "|----|----|----|\n"
            "| a0 | a1 | a2 |\n"
            "| b0 | b1 | b2 |\n"
            "\n"
            "after paragraph.\n";
        auto *doc = new Markoff::MarkoffDocument(3);
        doc->loadFromMarkdown(src);
        auto *ed = new Markoff::Canvas::EditorWidget;
        ed->resize(500, 400);
        ed->setDocument(doc);
        ed->show();
        QVERIFY(QTest::qWaitForWindowExposed(ed));

        Markoff::BlockId tableId;
        for (const auto id : doc->iterateBlocks()) {
            if (doc->blockKind(id) == Markoff::BlockKind::Table) { tableId = id; break; }
        }
        QVERIFY(!tableId.isNull());

        const QByteArray text = doc->blockText(tableId);
        const int a1Byte = text.indexOf("a1");
        QVERIFY(a1Byte >= 0);

        qRegisterMetaType<Markoff::EditorContext>();
        QSignalSpy spy(ed, &Markoff::MarkdownView::contextChanged);
        ed->view()->setCaretPosition(tableId, a1Byte);
        QTRY_VERIFY(spy.count() >= 1);
        const auto ctx = spy.last().at(0).value<Markoff::EditorContext>();
        QCOMPARE(ctx.blockKind, QString(Markoff::BlockKindNames::Table));
        QVERIFY(ctx.inTable);
        QCOMPARE(ctx.tableRow, 1);   // first body row
        QCOMPARE(ctx.tableCol, 1);   // second column ("a1")

        delete ed;
        delete doc;
    }

    // ---- P3.5: fontScale relayout + scroll re-anchor -----------------------

    // The task's named falsifiable check: setFontScale must trigger a full
    // relayout (every block's font — and therefore its layout width/height
    // — depends on the scale), NOT just restyle in place, and must
    // re-anchor scroll to the block that was at the viewport's top rather
    // than preserving the old pixel offset (every height just changed, so
    // the old offset would land on an unrelated block).
    void font_scale_triggers_relayout_and_anchors_scroll() {
        QByteArray src;
        for (int i = 0; i < 40; ++i)
            src += "paragraph number " + QByteArray::number(i) + "\n\n";
        auto *doc = new Markoff::MarkoffDocument(4);
        doc->loadFromMarkdown(src);
        auto *ed = new Markoff::Canvas::EditorWidget;
        ed->resize(300, 200);
        ed->setDocument(doc);
        ed->show();
        QVERIFY(QTest::qWaitForWindowExposed(ed));

        auto *view = ed->view();
        QVERIFY(view->verticalScrollBar()->maximum() > 0);

        // Scroll partway down and find the block sitting at the viewport's
        // top edge.
        view->verticalScrollBar()->setValue(view->verticalScrollBar()->maximum() / 2);
        const qreal scrolledY = view->verticalScrollBar()->value();
        QVERIFY(scrolledY > 0);

        Markoff::BlockId anchor;
        for (const auto id : doc->iterateBlocks()) {
            const QRectF r = view->blockRect(id);
            if (r.top() <= scrolledY && r.bottom() > scrolledY) { anchor = id; break; }
        }
        QVERIFY(!anchor.isNull());
        const qreal heightBefore = view->blockRect(anchor).height();

        ed->setFontScale(2.0);

        // Falsifiable half 1: the relayout actually happened — height
        // measurably grew under the larger font, not just a repaint with
        // the old layout.
        const qreal heightAfter = view->blockRect(anchor).height();
        QVERIFY(heightAfter > heightBefore * 1.2);

        // Falsifiable half 2: scroll followed the ANCHOR BLOCK, not the
        // stale pixel offset — the anchor's top must now sit at (or just
        // above) the viewport's top edge.
        const qreal newScroll = view->verticalScrollBar()->value();
        const QRectF anchorRectAfter = view->blockRect(anchor);
        QVERIFY(anchorRectAfter.top() <= newScroll + 1.0);
        QVERIFY(anchorRectAfter.bottom() > newScroll);

        delete ed;
        delete doc;
    }

    // ---- P3.6: ephemeral state JSON round-trip -----------------------------

    // Schema pin (plan P3.6, folds filled in P5.6): "scroll" is
    // {blockIndex, fraction}, "cursors" is an ARRAY of one {blockIndex,
    // byte} element (F1a multi-cursor readiness — not a bare object),
    // "folds" is an array of {blockIndex} objects, empty here because this
    // fixture's document has no folds toggled — see
    // tst_canvas_folding.cpp's ephemeral_state_round_trip_through_
    // detach_reattach for the populated case.
    void ephemeral_state_schema_shape() {
        m_ed->setCursorPosition({1, 1});
        const QJsonObject state = m_ed->saveEphemeralState();

        QVERIFY(state.contains(QStringLiteral("scroll")));
        QVERIFY(state.value(QStringLiteral("scroll")).isObject());
        const QJsonObject scroll = state.value(QStringLiteral("scroll")).toObject();
        QVERIFY(scroll.contains(QStringLiteral("blockIndex")));
        QVERIFY(scroll.contains(QStringLiteral("fraction")));

        QVERIFY(state.contains(QStringLiteral("cursors")));
        QVERIFY(state.value(QStringLiteral("cursors")).isArray());
        const QJsonArray cursors = state.value(QStringLiteral("cursors")).toArray();
        QCOMPARE(cursors.size(), 1);   // list of ONE, not a scalar (F1a)
        QVERIFY(cursors.first().isObject());
        const QJsonObject cursor = cursors.first().toObject();
        QVERIFY(cursor.contains(QStringLiteral("blockIndex")));
        QVERIFY(cursor.contains(QStringLiteral("byte")));

        QVERIFY(state.contains(QStringLiteral("folds")));
        QVERIFY(state.value(QStringLiteral("folds")).isArray());
        QVERIFY(state.value(QStringLiteral("folds")).toArray().isEmpty());
    }

    // Save → mutate (caret AND scroll both moved elsewhere) → restore →
    // both must land back on the originally-saved values, not wherever the
    // mutation left them. This is the task's own named falsification
    // target (skip restoring cursor position) applied as a real test.
    void ephemeral_state_round_trip_in_place() {
        QByteArray src;
        for (int i = 0; i < 40; ++i)
            src += "paragraph number " + QByteArray::number(i) + "\n\n";
        auto *doc = new Markoff::MarkoffDocument(5);
        doc->loadFromMarkdown(src);
        auto *ed = new Markoff::Canvas::EditorWidget;
        ed->resize(300, 200);
        ed->setDocument(doc);
        ed->show();
        QVERIFY(QTest::qWaitForWindowExposed(ed));
        QVERIFY(ed->view()->verticalScrollBar()->maximum() > 0);

        // Park the caret + scroll somewhere in the middle of the document.
        Markoff::BlockId savedBlock;
        int i = 0;
        for (const auto id : doc->iterateBlocks()) {
            if (i++ == 20) { savedBlock = id; break; }
        }
        QVERIFY(!savedBlock.isNull());
        ed->view()->setCaretPosition(savedBlock, 3);
        ed->view()->verticalScrollBar()->setValue(
            ed->view()->verticalScrollBar()->maximum() / 3);

        const QJsonObject saved = ed->saveEphemeralState();
        const Markoff::BlockId savedCaretBlock = ed->view()->caretBlock();
        const int savedCaretByte = ed->view()->caretByteOffset();
        const int savedScrollValue = ed->view()->verticalScrollBar()->value();

        // Mutate: caret to the very first block, scroll to the very top —
        // as different from the saved state as this fixture allows.
        Markoff::BlockId firstBlock = *doc->iterateBlocks().begin();
        ed->view()->setCaretPosition(firstBlock, 0);
        ed->view()->verticalScrollBar()->setValue(0);
        QVERIFY(ed->view()->caretBlock() != savedCaretBlock
                || ed->view()->caretByteOffset() != savedCaretByte);

        ed->restoreEphemeralState(saved);

        QCOMPARE(ed->view()->caretBlock(), savedCaretBlock);
        QCOMPARE(ed->view()->caretByteOffset(), savedCaretByte);
        // Scroll restore is anchor-based (block + within-block fraction),
        // so it need not reproduce the exact pixel value bit-for-bit, but
        // it must land close — same tolerance spirit as the fontScale
        // re-anchor test above.
        QVERIFY(qAbs(ed->view()->verticalScrollBar()->value() - savedScrollValue) <= 2);

        delete ed;
        delete doc;
    }

    // Round-trip through a REAL detach/reattach cycle (plan P3.6's own
    // wording), not just an in-place restore: save state, detach the
    // document (setDocument(nullptr)), reattach it, then restore — the
    // saved block INDICES must still resolve against the same document's
    // re-synced cache and land the caret/scroll back where they were.
    void ephemeral_state_round_trip_through_detach_reattach() {
        QByteArray src;
        for (int i = 0; i < 40; ++i)
            src += "paragraph number " + QByteArray::number(i) + "\n\n";
        auto *doc = new Markoff::MarkoffDocument(6);
        doc->loadFromMarkdown(src);
        auto *ed = new Markoff::Canvas::EditorWidget;
        ed->resize(300, 200);
        ed->setDocument(doc);
        ed->show();
        QVERIFY(QTest::qWaitForWindowExposed(ed));
        QVERIFY(ed->view()->verticalScrollBar()->maximum() > 0);

        Markoff::BlockId savedBlock;
        int i = 0;
        for (const auto id : doc->iterateBlocks()) {
            if (i++ == 15) { savedBlock = id; break; }
        }
        QVERIFY(!savedBlock.isNull());
        ed->view()->setCaretPosition(savedBlock, 2);
        ed->view()->verticalScrollBar()->setValue(
            ed->view()->verticalScrollBar()->maximum() / 4);

        const QJsonObject saved = ed->saveEphemeralState();
        const int savedBlockIndex =
            saved.value(QStringLiteral("cursors")).toArray().first()
                .toObject().value(QStringLiteral("blockIndex")).toInt();

        // Real detach/reattach — not an in-place restore.
        ed->setDocument(nullptr);
        QCOMPARE(ed->document(), nullptr);
        ed->setDocument(doc);

        ed->restoreEphemeralState(saved);

        QCOMPARE(ed->view()->blockIndexOf(ed->view()->caretBlock()), savedBlockIndex);
        QCOMPARE(ed->view()->caretByteOffset(), 2);

        delete ed;
        delete doc;
    }

    // Restoring against missing/malformed JSON must not crash and must
    // leave whatever pieces it can't parse untouched — e.g. an empty
    // object (fresh-document case), or a blob with the right top-level
    // keys but the wrong inner shape.
    void ephemeral_state_restore_handles_malformed_json() {
        m_ed->setCursorPosition({1, 1});
        const Markoff::BlockId beforeBlock = m_ed->view()->caretBlock();
        const int beforeByte = m_ed->view()->caretByteOffset();

        m_ed->restoreEphemeralState(QJsonObject{});   // completely empty
        QCOMPARE(m_ed->view()->caretBlock(), beforeBlock);
        QCOMPARE(m_ed->view()->caretByteOffset(), beforeByte);

        QJsonObject malformed;
        malformed[QStringLiteral("scroll")] = QStringLiteral("not an object");
        malformed[QStringLiteral("cursors")] = QStringLiteral("not an array");
        malformed[QStringLiteral("folds")] = 42;
        m_ed->restoreEphemeralState(malformed);        // must not crash
        QCOMPARE(m_ed->view()->caretBlock(), beforeBlock);
        QCOMPARE(m_ed->view()->caretByteOffset(), beforeByte);
    }
};

QTEST_MAIN(TstCanvasViewContract)
#include "tst_canvas_view_contract.moc"
