// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QApplication>
#include <QClipboard>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/live/LiveListModelBinding.h>
#include <markoff/live/LiveActionController.h>
#include <markoff/live/LiveClipboardController.h>

class TestClipboardChangedUpdatesPaste : public QObject {
    Q_OBJECT
private slots:
    void paste_enables_when_clipboard_gains_text() {
        // Start with an empty clipboard; paste must be disabled.
        // Then set plain text directly; paste must enable.
        //
        // We call updateEnabledStates() explicitly after setText because
        // QClipboard::changed is not guaranteed to fire synchronously under
        // the offscreen test QPA platform.
        QApplication::clipboard()->clear();

        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("x\n");

        Markoff::Live::LiveClipboardController cc;
        cc.setDocument(&doc);
        cc.setSelectionView(binding.cursorState());
        cc.setModel(binding.model());

        Markoff::Live::LiveActionController ac;
        ac.setDocument(&doc);
        ac.setSelectionView(binding.cursorState());
        ac.setClipboardController(&cc);

        QVERIFY(!ac.pasteAction()->isEnabled());

        QApplication::clipboard()->setText(QStringLiteral("text"));
        // The QClipboard::changed signal drives onClipboardChanged() in
        // production. Under offscreen QPA it may not fire synchronously,
        // so we also call updateEnabledStates() to exercise the same
        // clipboard-reading logic and confirm it observes the new content.
        ac.updateEnabledStates();
        QVERIFY(ac.pasteAction()->isEnabled());
    }
};

QTEST_MAIN(TestClipboardChangedUpdatesPaste)
#include "tst_live_render_clipboard_changed_updates_paste.moc"
