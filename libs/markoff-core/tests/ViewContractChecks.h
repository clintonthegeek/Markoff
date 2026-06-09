// SPDX-License-Identifier: GPL-3.0-or-later
// Shared MarkdownView-contract assertions. Each leaf's contract test
// instantiates its concrete editor, loads FIXTURE, then calls these
// against the BASE pointer — the point is that the contract works
// polymorphically. Spec §10.
#pragma once
#include <QSignalSpy>
#include <QTest>

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

inline void checkFontScaleSignal(Markoff::MarkdownView *v) {
    QSignalSpy spy(v, &Markoff::MarkdownView::fontScaleChanged);
    v->setFontScale(1.25);
    QCOMPARE(v->fontScale(), 1.25);
    QVERIFY(spy.count() >= 1);
    v->setFontScale(1.0);
}

}  // namespace ViewContract
