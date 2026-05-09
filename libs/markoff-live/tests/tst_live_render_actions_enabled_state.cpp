// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/live/LiveListModelBinding.h>
#include <markoff/live/LiveActionController.h>
#include <markoff/live/LiveClipboardController.h>
#include <markoff/live/LiveSelectionView.h>

class TestActionsEnabledState : public QObject {
    Q_OBJECT
private slots:
    void paste_disabled_when_clipboard_empty() {
        QApplication::clipboard()->clear();
        Markoff::MarkoffDocument doc(quint16(42));
        doc.loadFromMarkdown("hello\n");
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);

        Markoff::Live::LiveClipboardController cc;
        cc.setDocument(&doc);
        cc.setSelectionView(binding.selectionView());
        cc.setModel(binding.model());

        Markoff::Live::LiveActionController ac;
        ac.setDocument(&doc);
        ac.setSelectionView(binding.selectionView());
        ac.setClipboardController(&cc);

        QVERIFY(!ac.pasteAction()->isEnabled());
    }

    void paste_enabled_after_copy() {
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        // setDocument before loadFromMarkdown so the model populates.
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

        cc.copy();
        // updateEnabledStates reads clipboard state synchronously; call it
        // directly because QClipboard::changed is not guaranteed to fire
        // synchronously under the offscreen test QPA.
        ac.updateEnabledStates();
        QVERIFY(ac.pasteAction()->isEnabled());
    }

    void cut_copy_disabled_when_no_selection() {
        Markoff::MarkoffDocument doc(quint16(42));
        doc.loadFromMarkdown("hello\n");
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);

        Markoff::Live::LiveActionController ac;
        ac.setDocument(&doc);
        ac.setSelectionView(binding.selectionView());

        QVERIFY(!ac.cutAction()->isEnabled());
        QVERIFY(!ac.copyAction()->isEnabled());
    }

    void cut_copy_enabled_with_selection() {
        Markoff::MarkoffDocument doc(quint16(42));
        doc.loadFromMarkdown("hello\n");
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        binding.selectionView()->begin(0, 0);
        binding.selectionView()->extend(0, 3);

        Markoff::Live::LiveActionController ac;
        ac.setDocument(&doc);
        ac.setSelectionView(binding.selectionView());

        QVERIFY(ac.cutAction()->isEnabled());
        QVERIFY(ac.copyAction()->isEnabled());
    }
};

QTEST_MAIN(TestActionsEnabledState)
#include "tst_live_render_actions_enabled_state.moc"
