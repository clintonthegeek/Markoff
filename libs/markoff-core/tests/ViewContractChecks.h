// SPDX-License-Identifier: GPL-3.0-or-later
// Shared MarkdownView-contract assertions. Each leaf's contract test
// instantiates its concrete editor, loads FIXTURE, then calls these
// against the BASE pointer — the point is that the contract works
// polymorphically. Spec §10.
#pragma once
#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>

#include <markoff/core/Cmd/D2.h>
#include <markoff/core/EditorContext.h>
#include <markoff/core/MarkdownView.h>
#include <markoff/core/MarkoffDocument.h>

namespace ViewContract {

// 3 blocks; block 1 is a 3-line code block, so the flat-line model is:
// line 1 = "alpha one", lines 2-4 = code fence, line 5 = "omega end".
inline QByteArray fixture() {
    return QByteArray("alpha one\n\n```\ncode line\n```\n\nomega end");
}

inline void checkCursorRoundTrip(Markoff::MarkdownView *v) {
    QVERIFY(v->hasCursor());
    v->setCursorPosition({5, 3});
    const auto p = v->cursorPosition();
    QCOMPARE(p.line, 5);
    QCOMPARE(p.column, 3);
    // Park on line 1 first so a no-op implementation is distinguishable
    // from clamping (INVARIANTS rule 4: the check must be falsifiable).
    v->setCursorPosition({1, 1});
    QCOMPARE(v->cursorPosition().line, 1);
    v->setCursorPosition({9999, 1});            // clamps to last line, never no-ops
    QVERIFY(v->cursorPosition().line > 1);
}

inline void checkReadOnlyBlocksUndoAndKeepsBytes(Markoff::MarkdownView *v,
                                                 Markoff::MarkoffDocument *doc) {
    const QByteArray before = doc->serializeForSave();
    v->setReadOnly(true);
    QVERIFY(v->isReadOnly());
    QVERIFY(!v->hasEditing());
    v->undo();
    v->toggleBold();
    QCOMPARE(doc->serializeForSave(), before);
    v->setReadOnly(false);
}

inline void checkUndoRedoViaBase(Markoff::MarkdownView *v,
                                 Markoff::MarkoffDocument *doc) {
    const QByteArray before = doc->serializeForSave();
    doc->applyFlatEdit(0, 0, QByteArray("X"), Markoff::Origin::UserEdit);
    const QByteArray after = doc->serializeForSave();
    QVERIFY(after != before);
    v->undo();
    QCOMPARE(doc->serializeForSave(), before);
    v->redo();
    QCOMPARE(doc->serializeForSave(), after);
    v->undo();   // restore fixture for subsequent checks
}

// cursorPositionChanged (canvas production plan P3.2, 2026-08-14): general
// over any MarkdownView* — the base contract requires the signal fire when
// the caret genuinely moves and stay silent when a set lands on the SAME
// position (change-gated, mirrors checkContextChangedKindGated's shape for
// contextChanged). Uses the shared 5-line `fixture()` above.
inline void checkCursorPositionChangedSignal(Markoff::MarkdownView *v) {
    QSignalSpy spy(v, &Markoff::MarkdownView::cursorPositionChanged);

    v->setCursorPosition({1, 1});
    const int n0 = spy.count();
    v->setCursorPosition({5, 1});           // clamps onto "omega end"
    QTRY_VERIFY(spy.count() > n0);
    const auto args = spy.last();
    QCOMPARE(args.at(0).toInt(), v->cursorPosition().line);
    QCOMPARE(args.at(1).toInt(), v->cursorPosition().column);

    // Re-setting the SAME resulting position must NOT re-emit.
    const int n1 = spy.count();
    v->setCursorPosition(v->cursorPosition());
    QTest::qWait(20);
    QCOMPARE(spy.count(), n1);
}

inline void checkFontScaleSignal(Markoff::MarkdownView *v) {
    QSignalSpy spy(v, &Markoff::MarkdownView::fontScaleChanged);
    v->setFontScale(1.25);
    QCOMPARE(v->fontScale(), 1.25);
    QVERIFY(spy.count() >= 1);
    v->setFontScale(1.0);
}

// Spec §7: contextChanged is emitted on block kind change, and is
// change-gated (no re-emit when the caret stays in the same block).
//
// Fixture flat lines (in the widget — code fence stripped to content by D2):
//   line 1 → "alpha one"    (Paragraph, block 0)
//   line 2 → "code line"    (CodeBlock, block 1)
//   line 3 → "omega end"    (Paragraph, block 2)
//
// setCursorPosition({5, 1}) clamps to the last line → omega end (Paragraph).
// setCursorPosition({2, 1}) → code line (CodeBlock).
inline void checkContextChangedKindGated(Markoff::MarkdownView *v) {
    qRegisterMetaType<Markoff::EditorContext>();
    QSignalSpy spy(v, &Markoff::MarkdownView::contextChanged);

    // Park on a known block first (omega end → Paragraph).
    v->setCursorPosition({5, 1});
    QTRY_VERIFY(spy.count() >= 1);
    const auto ctx0 = spy.last().at(0).value<Markoff::EditorContext>();
    QCOMPARE(ctx0.blockKind, QString(Markoff::BlockKindNames::Paragraph));

    // Move within the SAME block (still omega end) — must NOT emit again.
    const int n = spy.count();
    v->setCursorPosition({5, 2});   // still clamped to the same last line
    QTest::qWait(20);
    QCOMPARE(spy.count(), n);       // change-gated → no new emit

    // Move to code block (line 2 in widget flat view).
    v->setCursorPosition({2, 1});
    QTRY_VERIFY(spy.count() > n);
    const auto ctx1 = spy.last().at(0).value<Markoff::EditorContext>();
    QCOMPARE(ctx1.blockKind, QString(Markoff::BlockKindNames::CodeBlock));
}

// Queue #15: a programmatic Cmd::changeKind on the block the caret is
// currently sitting in must refresh contextChanged even though the caret
// itself never moves — source/styled only recompute on cursorPositionChanged
// by default (spec §7 deviation), so this exercises the structuralEditSequence
// fallback both leaves wire on top of that.
inline void checkContextChangedOnStructuralKindChangeWithoutCaretMove(
        Markoff::MarkdownView *v, Markoff::MarkoffDocument *doc) {
    qRegisterMetaType<Markoff::EditorContext>();

    // Park on block 0 ("alpha one", Paragraph).
    v->setCursorPosition({1, 1});
    QTest::qWait(20);  // let any pending cursor-driven recompute settle

    const Markoff::BlockId block0 = doc->iterateBlocks().front();
    QCOMPARE(doc->blockKind(block0), Markoff::BlockKind::Paragraph);

    QSignalSpy spy(v, &Markoff::MarkdownView::contextChanged);
    const auto posBefore = v->cursorPosition();

    Markoff::Cmd::changeKind(*doc, block0, Markoff::BlockKind::Heading);

    QTRY_VERIFY(spy.count() >= 1);
    const auto ctx = spy.last().at(0).value<Markoff::EditorContext>();
    QCOMPARE(ctx.blockKind, QString(Markoff::BlockKindNames::Heading));
    // The caret genuinely did not move — proves this isn't just a
    // cursor-driven recompute coincidentally landing on the new kind.
    QCOMPARE(v->cursorPosition().line, posBefore.line);
    QCOMPARE(v->cursorPosition().column, posBefore.column);

    Markoff::Cmd::changeKind(*doc, block0, Markoff::BlockKind::Paragraph);  // restore
    QTest::qWait(20);
}

// Attach-window contract (canvas production plan P3.1, 2026-08-13): a
// cursor write issued by the consumer in the same call stack as
// `setDocument()` — i.e. immediately after it returns, with no event-loop
// spin in between — must stick. A leaf that schedules any deferred/async
// re-seed of the caret after attach (live's QML initial-focus seed is the
// motivating precedent; canvas has none — C2 forbids deferral there
// outright) would silently win the race and clobber it. `doc` must be the
// same document `v` is already attached to (re-attaching exercises the
// same teardown/setup path a leaf swap or reattach would take).
inline void checkAttachWindowCaretWriteSurvives(Markoff::MarkdownView *v,
                                                Markoff::MarkoffDocument *doc) {
    v->setDocument(doc);
    v->setCursorPosition({3, 1});
    // A leaf with no async re-seed has nothing to pump here — this is a
    // no-op for a fully synchronous setDocument(). It is included so the
    // check is a genuine test of "nothing queued wins the race" rather
    // than merely "nothing raced yet": a regression that defers part of
    // setDocument()'s caret handling onto the event queue would still be
    // caught, because that deferred step gets a chance to run before the
    // assertion below.
    QCoreApplication::processEvents();
    QCOMPARE(v->cursorPosition().line, 3);
}

}  // namespace ViewContract
