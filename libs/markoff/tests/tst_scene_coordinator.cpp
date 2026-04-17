// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QSignalSpy>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>

#include <markoff/Editor.h>
#include "MarkdownTextItem.h"
#include "SceneCoordinator.h"
#include "SelectableItem.h"
#include "TextControl.h"

using namespace Markoff;

class TstSceneCoordinator : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void reparsedHandlerEditIsNotSwallowed();
};

/// Regression: `SceneCoordinator::reparse()` used to schedule the
/// `m_inReparse = false` clear via `QTimer::singleShot(0, ...)`, so
/// any synchronous `reparsed()` handler that edited text saw
/// `m_inReparse == true` in `onItemTextChanged` and had its follow-up
/// reparse suppressed. The fix clears the flag before emitting;
/// handler edits then properly restart the 150 ms debounce timer.
///
/// We detect this by counting `reparsed()` emissions. Without the fix,
/// the handler edit fires during emit → `onItemTextChanged` bails out
/// on `m_inReparse` → the debounce timer never restarts → only ONE
/// reparse ever emits. With the fix, `m_inReparse` is false when the
/// handler's `insertText` runs → the timer restarts → a second reparse
/// fires ~150 ms later → TWO emissions.
void TstSceneCoordinator::reparsedHandlerEditIsNotSwallowed()
{
    Editor editor;
    editor.resize(600, 400);
    editor.setPlainText(QStringLiteral("alpha"));
    editor.show();
    QApplication::processEvents();

    auto *coord = editor.coordinatorForTesting();
    QVERIFY(coord);

    // Begin counting AFTER setPlainText's initial reparse has settled.
    QSignalSpy spy(coord, &SceneCoordinator::reparsed);

    // One-shot handler that edits the last text item on the first
    // reparse. If the bug is present, the edit's follow-up reparse
    // never fires because `onItemTextChanged` sees `m_inReparse == true`.
    bool injected = false;
    QObject::connect(coord, &SceneCoordinator::reparsed,
                     &editor, [&]() {
        if (injected) return;
        injected = true;
        MarkdownTextItem *ti = nullptr;
        for (auto *item : coord->items()) {
            if (item->isTextItem())
                ti = static_cast<MarkdownTextItem *>(item);
        }
        if (!ti) return;
        QTextCursor c(ti->document());
        c.movePosition(QTextCursor::End);
        c.insertText(QStringLiteral(" beta"));
    });

    // Trigger the reparse that will fire the handler.
    MarkdownTextItem *ti = nullptr;
    for (auto *item : coord->items()) {
        if (item->isTextItem()) {
            ti = static_cast<MarkdownTextItem *>(item);
            break;
        }
    }
    QVERIFY(ti);
    {
        QTextCursor c(ti->document());
        c.movePosition(QTextCursor::End);
        c.insertText(QStringLiteral("!"));
    }

    // Wait: 150 ms for the first debounce, handler fires, its edit
    // should restart the debounce, 150 ms more for the second reparse.
    // 500 ms total leaves generous margin.
    QTest::qWait(500);

    // Two reparses must have emitted: the one that fired the handler,
    // plus the one caused by the handler's edit.
    QVERIFY2(spy.count() >= 2,
             qPrintable(QStringLiteral("expected ≥2 reparse emissions, got %1")
                        .arg(spy.count())));
}

QTEST_MAIN(TstSceneCoordinator)
#include "tst_scene_coordinator.moc"
