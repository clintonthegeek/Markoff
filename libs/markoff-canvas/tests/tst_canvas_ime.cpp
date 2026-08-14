// SPDX-License-Identifier: GPL-3.0-or-later
//
// T8 — IME composition (exit E6).
//
// Ports the five audit-L7 scenarios
// (docs/specs/2026-05-21-audit-L7-ime-composition.md,
// tst_live_render_ime_composition_qml) to this leaf's actual shape: the
// live leaf defers all edits behind a `m_composing` flag and swaps the
// whole block wholesale on commit; this leaf never buffers anything —
// `View::inputMethodEvent` mirrors QWidgetTextControlPrivate's ordering
// (see plan T8's "Qt upstream reference" note) and writes at most one
// `d2ApplyBufferEdit` per event, at the caret, with the preedit string
// spliced into the caret block's QTextLayout via `setPreeditArea` and
// never into the document.
//
// IME events are synthesized via QInputMethodEvent and delivered to the
// production View through QCoreApplication::sendEvent, same protocol the
// live suite uses.

#include <QInputMethodEvent>
#include <QTest>

#include <markoff/canvas/View.h>
#include <markoff/core/MarkoffDocument.h>

using Markoff::BlockId;
using Markoff::Canvas::View;

namespace {

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

/// Click at the end of the block's text to place the caret there — same
/// pattern as tst_canvas_typing's click-to-place setup.
void placeCaretAtEnd(View &view, BlockId block)
{
    const QRectF rect = view.blockRect(block);
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(int(rect.x()) + int(rect.width()) - 5, int(rect.y()) + 8));
    QTest::keyClick(&view, Qt::Key_End);
}

}  // namespace

class TstCanvasIme : public QObject {
    Q_OBJECT

private slots:
    void commit_after_preedit_lands_in_buffer();
    void preedit_then_replace_then_commit_records_single_edit();
    void preedit_then_empty_no_commit_leaves_buffer_unchanged();
    void commit_into_non_empty_block_inserts_at_caret();
    void composing_lifecycle_matches_qt_native();
    void read_only_blocks_ime_commit();
};

void TstCanvasIme::commit_after_preedit_lands_in_buffer()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("seed\n");
    const BlockId block = doc.iterateBlocks().front();

    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    placeCaretAtEnd(view, block);

    // Composing: CRDT must remain untouched.
    sendPreedit(view, QStringLiteral("ab"));
    QVERIFY(view.isComposing());
    QCOMPARE(doc.blockText(block), QByteArray("seed"));

    // Commit: buffer reflects the committed text, composing ends.
    sendCommit(view, QStringLiteral("ab"));
    QVERIFY(!view.isComposing());
    QCOMPARE(doc.blockText(block), QByteArray("seedab"));
}

void TstCanvasIme::preedit_then_replace_then_commit_records_single_edit()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("seed\n");
    const BlockId block = doc.iterateBlocks().front();

    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    placeCaretAtEnd(view, block);

    const quint64 seqBeforeComposing = doc.blockEditSequence(block);

    sendPreedit(view, QStringLiteral("a"));
    QCOMPARE(doc.blockText(block), QByteArray("seed"));
    sendPreedit(view, QStringLiteral("ab"));
    QCOMPARE(doc.blockText(block), QByteArray("seed"));
    // Neither preedit step touched the buffer, so its edit sequence has
    // not moved either.
    QCOMPARE(doc.blockEditSequence(block), seqBeforeComposing);

    sendCommit(view, QStringLiteral("ab"));
    QCOMPARE(doc.blockText(block), QByteArray("seedab"));
    // Exactly one buffer edit landed for the whole composition.
    QCOMPARE(doc.blockEditSequence(block), seqBeforeComposing + 1);
}

void TstCanvasIme::preedit_then_empty_no_commit_leaves_buffer_unchanged()
{
    // A cancelled composition: preedit starts, then the IME withdraws it
    // (empty preedit, no commit). Falsify (plan T8): route this into a
    // document write — this test must fail if the preedit ever lands in
    // blockText().
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("seed\n");
    const BlockId block = doc.iterateBlocks().front();

    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    placeCaretAtEnd(view, block);

    sendPreedit(view, QStringLiteral("a"));
    QVERIFY(view.isComposing());

    sendPreedit(view, QString());
    QVERIFY(!view.isComposing());
    QCOMPARE(doc.blockText(block), QByteArray("seed"));
}

void TstCanvasIme::commit_into_non_empty_block_inserts_at_caret()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("hello\n");
    const BlockId block = doc.iterateBlocks().front();

    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    placeCaretAtEnd(view, block);

    sendPreedit(view, QStringLiteral("WO"));
    QCOMPARE(doc.blockText(block), QByteArray("hello"));

    sendCommit(view, QStringLiteral("WORLD"));
    QCOMPARE(doc.blockText(block), QByteArray("helloWORLD"));
}

void TstCanvasIme::composing_lifecycle_matches_qt_native()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("x\n");
    const BlockId block = doc.iterateBlocks().front();

    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    placeCaretAtEnd(view, block);

    QVERIFY(!view.isComposing());

    sendPreedit(view, QStringLiteral("p"));
    QVERIFY(view.isComposing());
    QCOMPARE(view.preeditText(), QStringLiteral("p"));

    sendCommit(view, QStringLiteral("p"));
    QVERIFY(!view.isComposing());

    sendPreedit(view, QStringLiteral("q"));
    QVERIFY(view.isComposing());

    sendPreedit(view, QString());
    QVERIFY(!view.isComposing());
}

// P3.3's named falsification target: the IME-commit read-only gate.
// setReadOnly(true) must block a commit from ever reaching the document —
// no partial application, no preedit splice either (View::inputMethodEvent
// disables composition outright while read-only, see its doc comment).
void TstCanvasIme::read_only_blocks_ime_commit()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("seed\n");
    const BlockId block = doc.iterateBlocks().front();

    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    placeCaretAtEnd(view, block);

    view.setReadOnly(true);
    QVERIFY(view.isReadOnly());

    sendPreedit(view, QStringLiteral("ab"));
    QVERIFY(!view.isComposing());  // composition itself is disabled
    QCOMPARE(doc.blockText(block), QByteArray("seed"));

    sendCommit(view, QStringLiteral("ab"));
    QCOMPARE(doc.blockText(block), QByteArray("seed"));  // never mutated

    view.setReadOnly(false);
    sendCommit(view, QStringLiteral("ab"));
    QCOMPARE(doc.blockText(block), QByteArray("seedab"));  // gate lifts cleanly
}

QTEST_MAIN(TstCanvasIme)
#include "tst_canvas_ime.moc"
