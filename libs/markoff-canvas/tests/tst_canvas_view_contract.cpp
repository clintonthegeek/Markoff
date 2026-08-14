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
// checkReadOnlyBlocksUndoAndKeepsBytes and checkFontScaleSignal pass today
// via the base class's own storage/gating (setReadOnly/isReadOnly,
// setFontScale/fontScaleChanged) — EditorWidget has not overridden either
// yet (P3.3, P3.5), so they exercise the base only. Kept enrolled anyway:
// they cost nothing and start earning their keep the moment those
// overrides land.
#include <QSignalSpy>
#include <QTest>

#include <markoff/canvas/EditorWidget.h>
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
