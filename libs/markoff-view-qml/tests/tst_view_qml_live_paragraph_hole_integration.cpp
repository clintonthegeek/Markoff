// SPDX-License-Identifier: GPL-3.0-or-later
//
// Stage 4 follow-up — paragraph-hole reify-focus integration test.
//
// The unit tests in `tst_view_qml_live_paragraph_hole.cpp` drive the layer
// + structural-key handler directly and never observe the QQuickView focus
// routing. This file covers the user-reported bug: typing the first
// character into the hole must place the cursor *inside* the new (now-real)
// paragraph delegate at qtPos == 1, not on the previous block.

#include <QGuiApplication>
#include <QHash>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QSignalSpy>
#include <QTest>
#include <QVariant>
#include <QtPlugin>

Q_IMPORT_PLUGIN(org_markoff_view_qmlPlugin)

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>
#include <markoff-foundation/Origin.h>
#include <markoff-foundation/Theme.h>

class TstLiveParagraphHoleIntegration : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initTestCase() {
        if (!qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
            QSKIP("QGuiApplication required for QQuickView");
        }
    }

    void enter_then_keystroke_routes_focus_into_new_paragraph() {
        Markoff::MarkoffDocument doc(1);

        QQuickView view;
        view.engine()->rootContext()->setContextProperty("doc", &doc);
        view.engine()->rootContext()->setContextProperty(
            "themeCtx", QVariant::fromValue(Markoff::Theme::defaultLight()));
        view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.resize(600, 800);

        QQmlComponent component(view.engine());
        component.setData(
            "import QtQuick\n"
            "import QtQuick.Controls\n"
            "import org.markoff.view.qml\n"
            "MarkoffEditor {\n"
            "    width: 600; height: 800\n"
            "    document: doc\n"
            "    theme: themeCtx\n"
            "    mode: \"live\"\n"
            "}\n",
            QUrl());
        if (component.isError()) qWarning() << component.errors();
        QVERIFY(!component.isError());

        auto *root = qobject_cast<QQuickItem *>(component.create());
        QVERIFY(root);
        root->setParentItem(view.contentItem());
        root->setParent(view.contentItem());
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        // Seed: "Hello" → 1 row.
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        Markoff::MarkoffEdit ed;
        ed.oldStart = 0; ed.oldEnd = 0;
        ed.newText = QByteArrayLiteral("Hello");
        doc.applyLocalEdit({ ed });
        QVERIFY(parseSpy.wait(2000));

        QQuickItem *listView = root->findChild<QQuickItem *>(
            QStringLiteral("listView"));
        QVERIFY(listView);
        QTRY_COMPARE(listView->property("count").toInt(), 1);

        QMetaObject::invokeMethod(listView, "forceLayout");
        view.grabWindow();

        auto *contentItem = qvariant_cast<QQuickItem *>(
            listView->property("contentItem"));
        QVERIFY(contentItem);

        auto findDelegate = [contentItem](int blockIndex) -> QQuickItem * {
            for (QQuickItem *child : contentItem->childItems()) {
                QVariant bi = child->property("blockIndex");
                if (bi.isValid() && bi.toInt() == blockIndex) {
                    return child;
                }
            }
            return nullptr;
        };

        QQuickItem *para0 = nullptr;
        QTRY_VERIFY((para0 = findDelegate(0)) != nullptr);

        // Focus into paragraph 0 at end-of-text.
        QMetaObject::invokeMethod(para0, "focusAtEnd");
        QTest::qWait(50);

        // Sanity: focus is inside paragraph 0's TextEdit.
        QQuickItem *focused = view.activeFocusItem();
        QVERIFY(focused);

        // Press End to ensure cursor is at end (idempotent), then Enter.
        QTest::keyClick(&view, Qt::Key_End);
        QTest::qWait(20);
        QTest::keyClick(&view, Qt::Key_Return);

        // Wait for the parse round-trip + hole materialisation: count should
        // reach 2 (the original paragraph + the hole row).
        QTRY_COMPARE_WITH_TIMEOUT(listView->property("count").toInt(), 2, 5000);

        QMetaObject::invokeMethod(listView, "forceLayout");
        view.grabWindow();

        // Wait for the hole's onBlockHoleCreated focus-routing (Qt.callLater
        // x 2) to land focus inside the hole-row delegate before we type the
        // first character.
        auto focusInsideDelegate = [&](int blockIndex) -> bool {
            QQuickItem *fi = view.activeFocusItem();
            if (!fi) return false;
            QQuickItem *p = fi;
            while (p) {
                QVariant bi = p->property("blockIndex");
                if (bi.isValid() && bi.toInt() == blockIndex) return true;
                p = p->parentItem();
            }
            return false;
        };
        // Spin the event loop until focus lands inside delegate 1 (the hole
        // row). The retry-loop in LiveView.qml's onBlockHoleCreated handles
        // delegate-materialisation timing under QQuickView; we just need to
        // give it time.
        bool ok = false;
        for (int i = 0; i < 80 && !ok; ++i) {
            QMetaObject::invokeMethod(listView, "forceLayout");
            view.grabWindow();
            QTest::qWait(25);
            ok = focusInsideDelegate(1);
        }
        QVERIFY(ok);

        // Type 'x' into the hole. This triggers reifyBlockHole, which:
        //   - synchronously drops the hole (count: 2→1)
        //   - applyLocalEdit → parse round-trip (count: 1→2)
        //   - emits holeReified(viewRow, qtPos) → LiveView routes focus.
        QTest::keyClick(&view, Qt::Key_X);

        // Doc should eventually contain "Hello\n\nx".
        QTRY_COMPARE_WITH_TIMEOUT(doc.toMarkdownUtf8(), QByteArray("Hello\n\nx"), 5000);

        // listView.count must be 2 again after the parse round-trip.
        QTRY_COMPARE_WITH_TIMEOUT(listView->property("count").toInt(), 2, 5000);

        QMetaObject::invokeMethod(listView, "forceLayout");
        view.grabWindow();

        // The new paragraph delegate (block 1) must exist.
        QQuickItem *para1 = nullptr;
        QTRY_VERIFY((para1 = findDelegate(1)) != nullptr);

        // Verify focus eventually lands inside delegate 1 with cursor at qtPos
        // == 1. The Qt.callLater × 2 dance in LiveView.qml means we may need
        // to spin the event loop a few iterations.
        QTRY_VERIFY_WITH_TIMEOUT(focusInsideDelegate(1), 5000);

        // cursorPosition on the new delegate should be 1 (just past the typed 'x').
        QTRY_COMPARE_WITH_TIMEOUT(para1->property("cursorPosition").toInt(), 1, 5000);
    }
};

QTEST_MAIN(TstLiveParagraphHoleIntegration)
#include "tst_view_qml_live_paragraph_hole_integration.moc"
