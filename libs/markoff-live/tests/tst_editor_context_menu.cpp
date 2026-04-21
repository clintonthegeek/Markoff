// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#include <QAction>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QMenu>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

#include <markoff/Editor.h>
#include <markoff/EditorContext.h>

using namespace Markoff;

namespace {
// Menus created by contextMenuEvent are modal (menu.exec()). Under offscreen
// QPA the exec() still enters a local event loop, so we schedule a close()
// before dispatching the event. The aboutToShowContextMenu signal fires
// synchronously before exec(), so the spy captures it regardless of how the
// exec exits.
void dispatchContextMenu(Editor &ed, const QPoint &localPos)
{
    // Close any about-to-be-shown menu so exec() returns immediately.
    auto closeAny = [&ed]() {
        const auto menus = ed.findChildren<QMenu *>();
        for (QMenu *m : menus) {
            if (m->isVisible())
                m->close();
        }
    };
    QTimer::singleShot(0, &ed, closeAny);
    QTimer::singleShot(20, &ed, closeAny);

    QContextMenuEvent ev(QContextMenuEvent::Mouse,
                          localPos,
                          ed.mapToGlobal(localPos));
    QCoreApplication::sendEvent(&ed, &ev);
    // Let scheduled close()s drain so exec() returns.
    QCoreApplication::processEvents();
}
} // namespace

class TstEditorContextMenu : public QObject
{
    Q_OBJECT

private slots:
    void aboutToShowContextMenu_firesOnRightClick();
    void subscribedAction_canBeAppended();
    void readOnlyEditor_doesNotFireSignal();
};

void TstEditorContextMenu::aboutToShowContextMenu_firesOnRightClick()
{
    Editor ed;
    ed.setPlainText(QStringLiteral("paragraph"));
    ed.resize(400, 300);
    ed.show();
    QVERIFY(QTest::qWaitForWindowActive(&ed));
    QTest::qWait(50);

    QSignalSpy spy(&ed, &Editor::aboutToShowContextMenu);
    QVERIFY(spy.isValid());

    dispatchContextMenu(ed, QPoint(10, 10));

    QCOMPARE(spy.count(), 1);
}

void TstEditorContextMenu::subscribedAction_canBeAppended()
{
    Editor ed;
    ed.setPlainText(QStringLiteral("paragraph"));
    ed.resize(400, 300);
    ed.show();
    QVERIFY(QTest::qWaitForWindowActive(&ed));
    QTest::qWait(50);

    bool subscriberFired = false;
    int menuChildCountBeforeSub = -1;
    int menuChildCountAfterSub = -1;

    connect(&ed, &Editor::aboutToShowContextMenu, &ed,
            [&](QMenu *menu, const EditorContext &ctx, const QPoint &) {
        Q_UNUSED(ctx);
        subscriberFired = true;
        menuChildCountBeforeSub = menu->actions().count();
        menu->addAction(QStringLiteral("Phase-C6-appended"));
        menuChildCountAfterSub = menu->actions().count();
    });

    dispatchContextMenu(ed, QPoint(10, 10));

    QVERIFY(subscriberFired);
    // The subscriber saw a non-empty menu (built-ins already appended) and
    // successfully added a new action.
    QVERIFY2(menuChildCountBeforeSub > 0,
             "expected built-ins to be present when signal fires");
    QCOMPARE(menuChildCountAfterSub, menuChildCountBeforeSub + 1);
}

void TstEditorContextMenu::readOnlyEditor_doesNotFireSignal()
{
    Editor ed;
    ed.setPlainText(QStringLiteral("paragraph"));
    ed.setReadOnly(true);
    ed.resize(400, 300);
    ed.show();
    QVERIFY(QTest::qWaitForWindowActive(&ed));
    QTest::qWait(50);

    QSignalSpy spy(&ed, &Editor::aboutToShowContextMenu);

    dispatchContextMenu(ed, QPoint(10, 10));

    QCOMPARE(spy.count(), 0);  // read-only returns early; no menu shown
}

QTEST_MAIN(TstEditorContextMenu)
#include "tst_editor_context_menu.moc"
