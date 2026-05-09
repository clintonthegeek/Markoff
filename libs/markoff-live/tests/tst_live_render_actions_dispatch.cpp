// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <QApplication>
#include <QClipboard>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/live/LiveListModelBinding.h>
#include <markoff/live/LiveActionController.h>
#include <markoff/live/LiveClipboardController.h>

class TestActionsDispatch : public QObject {
    Q_OBJECT
private slots:
    void save_action_emits_save_requested() {
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("x\n");

        Markoff::Live::LiveActionController ac;
        ac.setDocument(&doc);
        ac.setSelectionView(binding.selectionView());

        QSignalSpy spy(&ac, &Markoff::Live::LiveActionController::saveRequested);
        ac.saveAction()->trigger();
        QCOMPARE(spy.count(), 1);
    }

    void select_all_action_selects_document() {
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("hello\n");
        QCOMPARE(binding.model()->rowCount(), 1);

        Markoff::Live::LiveActionController ac;
        ac.setDocument(&doc);
        ac.setSelectionView(binding.selectionView());

        QVERIFY(!binding.selectionView()->hasSelection());
        ac.selectAllAction()->trigger();
        QVERIFY(binding.selectionView()->hasSelection());
    }

    void copy_action_writes_clipboard() {
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("hello\n");
        QCOMPARE(binding.model()->rowCount(), 1);
        binding.selectionView()->begin(0, 0);
        binding.selectionView()->extend(0, 5);

        Markoff::Live::LiveClipboardController cc;
        cc.setDocument(&doc);
        cc.setSelectionView(binding.selectionView());
        cc.setModel(binding.model());

        Markoff::Live::LiveActionController ac;
        ac.setDocument(&doc);
        ac.setSelectionView(binding.selectionView());
        ac.setClipboardController(&cc);

        QApplication::clipboard()->clear();
        ac.copyAction()->trigger();
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("hello"));
    }
};

QTEST_MAIN(TestActionsDispatch)
#include "tst_live_render_actions_dispatch.moc"
