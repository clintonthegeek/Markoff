// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/live/LiveListModelBinding.h>
#include <markoff/live/LiveSelectionView.h>

class TestSelectionSelectAll : public QObject {
    Q_OBJECT
private slots:
    void selects_full_document() {
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("para A\n\npara B\n");

        auto *sv = binding.selectionView();
        QVERIFY(!sv->hasSelection());
        sv->selectAll();
        QVERIFY(sv->hasSelection());
        // First block in a cross-block selection: covered from 0 to end-of-block (INT_MAX).
        const auto first = sv->rangeForBlock(0);
        QCOMPARE(first.x(), 0);
        QCOMPARE(first.y(), INT_MAX);
        // Last block: selection covers from 0 to end of "para B" (length 6).
        const auto last = sv->rangeForBlock(1);
        QCOMPARE(last.x(), 0);
        QCOMPARE(last.y(), 6);
    }

    void noop_on_empty_doc() {
        // selectAll() on an empty doc: anchor == active == (0,0), so
        // hasSelection() stays false — there's nothing to cover.
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("");
        binding.selectionView()->selectAll();
        QVERIFY(!binding.selectionView()->hasSelection());
    }
};

QTEST_MAIN(TestSelectionSelectAll)
#include "tst_live_render_selection_select_all.moc"
