// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/live/LiveListModelBinding.h>
#include <markoff/live/LiveCursorState.h>

class TestSelectionDelete : public QObject {
    Q_OBJECT
private slots:
    void deletes_within_block() {
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("hello world\n");
        auto *sv = binding.cursorState();
        sv->begin(0, 5);   // before " world"
        sv->extend(0, 11); // after "world"
        sv->deleteSelection();
        // Document should now contain "hello" (B1: blockText is content-only, no trailing '\n')
        const QByteArray flat = doc.blockText(doc.iterateBlocks()[0]);
        QCOMPARE(flat, QByteArray("hello"));
        QVERIFY(!sv->hasSelection());
    }

    void noop_when_no_selection() {
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("x\n");
        const quint64 seqBefore = doc.d2EditSequence();
        binding.cursorState()->deleteSelection();
        QCOMPARE(doc.d2EditSequence(), seqBefore);
    }
};

QTEST_MAIN(TestSelectionDelete)
#include "tst_live_render_selection_delete.moc"
