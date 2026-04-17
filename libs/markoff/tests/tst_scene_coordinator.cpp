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
    void roundTripPreservesBlankLinesAroundFence();
    void roundTripPreservesNoBlankLineAroundFence();
    void roundTripPreservesMultipleBlankLinesAroundFence();
    void roundTripPreservesBlankLinesAroundImage();
    void roundTripPreservesTrailingNewlines();
    void roundTripPureText();
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

/// Round-trip fidelity: `toPlainText()` (which calls
/// `SceneCoordinator::toMarkdown()`) must reproduce the exact source
/// markdown, byte for byte, when no edits have been made. Historically
/// `interItemNewlines()` hardcoded the gap between a text item and a
/// block item to "\n\n", losing the real blank-line count from the
/// source.

static QString roundTrip(const QString &input)
{
    Editor editor;
    editor.resize(800, 400);
    editor.setPlainText(input);
    editor.show();
    QApplication::processEvents();
    return editor.toPlainText();
}

void TstSceneCoordinator::roundTripPreservesBlankLinesAroundFence()
{
    const QString src = QStringLiteral("A\n\n```\ncode\n```\n\nB");
    QCOMPARE(roundTrip(src), src);
}

void TstSceneCoordinator::roundTripPreservesNoBlankLineAroundFence()
{
    const QString src = QStringLiteral("A\n```\ncode\n```\nB");
    QCOMPARE(roundTrip(src), src);
}

void TstSceneCoordinator::roundTripPreservesMultipleBlankLinesAroundFence()
{
    const QString src = QStringLiteral("A\n\n\n```\ncode\n```\n\n\nB");
    QCOMPARE(roundTrip(src), src);
}

void TstSceneCoordinator::roundTripPreservesBlankLinesAroundImage()
{
    const QString src = QStringLiteral("before\n\n\n![alt](img.png)\n\n\nafter");
    QCOMPARE(roundTrip(src), src);
}

void TstSceneCoordinator::roundTripPreservesTrailingNewlines()
{
    const QString src = QStringLiteral("A\n\nB\n\n");
    QCOMPARE(roundTrip(src), src);
}

void TstSceneCoordinator::roundTripPureText()
{
    const QString src = QStringLiteral("line1\nline2\n\nline4\n\n\nline7");
    QCOMPARE(roundTrip(src), src);
}

QTEST_MAIN(TstSceneCoordinator)
#include "tst_scene_coordinator.moc"
