// SPDX-License-Identifier: GPL-3.0-or-later
// MarkdownView contract — live leaf (spec
// docs/specs/2026-06-09-markdownview-contract-v2-design.md §4.1,
// plan Task 7). Cursor slots, read-only slots (Task 8), theme/fontScale
// forwarding + format-verb delegation (Task 9).
//
// Drives the REAL Markoff::Live::EditorWidget (QQuickWidget hosting
// EditorContent.qml → LiveView) and reads/writes cursor position through
// the BASE MarkdownView pointer — invariant 5: the production callsite
// (Corbomite's polymorphic `MarkdownView *activeLeaf()`), not a
// binding-only synonym.
//
// Cursor authority (L3, docs/specs/2026-05-22-cursor-authority-decision.md):
// the chokepoint is canonical. setCursorPosition WRITES through
// LiveCursorState::requestTextCaretAtRow; cursorPosition READS
// LiveCursorState::currentTextCaret. No new cursor store is involved.
//
// Timing note: requestTextCaretAtRow stages a *pending* focus that only
// resolves into the canonical cursor once the target row's QML delegate
// has registered via delegateAvailable (developmental history §A;
// chokepoint spec §5.1). init() therefore waits for the model rows and
// settles the event loop before any slot runs, and every read-after-write
// goes through QTRY_*.

#include <QAction>
#include <QClipboard>
#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickWidget>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>

#include <markoff/core/MarkdownView.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Theme.h>
#include <markoff/live/EditorWidget.h>
#include <markoff/live/LiveActionController.h>
#include <markoff/live/LiveBlockModel.h>
#include <markoff/live/LiveCursorState.h>
#include <markoff/live/LiveListModelBinding.h>
#include <markoff/live/TableEditBinding.h>

#include "../../markoff-core/tests/ViewContractChecks.h"

using Markoff::Live::EditorWidget;
using Markoff::Live::LiveCursorState;
using Markoff::Live::LiveListModelBinding;

class TstViewContractLive : public QObject {
    Q_OBJECT

    Markoff::MarkoffDocument *m_doc    = nullptr;
    EditorWidget             *m_widget = nullptr;

    // The contract is exercised through the base pointer (invariant 5).
    Markoff::MarkdownView *baseView() const { return m_widget; }
    LiveListModelBinding *binding() const { return m_widget->binding(); }
    LiveCursorState *cursorState() const {
        return m_widget->binding()->cursorState();
    }

    // The QQuickWidget hosting EditorContent.qml. Key events sent to it
    // are forwarded into its offscreen QQuickWindow (QQuickWidget's
    // keyPressEvent re-dispatch) and land on the activeFocusItem — the
    // same routing a real keystroke takes once the widget has focus.
    QQuickWidget *quickWidget() const {
        return m_widget->findChild<QQuickWidget *>();
    }
    QQuickWindow *quickWindow() const {
        auto *qw = quickWidget();
        return qw ? qw->quickWindow() : nullptr;
    }

    // Production typing path: chokepoint focus request (the same call the
    // delegates' click handlers make), then real key events through the
    // QQuickWidget → offscreen window → focused TextEdit.
    void focusRow0AndType(const QString &s) {
        cursorState()->requestTextCaretAtRow(0, 0);
        QTRY_VERIFY(quickWindow() && quickWindow()->activeFocusItem());
        for (QChar c : s)
            QTest::keyClick(quickWidget(), c.toLatin1());
        QTest::qWait(20);
    }

    // Production document-mutation path for gate 1 specifically:
    // TextEdit.insert() mutates the delegate's QTextDocument, which fires
    // QTextDocument::contentsChange → LiveEditBinding::onContentsChange —
    // the exact signal chain user input uses. We drive it programmatically
    // because the cosmetic QML `readOnly:` binding (spec §4.2, explicitly
    // "not relied on by tests") stops key-driven text input BEFORE the
    // document, which would otherwise mask the C++ gate.
    void insertIntoFocusedTextEdit(const QString &s) {
        QQuickItem *fi = quickWindow() ? quickWindow()->activeFocusItem()
                                       : nullptr;
        QVERIFY(fi);
        QVERIFY(QMetaObject::invokeMethod(fi, "insert",
                                          Q_ARG(int, 0), Q_ARG(QString, s)));
        QTest::qWait(20);
    }

private Q_SLOTS:
    void init() {
        m_doc = new Markoff::MarkoffDocument(1);
        m_doc->loadFromMarkdown(ViewContract::fixture());
        m_widget = new EditorWidget;
        m_widget->resize(800, 600);
        m_widget->setDocument(m_doc);
        m_widget->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_widget));
        // Wait for the model rows, then let the ListView realise its
        // delegates (delegateAvailable registration) so chokepoint
        // requests can resolve.
        QTRY_COMPARE(m_widget->binding()->model()->rowCount(), 3);
        QTRY_VERIFY(cursorState()->isDelegateRegistered(
            cursorState()->blockAnchorAt(2)));
        QTest::qWait(50);
    }

    void cleanup() {
        delete m_widget;
        m_widget = nullptr;
        delete m_doc;
        m_doc = nullptr;
    }

    /// Empirical pin of the fixture's flat-line model (spec §3: each
    /// block contributes 1 + internal-'\n' lines). Block 1 — the fenced
    /// code block — retains its fences AND internal newlines in the
    /// buffer, so the flat lines are:
    ///   1 = "alpha one", 2 = "```", 3 = "code line", 4 = "```",
    ///   5 = "omega end".
    /// If this slot fails, the expected line numbers in the slots below
    /// are stale, not the production mapping.
    void fixture_line_model_is_as_expected() {
        const auto ids = m_doc->iterateBlocks();
        QCOMPARE(int(ids.size()), 3);
        QCOMPARE(m_doc->blockText(ids[0]), QByteArray("alpha one"));
        QCOMPARE(m_doc->blockText(ids[1]), QByteArray("```\ncode line\n```"));
        QCOMPARE(m_doc->blockText(ids[2]), QByteArray("omega end"));
    }

    void cursorPosition_maps_flat_lines() {
        // Place the caret in "omega end" (model row 2, qtPos 6) through
        // the chokepoint, then read through the BASE pointer.
        cursorState()->requestTextCaretAtRow(2, 6);
        QTRY_COMPARE(baseView()->cursorPosition().line, 5);
        QCOMPARE(baseView()->cursorPosition().column, 7);
    }

    void setCursorPosition_routes_through_chokepoint() {
        baseView()->setCursorPosition({5, 7});
        QTRY_VERIFY(cursorState()->currentTextCaret().has_value());
        QTRY_COMPARE(cursorState()->currentTextCaret()->cachedQtPos,
                     quint32(6));
        // A line inside the code block: line 3 = "code line", col 1 →
        // block row 1, intra-block qtPos 4 (just past the first '\n'
        // of "```\ncode line\n```").
        baseView()->setCursorPosition({3, 1});
        QTRY_VERIFY(cursorState()->currentTextCaret().has_value());
        QTRY_COMPARE(cursorState()->currentTextCaret()->cachedQtPos,
                     quint32(4));
    }

    void setCursorPosition_clamps_out_of_range() {
        // Never a no-op: an out-of-range line clamps to the end of the
        // last block ("omega end", line 5).
        baseView()->setCursorPosition({9999, 99});
        QTRY_VERIFY(cursorState()->currentTextCaret().has_value());
        QTRY_VERIFY(baseView()->cursorPosition().line >= 5);
    }

    // ---- Task 8: read-only mutation-ingress gates (spec §4.2) ----
    //
    // Production path per assertion:
    //   typing      — real key events → QQuickWidget → focused TextEdit
    //                 (blocked by the cosmetic QML readOnly binding);
    //   gate 1      — TextEdit.insert() → QTextDocument contentsChange →
    //                 LiveEditBinding::onContentsChange (bypasses the QML
    //                 cosmetic layer; the C++ gate is the contract);
    //   structural  — Key_Return → delegate Keys.onPressed →
    //                 LiveStructuralKeyHandler::tryHandle;
    //   paste       — Ctrl+V → KeyDispatch.tryDispatchCtrlChord →
    //                 LiveClipboardController::paste();
    //   copy        — Ctrl+C → same chord dispatcher → copy() (ungated).
    void readOnly_blocks_typing_structural_paste_but_not_copy() {
        const QByteArray before = m_doc->serializeForSave();
        m_widget->setReadOnly(true);

        focusRow0AndType(QStringLiteral("XYZ"));
        insertIntoFocusedTextEdit(QStringLiteral("GATE1"));
        QTest::keyClick(quickWidget(), Qt::Key_Return);
        QTest::qWait(30);
        // Paste over a live selection (selectAll = the Ctrl+A chord's
        // target): ungated, this replaces the whole document — the
        // strongest possible falsification of the clipboard gate.
        QGuiApplication::clipboard()->setText(QStringLiteral("PASTE"));
        cursorState()->selectAll();
        QTest::keyClick(quickWidget(), Qt::Key_V, Qt::ControlModifier);
        QTest::qWait(50);
        QCOMPARE(m_doc->serializeForSave(), before);

        // Copy still works while read-only (selection survives from above).
        QGuiApplication::clipboard()->setText(QStringLiteral("sentinel"));
        QTest::keyClick(quickWidget(), Qt::Key_C, Qt::ControlModifier);
        QTRY_VERIFY(QGuiApplication::clipboard()->text().contains(
            QStringLiteral("alpha")));

        // Editing restores when the flag clears (proves the drives above
        // were live, not vacuous).
        m_widget->setReadOnly(false);
        cursorState()->clearSelection();
        insertIntoFocusedTextEdit(QStringLiteral("Z"));
        QTRY_VERIFY(m_doc->serializeForSave() != before);
    }

    void readOnly_disables_edit_actions_but_not_copy_zoom() {
        auto *ac = binding()->actionController();
        QVERIFY(ac);
        // Preconditions so "enabled" is distinguishable from "never
        // enabled": clipboard content (paste) + a selection (cut/copy/bold).
        // begin()/extend() is the production drag/Shift-arrow priming —
        // the same calls tst_live_render_actions_enabled_state uses.
        // (selectAll() sets m_selectionExtended only AFTER its
        // selectionChanged emission, so action states stay stale on it —
        // logged in docs/queue.md Discipline Log.)
        QGuiApplication::clipboard()->setText(QStringLiteral("clip-seed"));
        cursorState()->begin(0, 0);
        cursorState()->extend(0, 5);
        QTRY_VERIFY(ac->pasteAction()->isEnabled());
        QVERIFY(ac->copyAction()->isEnabled());
        QVERIFY(ac->cutAction()->isEnabled());

        m_widget->setReadOnly(true);
        QVERIFY(!ac->pasteAction()->isEnabled());
        QVERIFY(!ac->cutAction()->isEnabled());
        QVERIFY(!ac->deleteAction()->isEnabled());
        QVERIFY(!ac->undoAction()->isEnabled());
        QVERIFY(!ac->redoAction()->isEnabled());
        QVERIFY(!ac->boldAction()->isEnabled());
        QVERIFY(!ac->heading2Action()->isEnabled());
        QVERIFY(ac->copyAction()->isEnabled());
        QVERIFY(ac->selectAllAction()->isEnabled());
        QVERIFY(ac->zoomInAction()->isEnabled());
        QVERIFY(ac->toggleDarkAction()->isEnabled());

        m_widget->setReadOnly(false);
        QVERIFY(ac->pasteAction()->isEnabled());
        QVERIFY(ac->cutAction()->isEnabled());
    }

    // Gate 4. TableDelegate cells call TableEditBinding::applyCellEdit from
    // their onContentsChange with (cellStartCharPos, cellQtPos, removed,
    // added); we construct the binding exactly as the QML delegate does
    // (binding + modelIndex properties) and invoke the same entry with the
    // same argument shape — gate-level production-path drive, since the
    // shared fixture has no table block.
    void readOnly_blocks_table_cell_edit() {
        auto *doc = new Markoff::MarkoffDocument(2);
        doc->loadFromMarkdown(
            QByteArray("| a | b |\n| --- | --- |\n| c | d |"));
        auto *w = new EditorWidget;
        w->resize(800, 600);
        w->setDocument(doc);
        w->show();
        QVERIFY(QTest::qWaitForWindowExposed(w));
        QTRY_VERIFY(w->binding()->model()->rowCount() >= 1);

        const QByteArray before = doc->serializeForSave();
        w->setReadOnly(true);

        Markoff::Live::TableEditBinding teb;
        teb.setBinding(w->binding());
        teb.setModelIndex(0);
        teb.applyCellEdit(/*cellStartCharPos=*/2, /*cellQtPos=*/0,
                          /*removed=*/0, QStringLiteral("X"));
        QTest::qWait(20);
        QCOMPARE(doc->serializeForSave(), before);

        w->setReadOnly(false);
        teb.applyCellEdit(2, 0, 0, QStringLiteral("X"));
        QTRY_VERIFY(doc->serializeForSave() != before);

        delete w;
        delete doc;
    }

    // ---- Task 9: theme/fontScale forwarding + format verbs (§4.3-4.4) ----
    //
    // All drives go through the BASE pointer (invariant 5). The binding's
    // theme() pointer is NEVER null — it rotates two internal copy-buffers
    // (LiveListModelBinding.cpp, Private::themeBuffers) — so a non-null
    // check alone cannot falsify forwarding; the CONTENT probes below can.
    void theme_and_fontScale_forward_to_binding() {
        auto *base = baseView();
        const QColor lightBg = Markoff::Theme::defaultLight().color(
            Markoff::Theme::Slot::EditorBackground);
        const QColor darkBg = Markoff::Theme::defaultDark().color(
            Markoff::Theme::Slot::EditorBackground);
        QVERIFY(lightBg != darkBg);        // probe can discriminate
        QCOMPARE(binding()->theme()->color(
                     Markoff::Theme::Slot::EditorBackground), lightBg);

        QSignalSpy themeSpy(base, &Markoff::MarkdownView::themeChanged);
        QSignalSpy bindingThemeSpy(binding(),
                                   &LiveListModelBinding::themeChanged);
        base->setTheme(Markoff::Theme::defaultDark());
        QCOMPARE(themeSpy.count(), 1);
        QVERIFY(binding()->theme() != nullptr);       // forwarded...
        QCOMPARE(binding()->theme()->color(           // ...with content
                     Markoff::Theme::Slot::EditorBackground), darkBg);
        QCOMPARE(bindingThemeSpy.count(), 1);
        // Base store stays the read authority (no theme() override).
        QCOMPARE(base->theme().color(
                     Markoff::Theme::Slot::EditorBackground), darkBg);
        // Repeated setTheme still notifies the binding: its two-buffer
        // rotation hands out a fresh pointer + themeChanged per call, so
        // there is no pointer-equality short-circuit to defeat.
        base->setTheme(Markoff::Theme::defaultDark());
        QCOMPARE(bindingThemeSpy.count(), 2);

        QSignalSpy scaleSpy(base, &Markoff::MarkdownView::fontScaleChanged);
        base->setFontScale(1.5);
        QCOMPARE(base->fontScale(), 1.5);
        QCOMPARE(binding()->fontScale(), 1.5);        // forwarded
        QCOMPARE(scaleSpy.count(), 1);
        base->setFontScale(1.0);
        QCOMPARE(binding()->fontScale(), 1.0);
    }

    // Format verbs delegate to LiveActionController's QActions — the same
    // QActions Corbomite's menus trigger today, so enabled-state gating
    // (selection presence, read-only — QAction::trigger() ignores
    // explicitly disabled actions) rides along in one authority. Bold +
    // heading assert bytes-level effect end-to-end; the remaining verbs
    // pin the verb→action mapping via triggered() spies, re-priming the
    // selection between toggles because every wrapPerBlock edit rebuilds
    // the model (same rhythm as tst_live_render_format_idempotent).
    void format_verbs_delegate_to_action_controller() {
        auto *ac = binding()->actionController();
        QVERIFY(ac);
        auto *base = baseView();

        // Select "alpha" in row 0 chars 0..5 via the cursor authority —
        // the begin/extend production drag priming Task 8's action test
        // used (boldAction needs a selection per updateEnabledStates).
        cursorState()->begin(0, 0);
        cursorState()->extend(0, 5);
        QTRY_VERIFY(ac->boldAction()->isEnabled());
        base->toggleBold();
        QTRY_VERIFY(m_doc->serializeForSave().contains("**alpha"));

        // Heading verb: re-prime so the selection deterministically
        // touches row 0 (the bold edit rebuilt the model); heading actions
        // need only a wired doc, the controller targets selection rows.
        cursorState()->begin(0, 0);
        cursorState()->extend(0, 1);
        QTRY_VERIFY(ac->heading2Action()->isEnabled());
        base->setHeadingLevel(2);
        QTRY_VERIFY(m_doc->serializeForSave().contains("## "));

        // Mapping pins for the remaining verbs, on row 2 ("omega end") —
        // away from the now-formatted row 0.
        QSignalSpy italicSpy(ac->italicAction(), &QAction::triggered);
        QSignalSpy strikeSpy(ac->strikeAction(), &QAction::triggered);
        QSignalSpy codeSpy(ac->inlineCodeAction(), &QAction::triggered);
        QSignalSpy linkSpy(ac->linkAction(), &QAction::triggered);

        cursorState()->begin(2, 0);
        cursorState()->extend(2, 5);
        QTRY_VERIFY(ac->italicAction()->isEnabled());
        base->toggleItalic();
        QTRY_COMPARE(italicSpy.count(), 1);

        cursorState()->begin(2, 0);
        cursorState()->extend(2, 1);
        QTRY_VERIFY(ac->strikeAction()->isEnabled());
        base->toggleStrikethrough();
        QTRY_COMPARE(strikeSpy.count(), 1);

        cursorState()->begin(2, 0);
        cursorState()->extend(2, 1);
        QTRY_VERIFY(ac->inlineCodeAction()->isEnabled());
        base->toggleInlineCode();
        QTRY_COMPARE(codeSpy.count(), 1);

        // Link allows empty selection (enabled with just a doc).
        QTRY_VERIFY(ac->linkAction()->isEnabled());
        base->insertLink();
        QTRY_COMPARE(linkSpy.count(), 1);

        // No cross-firing: each verb fired exactly its own action.
        QCOMPARE(italicSpy.count(), 1);
        QCOMPARE(strikeSpy.count(), 1);
        QCOMPARE(codeSpy.count(), 1);

        // Out-of-range heading levels are guarded no-ops.
        const QByteArray settled = m_doc->serializeForSave();
        base->setHeadingLevel(7);
        base->setHeadingLevel(-1);
        QCOMPARE(m_doc->serializeForSave(), settled);
    }

    void cursorPositionChanged_signal_fires() {
        QSignalSpy spy(m_widget,
                       &Markoff::MarkdownView::cursorPositionChanged);
        QVERIFY(spy.isValid());
        cursorState()->requestTextCaretAtRow(0, 2);
        QTRY_VERIFY(spy.count() >= 1);
        QCOMPARE(spy.last().at(0).toInt(), 1);   // line 1 ("alpha one")
        QCOMPARE(spy.last().at(1).toInt(), 3);   // qtPos 2 → column 3
    }

    // ---- Task 11(A): EditorContext feed for the live leaf (spec §7) ----
    //
    // The live leaf must emit contextChanged whenever the cursor moves to a
    // different block kind, and must change-gate it (no re-emit on
    // within-block moves). The signal is driven off LiveCursorState::cursorChanged
    // (the cursor authority — L3 decision). Recomputation reads the block kind
    // from MarkoffDocument::blockKind() via the same row→blockId path used by
    // cursorPosition().
    //
    // Fixture model in the live leaf:
    //   block 0: "alpha one"              → Paragraph
    //   block 1: "```\ncode line\n```"    → CodeBlock
    //   block 2: "omega end"              → Paragraph
    //
    // The live leaf's QML onCountChanged fires requestTextCaretAtRow(0, 0)
    // during init(), which pre-warms m_lastContext to "paragraph" before the
    // test slot runs. Unlike source/styled (where cursor-position-changed fires
    // only on explicit cursor moves after setDocument), the live leaf's initial
    // cursor seed is a QML-driven event that fires asynchronously but before
    // init() returns. We therefore write a live-specific version of the gated
    // check: drive to CodeBlock first (known to differ from the initial
    // Paragraph), verify emission, then drive back to Paragraph, verify
    // emission, then stay within Paragraph and verify change-gate.
    void context_changed_kind_gated() {
        qRegisterMetaType<Markoff::EditorContext>();
        QSignalSpy spy(baseView(), &Markoff::MarkdownView::contextChanged);

        // Step 1: Move to block 1 (CodeBlock). Since initial context is
        // Paragraph (from the QML initial cursor seed), this MUST emit.
        // Visual line 3 is inside the code block (block row 1).
        cursorState()->requestTextCaretAtRow(1, 0);
        QTRY_VERIFY(spy.count() >= 1);
        const auto ctx0 = spy.last().at(0).value<Markoff::EditorContext>();
        QCOMPARE(ctx0.blockKind, QString(Markoff::BlockKindNames::CodeBlock));

        // Step 2: Move within the SAME block — change-gated, must NOT emit again.
        const int n = spy.count();
        cursorState()->requestTextCaretAtRow(1, 4);  // within CodeBlock
        QTest::qWait(20);
        QCOMPARE(spy.count(), n);  // change-gated → no new emit

        // Step 3: Move to block 2 (Paragraph). Must emit (CodeBlock → Paragraph).
        cursorState()->requestTextCaretAtRow(2, 0);
        QTRY_VERIFY(spy.count() > n);
        const auto ctx1 = spy.last().at(0).value<Markoff::EditorContext>();
        QCOMPARE(ctx1.blockKind, QString(Markoff::BlockKindNames::Paragraph));
    }

    // ---- Task 11(A): in-table context (spec §7, contract minimum) ----
    //
    // When the caret lands on a Table block, contextChanged must report
    // inTable == true and blockKind == "table". The table row/col fields
    // may be -1 (contract minimum — see Task 11 spec note); this test
    // asserts only the inTable flag and kind, not row/col.
    //
    // Note: table row/col would require surfacing TableEditBinding's per-cell
    // state up to EditorWidget; the live leaf's cursor authority (LiveCursorState)
    // does not carry per-cell coordinates. We assert the contract minimum here.
    //
    // The test uses a two-block document (paragraph + table) so we can start
    // in the paragraph and then move to the table, triggering a contextChanged
    // emission on the kind transition. Using a single-table document would
    // leave m_lastContext pre-warmed to "table" by the QML initial cursor seed
    // before the spy is wired, making the explicit move a no-op for the gate.
    void context_changed_in_table_reports_inTable() {
        qRegisterMetaType<Markoff::EditorContext>();
        auto *doc = new Markoff::MarkoffDocument(2);
        doc->loadFromMarkdown(
            QByteArray("intro\n\n| A | B |\n| --- | --- |\n| x | y |"));
        auto *w = new EditorWidget;
        w->resize(800, 600);
        w->setDocument(doc);
        w->show();
        QVERIFY(QTest::qWaitForWindowExposed(w));
        QTRY_VERIFY(w->binding()->model()->rowCount() >= 2);
        QTRY_VERIFY(w->binding()->cursorState()->isDelegateRegistered(
            w->binding()->cursorState()->blockAnchorAt(1)));
        QTest::qWait(50);

        // The QML initial cursor seed placed the caret at block 0 (paragraph),
        // so m_lastContext is pre-warmed to "paragraph". Moving to block 1
        // (table) triggers a contextChanged emission (kind change).
        QSignalSpy spy(static_cast<Markoff::MarkdownView *>(w),
                       &Markoff::MarkdownView::contextChanged);

        // Move to block 1 (table). The kind-change paragraph→table must emit.
        w->binding()->cursorState()->requestTextCaretAtRow(1, 0);
        QTRY_VERIFY(spy.count() >= 1);
        const auto ctx = spy.last().at(0).value<Markoff::EditorContext>();
        QCOMPARE(ctx.blockKind, QString(Markoff::BlockKindNames::Table));
        QVERIFY(ctx.inTable);

        delete w;
        delete doc;
    }

    // ---- Task 11(B): scrollPositionChanged fires on programmatic scroll ----
    //
    // setScrollPositionVisualLine(pos) is the cross-leaf programmatic scroll
    // API. Calling it must emit scrollPositionChanged with the resulting
    // position value. We don't need a tall document here — the setter drives
    // the signal regardless of the scrollbar state, which is the contract.
    void scrollPositionChanged_fires_on_set() {
        QSignalSpy spy(baseView(),
                       &Markoff::MarkdownView::scrollPositionChanged);
        QVERIFY(spy.isValid());
        // Call setScrollPositionVisualLine. The live leaf may clamp to 0.0
        // when the document fits in the viewport (no scrollbar). What the
        // contract requires is that the signal fires; the exact value is
        // implementation-defined by clamping.
        baseView()->setScrollPositionVisualLine(0.5f);
        QTRY_VERIFY(spy.count() >= 1);
    }

    // ---- Task 11(B) GAP 1: scrollPositionChanged fires on NATIVE QML scroll ----
    //
    // Spec §9: the signal fires on the native change signal. For the live leaf,
    // the native path is the QML ListView/Flickable's contentY property. Setting
    // contentY directly on the root object simulates a user flick (bypassing the
    // programmatic setter). The contentYChanged NOTIFY signal is connected in the
    // EditorWidget constructor (via QQuickWidget::statusChanged → SIGNAL/SLOT
    // string form) to emit scrollPositionChanged. This test exercises that path.
    //
    // Requires a tall document so contentHeight > viewport height; otherwise the
    // QML clamps contentY to 0 and contentYChanged does not fire (skip if so).
    void scrollPositionChanged_fires_on_native_qml_scroll() {
        // Build a tall document — 20 paragraphs.
        auto *doc = new Markoff::MarkoffDocument(3);
        QByteArray md;
        for (int i = 0; i < 20; ++i)
            md += QByteArrayLiteral("Paragraph ") + QByteArray::number(i) + "\n\n";
        doc->loadFromMarkdown(md);

        auto *w = new EditorWidget;
        w->resize(400, 200);
        w->setDocument(doc);
        w->show();
        QVERIFY(QTest::qWaitForWindowExposed(w));
        QTRY_VERIFY(w->binding()->model()->rowCount() >= 5);
        QTest::qWait(80);  // let QML lay out so contentHeight is settled

        auto *qw = w->findChild<QQuickWidget *>();
        QVERIFY(qw);
        auto *root = qw->rootObject();
        if (!root) {
            delete w; delete doc;
            QSKIP("QML root not ready");
        }

        bool ok = false;
        const qreal contentH = root->property("contentHeight").toReal(&ok);
        const qreal height   = root->property("height").toReal();
        if (!ok || contentH <= height) {
            delete w; delete doc;
            QSKIP("Content fits in viewport — no scrollable range (offscreen rendering)");
        }

        QSignalSpy spy(static_cast<Markoff::MarkdownView *>(w),
                       &Markoff::MarkdownView::scrollPositionChanged);
        QVERIFY(spy.isValid());

        // Set contentY directly on the root (native path — simulates a user flick).
        // The contentYChanged NOTIFY fires and reaches onContentYChanged(), which
        // emits scrollPositionChanged.
        const qreal targetY = (contentH - height) * 0.5;
        root->setProperty("contentY", QVariant::fromValue(targetY));
        QTRY_VERIFY(spy.count() >= 1);

        delete w;
        delete doc;
    }
};

QTEST_MAIN(TstViewContractLive)
#include "tst_view_contract_live.moc"
