// SPDX-License-Identifier: GPL-3.0-or-later
//
// P6.1 — Session caret authority closure. Every local caret/selection
// change in `Markoff::Canvas::View` must push through to
// `Session::setPrimarySelection()` (guide §B: cursor/selection lives on
// the Session, anchor-typed, so it is view-agnostic and portable — the
// mechanism P6.2's remote-presence paint path and any other external
// consumer relies on).
//
// Falsification target (plan P6.1): stop pushing to the Session; an
// EXTERNAL reader (this file — reads `session->primarySelection()`
// directly, never through View) fails to see the caret/selection that
// really is live in the widget. Same shape as P6.0's
// `tst_canvas_folding.cpp` `toggle_fold_writes_through_to_session()`.

#include <QTest>

#include <markoff/canvas/EditorWidget.h>
#include <markoff/canvas/View.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/core/UndoLog.h>

using Markoff::BlockId;
using Markoff::Canvas::EditorWidget;
using Markoff::Canvas::View;
using Markoff::MarkoffDocument;
using Markoff::Selection;
using Markoff::Session;

namespace {

/// Same rationale as tst_canvas_typing.cpp's helper: drives the real
/// keyPressEvent path (View reads event->text(), not the key code, for
/// printable input) rather than a test-only insertion entry point.
void sendTextKeyEvent(QWidget *w, const QString &text)
{
    QKeyEvent press(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, text);
    QCoreApplication::sendEvent(w, &press);
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_unknown, Qt::NoModifier, text);
    QCoreApplication::sendEvent(w, &release);
}

}  // namespace

class TstCanvasSessionSelection : public QObject {
    Q_OBJECT

private slots:
    void typing_pushes_caret_to_session();
    void arrow_key_motion_pushes_caret_to_session();
    void shift_click_selection_pushes_range_to_session();
    void model_change_reresolves_caret_from_session_anchor();
    void undo_falls_through_to_clamp_when_session_anchor_is_stale();
};

void TstCanvasSessionSelection::typing_pushes_caret_to_session()
{
    auto *doc = new MarkoffDocument(1);
    doc->loadFromMarkdown("Hello world.\n");
    auto *ed = new EditorWidget;
    ed->resize(400, 300);
    ed->setDocument(doc);
    ed->show();
    QVERIFY(QTest::qWaitForWindowExposed(ed));
    ed->view()->setFocus();

    Session *session = ed->view()->session();
    QVERIFY(session != nullptr);

    const BlockId block = doc->iterateBlocks().front();
    ed->view()->setCaretPosition(block, 5);  // right after "Hello"

    sendTextKeyEvent(ed->view(), QStringLiteral(","));

    // Read straight off the Session's own accessor — an external reader,
    // never through View — to prove the write-through actually happened.
    // NOTE: verified via TextAnchor::block() + offsetInBlock(), not
    // MarkoffDocument::blockAt() — see the P6.1 findings-log entry:
    // blockAt() resolves via latestBlockRanges/latestBlockAnchors, which
    // are never populated anywhere in markoff-core/src and so always
    // return nullopt; TextAnchor already carries its own origin block,
    // and offsetInBlock(BlockAnchor, TextAnchor) is the confirmed-D2-safe
    // accessor (P6.0 finding) that actually works here.
    const Selection sel = session->primarySelection();
    QVERIFY(sel.isEmpty());  // no selection in play, just a caret
    QVERIFY(!sel.active.isNull());
    QCOMPARE(sel.active.block(), block);
    QCOMPARE(doc->offsetInBlock(sel.active.block(), sel.active), 6);  // "Hello," -> byte 6

    // And it agrees with the View's own idea of where the caret is.
    QCOMPARE(ed->view()->caretBlock(), block);
    QCOMPARE(ed->view()->caretByteOffset(), 6);

    delete ed;
    delete doc;
}

void TstCanvasSessionSelection::arrow_key_motion_pushes_caret_to_session()
{
    auto *doc = new MarkoffDocument(2);
    doc->loadFromMarkdown("Hello world.\n");
    auto *ed = new EditorWidget;
    ed->resize(400, 300);
    ed->setDocument(doc);
    ed->show();
    QVERIFY(QTest::qWaitForWindowExposed(ed));
    ed->view()->setFocus();

    Session *session = ed->view()->session();
    QVERIFY(session != nullptr);

    const BlockId block = doc->iterateBlocks().front();
    ed->view()->setCaretPosition(block, 0);

    // Plain arrow-key motion never calls setCaret() (P6.1 finding — see
    // the plan's findings log): this is exactly the gap that would go
    // unpushed if pushSelectionToSession() were only wired into
    // setCaret() and the m_selectionAnchor mutation sites, as the task's
    // initial framing assumed.
    QTest::keyClick(ed->view(), Qt::Key_Right);
    QTest::keyClick(ed->view(), Qt::Key_Right);
    QTest::keyClick(ed->view(), Qt::Key_Right);

    QCOMPARE(ed->view()->caretByteOffset(), 3);

    const Selection sel = session->primarySelection();
    QVERIFY(!sel.active.isNull());
    QCOMPARE(doc->offsetInBlock(sel.active.block(), sel.active), 3);

    delete ed;
    delete doc;
}

void TstCanvasSessionSelection::shift_click_selection_pushes_range_to_session()
{
    auto *doc = new MarkoffDocument(3);
    doc->loadFromMarkdown("Hello world.\n");
    auto *ed = new EditorWidget;
    ed->resize(400, 300);
    ed->setDocument(doc);
    ed->show();
    QVERIFY(QTest::qWaitForWindowExposed(ed));
    ed->view()->setFocus();

    Session *session = ed->view()->session();
    QVERIFY(session != nullptr);

    const BlockId block = doc->iterateBlocks().front();
    ed->view()->setCaretPosition(block, 0);

    // Extend a selection with Shift+Right (goes through the same
    // m_selectionAnchor-then-move path a Shift+click drag uses).
    QTest::keyClick(ed->view(), Qt::Key_Right, Qt::ShiftModifier);
    QTest::keyClick(ed->view(), Qt::Key_Right, Qt::ShiftModifier);
    QTest::keyClick(ed->view(), Qt::Key_Right, Qt::ShiftModifier);
    QTest::keyClick(ed->view(), Qt::Key_Right, Qt::ShiftModifier);
    QTest::keyClick(ed->view(), Qt::Key_Right, Qt::ShiftModifier);

    QVERIFY(ed->view()->hasSelection());
    QCOMPARE(ed->view()->selectionAnchorByteOffset(), 0);
    QCOMPARE(ed->view()->caretByteOffset(), 5);

    const Selection sel = session->primarySelection();
    QVERIFY(!sel.isEmpty());
    QVERIFY(!sel.anchor.isNull());
    QVERIFY(!sel.active.isNull());
    QCOMPARE(doc->offsetInBlock(sel.anchor.block(), sel.anchor), 0);
    QCOMPARE(doc->offsetInBlock(sel.active.block(), sel.active), 5);

    delete ed;
    delete doc;
}

// P6.1 spec text: "After any model change, the view re-resolves the caret
// from the Session's stored anchor" — an anchor tracks content identity
// through an edit, strictly better information than clampCaret's plain
// "nearest surviving block by index" fallback. Typing earlier in the SAME
// block shifts every later byte offset; a pre-P6.1 view would have relied
// on clampCaret alone (index-based, still correct here since the block
// itself never disappears) — this test's real job is the next one, where
// the block identity actually changes.
void TstCanvasSessionSelection::model_change_reresolves_caret_from_session_anchor()
{
    auto *doc = new MarkoffDocument(4);
    doc->loadFromMarkdown("Hello world.\n");
    auto *ed = new EditorWidget;
    ed->resize(400, 300);
    ed->setDocument(doc);
    ed->show();
    QVERIFY(QTest::qWaitForWindowExposed(ed));
    ed->view()->setFocus();

    const BlockId block = doc->iterateBlocks().front();
    // Caret right before "world" (byte 6).
    ed->view()->setCaretPosition(block, 6);

    // A remote-style edit at the FRONT of the block: insert 3 bytes before
    // the caret's anchor. A plain byte-offset scheme would leave the caret
    // stuck at byte 6 (now inside "wor" instead of right before "world");
    // the anchor-based resolve should track the actual character.
    {
        Markoff::UndoLog::Transaction t(doc->d2UndoLog());
        doc->d2ApplyBufferEdit(block, 0, 0, QByteArray("Oh, "), t);
    }
    doc->flushPendingD2Changed();

    QCOMPARE(ed->view()->caretBlock(), block);
    QCOMPARE(ed->view()->caretByteOffset(), 10);  // "Oh, Hello " -> right before "world"

    delete ed;
    delete doc;
}

// guide §B.4 / plan P6.1: "where core doesn't repopulate the Session yet
// ... log the exact gap — do not patch core unprompted." undoD2()/
// redoD2() mutate the document and nothing else (T4/T5, queue #10 item
// 2) — they do NOT write a new Selection back to the Session, so the
// anchor a pre-undo push wrote often still resolves (if the undone edit
// didn't touch the caret's own block) but is not guaranteed to name the
// pre-edit position — the resolve is honest about whatever content
// happens to still be there, not a promise of restoration.
void TstCanvasSessionSelection::undo_falls_through_to_clamp_when_session_anchor_is_stale()
{
    auto *doc = new MarkoffDocument(5);
    doc->loadFromMarkdown("Hi\n");
    auto *ed = new EditorWidget;
    ed->resize(400, 300);
    ed->setDocument(doc);
    ed->show();
    QVERIFY(QTest::qWaitForWindowExposed(ed));
    ed->view()->setFocus();

    const BlockId block = doc->iterateBlocks().front();
    ed->view()->setCaretPosition(block, 2);  // end of "Hi"

    // Type " there" — caret pushes to byte 8 ("Hi there").
    sendTextKeyEvent(ed->view(), QStringLiteral(" there"));
    QCOMPARE(ed->view()->caretByteOffset(), 8);

    // Ctrl+Z: undoD2() removes " there" and nothing else. No selection
    // state is written back to the Session by core (the known B.4 gap) —
    // the Session still holds the pre-undo anchor (byte 8, "the character
    // just typed"), which no longer exists post-undo. onDocumentChanged's
    // resolve-from-session step must fail to resolve it and fall through
    // to clampCaret's plain byte-clamp, landing the caret at the block's
    // new (shorter) end rather than crashing or stranding it elsewhere.
    QTest::keyClick(ed->view(), Qt::Key_Z, Qt::ControlModifier);

    QCOMPARE(ed->view()->caretBlock(), block);
    QCOMPARE(doc->blockText(block), QByteArray("Hi"));
    QCOMPARE(ed->view()->caretByteOffset(), 2);  // clampCaret's byte-bound fallback

    delete ed;
    delete doc;
}

QTEST_MAIN(TstCanvasSessionSelection)
#include "tst_canvas_session_selection.moc"
