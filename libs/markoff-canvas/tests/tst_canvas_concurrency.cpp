// SPDX-License-Identifier: GPL-3.0-or-later
//
// P6.3 — concurrency tests (D5 part 1, plan §Phase 6).
//
// Two tests:
//
//   1. ime_composition_survives_concurrent_remote_edit_to_same_block(): the
//      load-bearing one. Opens an IME composition on document A (via a real
//      EditorWidget/View, exactly the tst_canvas_ime.cpp protocol), then
//      applies a concurrent remote edit to the SAME block — a second
//      MarkoffDocument replica (B) makes a local d2ApplyBufferEdit and its
//      resulting MarkoffOps are fed into A via applyRemoteOps, same shape
//      as markoff-core/tests/d5/tst_d5_two_doc_convergence.cpp's
//      wireRouter(). Asserts the composition survives the remote edit
//      (isComposing()/preeditText() unaffected — nothing in View clears
//      them on a document change) and that committing lands the text at
//      the CORRECT anchor: wherever the caret's Session-tracked CRDT
//      anchor (P6.1) resolves to in the post-remote-edit text, not a
//      stale pre-remote-edit byte offset. This is the C1 promise under
//      test: no re-entrance guard exists or is needed for a remote edit
//      to land safely mid-composition.
//
//   2. concurrent_gremlin_fuzz_converges_without_workaround(): a scripted,
//      seeded fuzz loop (collabtext's tst_gc.cpp convergence-fuzz shape,
//      adapted) alternating real local input through View's normal event
//      path with scripted remote edits fed through a second replica. Only
//      character-level edits to a single, pre-existing block are scripted
//      (no structural split/merge) — kept simple deliberately (see the
//      findings log entry this task appends: multi-block/structural
//      fuzzing is a natural follow-up, not required by this task's exit
//      criterion). After every iteration the caret is checked against the
//      CURRENT document (never a stale/vanished block); after all
//      iterations, a live third replica (mirroring every op A's own local
//      edits AND B's remote edits produce, applied as they're produced —
//      the "simpler" alternative the plan explicitly sanctions in place
//      of replaying A's full op history after the fact) must match A's
//      final content exactly.
//
// Both replicas' initial content is built via one document's own D2 ops,
// routed once to the other before any concurrent edits start (the D5 note
// at the top of tst_d5_two_doc_convergence.cpp: independent
// loadFromMarkdown() calls diverge in CRDT history and can never converge
// by op exchange alone).

#include <QInputMethodEvent>
#include <QRandomGenerator>
#include <QTest>

#include <markoff/canvas/EditorWidget.h>
#include <markoff/canvas/View.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/MarkoffOp.h>
#include <markoff/core/UndoLog.h>

using Markoff::BlockId;
using Markoff::BlockKind;
using Markoff::Canvas::EditorWidget;
using Markoff::Canvas::View;
using Markoff::MarkoffBundleMeta;
using Markoff::MarkoffDocument;
using Markoff::MarkoffOp;

namespace {

// ---- IME helpers (same protocol as tst_canvas_ime.cpp) --------------------

void sendPreedit(View &view, const QString &preeditStr)
{
    QList<QInputMethodEvent::Attribute> attrs;
    QInputMethodEvent ev(preeditStr, attrs);
    QCoreApplication::sendEvent(&view, &ev);
}

void sendCommit(View &view, const QString &commitStr)
{
    QList<QInputMethodEvent::Attribute> attrs;
    QInputMethodEvent ev(QString(), attrs);
    ev.setCommitString(commitStr);
    QCoreApplication::sendEvent(&view, &ev);
}

// Drives the real keyPressEvent path (View reads event->text(), not the key
// code, for printable input) — same helper as
// tst_canvas_session_selection.cpp's sendTextKeyEvent.
void sendTextKeyEvent(QWidget *w, const QString &text)
{
    QKeyEvent press(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, text);
    QCoreApplication::sendEvent(w, &press);
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_unknown, Qt::NoModifier, text);
    QCoreApplication::sendEvent(w, &release);
}

/// Builds a two-block-free single-paragraph document on `doc` (replica
/// identity is whatever `doc` was constructed with) containing `text`,
/// returning the new block's id. Mirrors tst_d5_two_doc_convergence.cpp's
/// per-op construction (never loadFromMarkdown for a doc that must later
/// converge with a peer via op exchange).
BlockId buildInitialBlock(MarkoffDocument &doc, const QByteArray &text)
{
    Markoff::UndoLog::Transaction t(doc.d2UndoLog());
    const BlockId block = doc.d2InsertBlock(BlockId{}, BlockKind::Paragraph, t);
    doc.d2ApplyBufferEdit(block, 0, 0, text, t);
    return block;
}

}  // namespace

class TstCanvasConcurrency : public QObject {
    Q_OBJECT

private slots:
    void ime_composition_survives_concurrent_remote_edit_to_same_block();
    void concurrent_gremlin_fuzz_converges_without_workaround();
};

// ---------------------------------------------------------------------------
// Test 1 — IME composition vs. concurrent remote edit to the same block.
// ---------------------------------------------------------------------------

void TstCanvasConcurrency::ime_composition_survives_concurrent_remote_edit_to_same_block()
{
    MarkoffDocument docA(quint16(101));
    MarkoffDocument docB(quint16(102));

    // Seed both replicas with identical content: build it on A, route the
    // resulting ops to B once (initial sync only — no ongoing wiring).
    QMetaObject::Connection initialSync =
        QObject::connect(&docA, &MarkoffDocument::localOpsProduced, &docB,
                          [&docB](QList<MarkoffOp> ops, MarkoffBundleMeta meta) {
                              docB.applyRemoteOps(std::move(ops), std::move(meta));
                          });

    const BlockId block = buildInitialBlock(docA, QByteArrayLiteral("Hello world\n"));
    docA.flushPendingD2Changed();
    QObject::disconnect(initialSync);

    QCOMPARE(docB.blockText(block), QByteArray("Hello world\n"));  // sanity: converged seed

    EditorWidget ed;
    ed.resize(400, 300);
    ed.setDocument(&docA);
    ed.show();
    QVERIFY(QTest::qWaitForWindowExposed(&ed));
    View *view = ed.view();
    view->setFocus();

    // Caret right after "Hello", before the space (byte 5).
    view->setCaretPosition(block, 5);
    QCOMPARE(view->caretByteOffset(), 5);

    // Start composing: nothing lands in the buffer yet.
    sendPreedit(*view, QStringLiteral("XY"));
    QVERIFY(view->isComposing());
    const QString preeditBefore = view->preeditText();
    QCOMPARE(preeditBefore, QStringLiteral("XY"));
    QCOMPARE(docA.blockText(block), QByteArray("Hello world\n"));

    // Concurrent remote edit to the SAME block, from B: insert "Oh, " at
    // the very front — 4 bytes land BEFORE the caret's anchor.
    QList<MarkoffOp> remoteOps;
    MarkoffBundleMeta remoteMeta;
    QObject::connect(&docB, &MarkoffDocument::localOpsProduced,
                      [&remoteOps, &remoteMeta](QList<MarkoffOp> ops, MarkoffBundleMeta meta) {
                          remoteOps = std::move(ops);
                          remoteMeta = std::move(meta);
                      });
    {
        Markoff::UndoLog::Transaction t(docB.d2UndoLog());
        docB.d2ApplyBufferEdit(block, 0, 0, QByteArrayLiteral("Oh, "), t);
    }
    docB.flushPendingD2Changed();
    QVERIFY(!remoteOps.isEmpty());

    docA.applyRemoteOps(remoteOps, remoteMeta);
    docA.flushPendingD2Changed();

    QCOMPARE(docA.blockText(block), QByteArray("Oh, Hello world\n"));

    // The composition must survive the remote edit untouched — no
    // re-entrance guard drops or clears it (C1).
    QVERIFY(view->isComposing());
    QCOMPARE(view->preeditText(), preeditBefore);

    // Commit: must land where the caret's CRDT anchor now resolves
    // ("right after Hello", now byte 9 — not the stale pre-edit byte 5).
    sendCommit(*view, QStringLiteral("XY"));
    QVERIFY(!view->isComposing());
    QCOMPARE(docA.blockText(block), QByteArray("Oh, HelloXY world\n"));
}

// ---------------------------------------------------------------------------
// Test 2 — seeded gremlin/fuzz convergence test.
// ---------------------------------------------------------------------------
//
// N and the seed: 300 iterations is comfortably a few-seconds run (each
// iteration is either one synthetic key event or one small D2 buffer edit
// plus a flush — no layout stress, no perf-sized documents) while still
// giving the interleaving enough room to hit edge byte offsets (0, near-
// end, adjacent to the trailing '\n') many times over. Seed is fixed
// (not std::random_device, unlike collabtext's tst_gc.cpp) so a failure
// is byte-for-byte reproducible without needing to capture a logged seed
// from a CI run first; it's still logged via qDebug() to match tst_gc's
// convention and to make a *reported* failure's seed traceable even
// though it never changes run to run.
void TstCanvasConcurrency::concurrent_gremlin_fuzz_converges_without_workaround()
{
    constexpr quint32 kSeed = 0xC0FFEE42u;
    constexpr int kIterations = 300;
    qDebug() << "Seed:" << kSeed << "Iterations:" << kIterations;
    QRandomGenerator rng(kSeed);

    MarkoffDocument docA(quint16(201));  // local replica, driven through View
    MarkoffDocument docB(quint16(202));  // remote replica, scripted directly
    MarkoffDocument docC(quint16(203));  // live mirror: every op A produces
                                         // (its own local edits) AND every
                                         // op B produces (fed to A as
                                         // "remote"), applied as they occur —
                                         // the plan's sanctioned simpler
                                         // alternative to replaying A's op
                                         // history into a fresh replica
                                         // after the fact.

    QMetaObject::Connection initialSyncB =
        QObject::connect(&docA, &MarkoffDocument::localOpsProduced, &docB,
                          [&docB](QList<MarkoffOp> ops, MarkoffBundleMeta meta) {
                              docB.applyRemoteOps(std::move(ops), std::move(meta));
                          });
    QMetaObject::Connection initialSyncC =
        QObject::connect(&docA, &MarkoffDocument::localOpsProduced, &docC,
                          [&docC](QList<MarkoffOp> ops, MarkoffBundleMeta meta) {
                              docC.applyRemoteOps(std::move(ops), std::move(meta));
                          });

    const BlockId block = buildInitialBlock(docA, QByteArrayLiteral("The quick fox\n"));
    docA.flushPendingD2Changed();
    QObject::disconnect(initialSyncB);
    QObject::disconnect(initialSyncC);

    QCOMPARE(docB.blockText(block), QByteArray("The quick fox\n"));
    QCOMPARE(docC.blockText(block), QByteArray("The quick fox\n"));

    EditorWidget ed;
    ed.resize(400, 300);
    ed.setDocument(&docA);
    ed.show();
    QVERIFY(QTest::qWaitForWindowExposed(&ed));
    View *view = ed.view();
    view->setFocus();
    view->setCaretPosition(block, 0);

    // From here on: A's OWN local edits mirror live into C via
    // localOpsProduced (no re-connect needed — Qt::UniqueConnection isn't
    // used above so this is a fresh, always-on connection distinct from
    // the one-shot initial-sync connections just disconnected).
    QObject::connect(&docA, &MarkoffDocument::localOpsProduced, &docC,
                      [&docC](QList<MarkoffOp> ops, MarkoffBundleMeta meta) {
                          docC.applyRemoteOps(std::move(ops), std::move(meta));
                      });

    for (int i = 0; i < kIterations; ++i) {
        const quint32 pick = rng.bounded(6u);

        if (pick == 0) {
            // Local: type one printable ASCII letter at the caret.
            const QChar ch = QChar(QLatin1Char('a' + int(rng.bounded(26u))));
            sendTextKeyEvent(view, QString(ch));
        } else if (pick == 1) {
            QTest::keyClick(view, Qt::Key_Left);
        } else if (pick == 2) {
            QTest::keyClick(view, Qt::Key_Right);
        } else if (pick == 3) {
            // In-block backspace only — never at byte 0 (would attempt a
            // structural boundary merge; this test scopes itself to
            // single-block character-level concurrency, see the class
            // doc comment / findings log).
            if (view->caretByteOffset() > 0)
                QTest::keyClick(view, Qt::Key_Backspace);
        } else if (pick == 4) {
            // In-block forward-delete only — never on the trailing '\n'.
            const QByteArray text = docA.blockText(block);
            if (view->caretByteOffset() < text.size() - 1)
                QTest::keyClick(view, Qt::Key_Delete);
        } else {
            // Remote: B makes one small scripted edit (insert or delete a
            // single ASCII byte, never touching the trailing '\n') against
            // ITS OWN current view of the block — which has diverged from
            // A's (B never receives A's local edits, only the reverse) —
            // exactly the concurrent-editors-without-live-sync shape D5
            // exists to handle.
            const QByteArray bText = docB.blockText(block);
            const int editableLen = qMax(0, bText.size() - 1);  // exclude '\n'
            QList<MarkoffOp> ops;
            MarkoffBundleMeta meta;
            QMetaObject::Connection capture = QObject::connect(
                &docB, &MarkoffDocument::localOpsProduced,
                [&ops, &meta](QList<MarkoffOp> o, MarkoffBundleMeta m) {
                    ops = std::move(o);
                    meta = std::move(m);
                });
            {
                Markoff::UndoLog::Transaction t(docB.d2UndoLog());
                if (editableLen == 0 || rng.bounded(2u) == 0) {
                    const int at = editableLen == 0 ? 0 : int(rng.bounded(quint32(editableLen + 1)));
                    const QChar ch = QChar(QLatin1Char('A' + int(rng.bounded(26u))));
                    docB.d2ApplyBufferEdit(block, uint32_t(at), 0,
                                           QString(ch).toUtf8(), t);
                } else {
                    const int at = int(rng.bounded(quint32(editableLen)));
                    docB.d2ApplyBufferEdit(block, uint32_t(at), 1, QByteArray(), t);
                }
            }
            docB.flushPendingD2Changed();
            QObject::disconnect(capture);

            if (!ops.isEmpty()) {
                docA.applyRemoteOps(ops, meta);
                docA.flushPendingD2Changed();
                // FALSIFY (P6.3, throwaway): skip feeding remote ops to the
                // comparison replica — docC.applyRemoteOps(ops, meta);
            }
        }

        // No caret stranding: the caret's block must exist in the CURRENT
        // document after every single iteration, local or remote.
        QVERIFY2(view->blockIndexOf(view->caretBlock()) >= 0,
                 qPrintable(QStringLiteral("iteration %1: caret block vanished").arg(i)));
    }

    // Convergence: A (the document actually driven through View for the
    // whole run) and C (the live mirror of every op A produced/received)
    // must agree exactly.
    QCOMPARE(docA.iterateBlocks().size(), docC.iterateBlocks().size());
    QCOMPARE(docA.blockText(block), docC.blockText(block));
}

QTEST_MAIN(TstCanvasConcurrency)
#include "tst_canvas_concurrency.moc"
