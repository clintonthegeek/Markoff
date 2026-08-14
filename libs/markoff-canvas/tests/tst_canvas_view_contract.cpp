// SPDX-License-Identifier: GPL-3.0-or-later
// EditorWidget's enrollment in the shared MarkdownView contract harness
// (plan P3.1, spec §4.1/§6). Runs the checks EditorWidget already backs
// for real against the BASE `Markoff::MarkdownView *` pointer — the point
// is that the contract works polymorphically, the same shape Corbomite's
// leaf-swap dispatch uses.
//
// Not yet enrolled (land with their owning tasks, not stubbed early):
//   - checkContextChangedKindGated /
//     checkContextChangedOnStructuralKindChangeWithoutCaretMove — EditorWidget
//     does not emit contextChanged yet (P3.5).
// checkReadOnlyBlocksUndoAndKeepsBytes: EditorWidget now overrides
// setReadOnly (P3.3), forwarding to the composed View — see
// set_read_only_forwards_to_composed_view below for the wiring pin, and
// tst_canvas_typing/tst_canvas_selection/tst_canvas_ime for the real
// keyboard/mouse/IME gates this check doesn't reach (it only exercises
// undo()/toggleBold() through the base MarkdownView virtuals).
// checkFontScaleSignal still passes via the base class's own
// storage/gating only — EditorWidget has not overridden setFontScale yet
// (P3.5). Kept enrolled anyway: it costs nothing and starts earning its
// keep the moment that override lands.
#include <QScrollBar>
#include <QSignalSpy>
#include <QTest>

#include <markoff/canvas/EditorWidget.h>
#include <markoff/canvas/View.h>
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
};

QTEST_MAIN(TstCanvasViewContract)
#include "tst_canvas_view_contract.moc"
