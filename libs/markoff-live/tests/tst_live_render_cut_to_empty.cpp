// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QCoreApplication>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/live/LiveListModelBinding.h>
#include <markoff/live/LiveClipboardController.h>

class TestCutToEmpty : public QObject {
    Q_OBJECT
private slots:
    void selectAll_then_cut_leaves_one_empty_paragraph() {
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("para A\n\npara B\n");
        Markoff::Live::LiveClipboardController cc;
        cc.setDocument(&doc);
        cc.setSelectionView(binding.cursorState());
        cc.setModel(binding.model());

        binding.cursorState()->selectAll();
        cc.cut();
        // The deleteSelection() path schedules a debounced d2DocumentChanged;
        // drain the event loop so the model update fires before asserting.
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();

        QCOMPARE(binding.model()->rowCount(), 1);
        QCOMPARE(binding.model()->recordAt(0).text, QString());
    }
};

QTEST_MAIN(TestCutToEmpty)
#include "tst_live_render_cut_to_empty.moc"
