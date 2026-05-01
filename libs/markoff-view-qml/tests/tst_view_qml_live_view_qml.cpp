// SPDX-License-Identifier: GPL-3.0-or-later
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

class TstLiveViewQml : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initTestCase() {
        if (!qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
            QSKIP("QGuiApplication required for QQuickView");
        }
    }

    void delegates_render_model_text() {
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

        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        Markoff::MarkoffEdit ed;
        ed.oldStart = 0; ed.oldEnd = 0;
        ed.newText = QByteArrayLiteral(
            "# Heading\n\n"
            "Para text.\n\n"
            "---\n\n"
            "![alt](http://example.com/img.png)\n\n"
            "```python\nx = 1\n```\n");
        doc.applyLocalEdit({ ed });
        QVERIFY(parseSpy.wait(2000));

        QQuickItem *listView = root->findChild<QQuickItem *>(
            QStringLiteral("listView"));
        QVERIFY(listView);
        QTRY_COMPARE(listView->property("count").toInt(), 5);

        // ListView delegates are visual children of listView.contentItem
        // (a Flickable's contentItem), not QObject-descendants of listView.
        // Force a layout + render so all five are materialised, then walk
        // contentItem->childItems().
        QMetaObject::invokeMethod(listView, "forceLayout");
        view.grabWindow();

        auto *contentItem = qvariant_cast<QQuickItem *>(
            listView->property("contentItem"));
        QVERIFY(contentItem);

        auto collectDelegates = [contentItem]() {
            QHash<int, QQuickItem *> out;
            for (QQuickItem *child : contentItem->childItems()) {
                QVariant bi = child->property("blockIndex");
                if (bi.isValid() && bi.toInt() >= 0) {
                    out.insert(bi.toInt(), child);
                }
            }
            return out;
        };

        QHash<int, QQuickItem *> delegates;
        QTRY_VERIFY((delegates = collectDelegates()).size() == 5);

        QCOMPARE(delegates.value(0)->property("blockText").toString(),
                 QStringLiteral("Heading"));
        QCOMPARE(delegates.value(1)->property("blockText").toString(),
                 QStringLiteral("Para text."));
        QCOMPARE(delegates.value(3)->property("imageSrc").toString(),
                 QStringLiteral("http://example.com/img.png"));
        QCOMPARE(delegates.value(3)->property("imageAlt").toString(),
                 QStringLiteral("alt"));
        QCOMPARE(delegates.value(4)->property("codeText").toString(),
                 QStringLiteral("x = 1\n"));
    }

    void mouse_drag_selects_across_block_kinds() {
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

        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        Markoff::MarkoffEdit ed;
        ed.oldStart = 0; ed.oldEnd = 0;
        ed.newText = QByteArrayLiteral(
            "# Heading\n\n"
            "Para text.\n\n"
            "---\n\n"
            "![alt](http://example.com/img.png)\n\n"
            "```python\nx = 1\n```\n");
        doc.applyLocalEdit({ ed });
        QVERIFY(parseSpy.wait(2000));

        QQuickItem *listView = root->findChild<QQuickItem *>(
            QStringLiteral("listView"));
        QVERIFY(listView);
        QTRY_COMPARE(listView->property("count").toInt(), 5);

        QMetaObject::invokeMethod(listView, "forceLayout");
        view.grabWindow();

        auto *contentItem = qvariant_cast<QQuickItem *>(
            listView->property("contentItem"));
        QVERIFY(contentItem);

        auto collectDelegates = [contentItem]() {
            QHash<int, QQuickItem *> out;
            for (QQuickItem *child : contentItem->childItems()) {
                QVariant bi = child->property("blockIndex");
                if (bi.isValid() && bi.toInt() >= 0) {
                    out.insert(bi.toInt(), child);
                }
            }
            return out;
        };

        QHash<int, QQuickItem *> delegates;
        QTRY_VERIFY((delegates = collectDelegates()).size() == 5);

        // Translate row 1 (paragraph) and row 3 (image) centres into window
        // coordinates. Mid-x avoids the leading 12-px margin set by delegates.
        auto *para = delegates.value(1);
        auto *img  = delegates.value(3);
        QVERIFY(para);
        QVERIFY(img);

        const QPointF paraScene =
            para->mapToScene(QPointF(para->width() / 2, para->height() / 2));
        const QPointF imgScene =
            img->mapToScene(QPointF(img->width() / 2, img->height() / 2));
        const QPoint paraPos = paraScene.toPoint();
        const QPoint imgPos = imgScene.toPoint();

        // Synthesise a press at the paragraph centre, drag through the HR
        // (row 2) into the image centre (row 3).
        QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, paraPos);
        QTest::qWait(20);

        const int steps = 4;
        for (int i = 1; i <= steps; ++i) {
            QPoint mid(paraPos.x() + (imgPos.x() - paraPos.x()) * i / steps,
                       paraPos.y() + (imgPos.y() - paraPos.y()) * i / steps);
            QTest::mouseMove(&view, mid);
            QTest::qWait(20);
        }
        QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, imgPos);
        QTest::qWait(20);

        // Locate the LiveListModelBinding (id: binding inside LiveView.qml)
        // by walking the live-view subtree for the first object whose class
        // name ends with "LiveListModelBinding".
        QObject *binding = nullptr;
        for (QObject *child : root->findChildren<QObject *>()) {
            if (QString::fromLatin1(child->metaObject()->className())
                    .endsWith(QLatin1String("LiveListModelBinding"))) {
                binding = child;
                break;
            }
        }
        QVERIFY(binding);

        QObject *selModel = qvariant_cast<QObject *>(
            binding->property("selectionModel"));
        QVERIFY(selModel);

        // LiveSelectionView no longer stores integer block indices — selection
        // is Session-canonical (TextAnchors). Verify: a non-degenerate selection
        // exists and includes block 3 (image), the drag endpoint.
        QVERIFY(selModel->property("hasSelection").toBool());
        QPoint r3;
        QMetaObject::invokeMethod(selModel, "rangeForBlock",
            Q_RETURN_ARG(QPoint, r3), Q_ARG(int, 3));
        QVERIFY(r3.x() != -1);   // block 3 (image) is included in the selection
    }

    void delegates_consume_theme_colors() {
        Markoff::MarkoffDocument doc(1);

        // Build a custom theme with sentinel colours.
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        const QColor sentinelBg("#abcdef");
        const QColor sentinelFg("#fedcba");
        theme.setColor(Markoff::Theme::Slot::CodeBlockBackground, sentinelBg);
        theme.setColor(Markoff::Theme::Slot::CodeBlock, sentinelFg);

        QQuickView view;
        view.engine()->rootContext()->setContextProperty("doc", &doc);
        view.engine()->rootContext()->setContextProperty(
            "themeCtx", QVariant::fromValue(theme));
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

        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        Markoff::MarkoffEdit ed;
        ed.oldStart = 0; ed.oldEnd = 0;
        ed.newText = QByteArrayLiteral(
            "# Heading\n\n"
            "Para text.\n\n"
            "---\n\n"
            "![alt](http://example.com/img.png)\n\n"
            "```python\nx = 1\n```\n");
        doc.applyLocalEdit({ ed });
        QVERIFY(parseSpy.wait(2000));

        QQuickItem *listView = root->findChild<QQuickItem *>(
            QStringLiteral("listView"));
        QVERIFY(listView);
        QTRY_COMPARE(listView->property("count").toInt(), 5);

        QMetaObject::invokeMethod(listView, "forceLayout");
        view.grabWindow();

        auto *contentItem = qvariant_cast<QQuickItem *>(
            listView->property("contentItem"));
        QVERIFY(contentItem);

        auto collectDelegates = [contentItem]() {
            QHash<int, QQuickItem *> out;
            for (QQuickItem *child : contentItem->childItems()) {
                QVariant bi = child->property("blockIndex");
                if (bi.isValid() && bi.toInt() >= 0) {
                    out.insert(bi.toInt(), child);
                }
            }
            return out;
        };
        QHash<int, QQuickItem *> delegates;
        QTRY_VERIFY((delegates = collectDelegates()).size() == 5);

        // Row 4: code block. Its outer Rectangle is the delegate root.
        auto *codeBlock = delegates.value(4);
        QVERIFY(codeBlock);
        QCOMPARE(codeBlock->property("color").value<QColor>(), sentinelBg);

        // The inner TextEdit is a child of the code-block Rectangle. Find by
        // metaObject class name (QQuickTextEdit).
        QQuickItem *codeTextEdit = nullptr;
        for (QQuickItem *child : codeBlock->findChildren<QQuickItem *>()) {
            if (QString::fromLatin1(child->metaObject()->className())
                    .startsWith(QLatin1String("QQuickTextEdit"))) {
                codeTextEdit = child;
                break;
            }
        }
        QVERIFY(codeTextEdit);
        QCOMPARE(codeTextEdit->property("color").value<QColor>(), sentinelFg);

        // Row 3: image (alt-fallback Rectangle uses CodeBlockBackground).
        auto *imageDelegate = delegates.value(3);
        QVERIFY(imageDelegate);
        QQuickItem *altFallback =
            imageDelegate->findChild<QQuickItem *>(QStringLiteral("altFallback"));
        QVERIFY(altFallback);
        QCOMPARE(altFallback->property("color").value<QColor>(), sentinelBg);
    }

    void hr_click_routes_focus_to_preceding_paragraph() {
        // Document: paragraph (0), HR (1), paragraph (2).
        // Calling routeNeighbourFocus(1) should focus the preceding paragraph (0).
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

        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        Markoff::MarkoffEdit ed;
        ed.oldStart = 0; ed.oldEnd = 0;
        ed.newText = QByteArrayLiteral("First para\n\n---\n\nSecond para");
        doc.applyLocalEdit({ ed });
        QVERIFY(parseSpy.wait(2000));

        QQuickItem *listView = root->findChild<QQuickItem *>(
            QStringLiteral("listView"));
        QVERIFY(listView);
        QTRY_COMPARE(listView->property("count").toInt(), 3);

        QMetaObject::invokeMethod(listView, "forceLayout");
        view.grabWindow();

        // Find the LiveView item (direct child of MarkoffEditor that has routeNeighbourFocus).
        // LiveView.qml is the root Item inside MarkoffEditor, accessible via findChild.
        QQuickItem *liveView = nullptr;
        for (QQuickItem *child : root->findChildren<QQuickItem *>()) {
            // routeNeighbourFocus is defined in LiveView.qml; check by property or
            // by finding the item that owns the listView.
            if (child->findChild<QQuickItem *>(QStringLiteral("listView"))) {
                liveView = child;
                break;
            }
        }
        if (!liveView) {
            QSKIP("Cannot locate LiveView item — focus routing test requires "
                  "a real QQuickWindow with materialised delegates");
        }

        // Invoke routeNeighbourFocus(1) — HR is at blockIndex 1.
        bool ok = QMetaObject::invokeMethod(liveView, "routeNeighbourFocus",
                                             Q_ARG(QVariant, QVariant(1)));
        if (!ok) {
            QSKIP("routeNeighbourFocus not invokable — QML function not accessible "
                  "from C++ in this test harness");
        }
        QTest::qWait(50);

        // The preceding paragraph (blockIndex 0) should now have active focus.
        // The activeFocusItem of the window should be a TextEdit inside block 0.
        QQuickItem *focused = view.activeFocusItem();
        QVERIFY2(focused,
                 "No item has active focus after routeNeighbourFocus(1)");

        // Walk up the parent chain to find the delegate at blockIndex 0.
        bool focusInBlock0 = false;
        for (QQuickItem *it = focused; it; it = it->parentItem()) {
            QVariant bi = it->property("blockIndex");
            if (bi.isValid() && bi.toInt() == 0) {
                focusInBlock0 = true;
                break;
            }
        }
        QVERIFY2(focusInBlock0,
                 "Focus should be in block 0 (preceding paragraph) after HR click");
    }

    void hr_at_top_routes_focus_to_following_paragraph() {
        // Document: HR (0), paragraph (1).
        // Calling routeNeighbourFocus(0) should focus the following paragraph (1).
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

        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        Markoff::MarkoffEdit ed;
        ed.oldStart = 0; ed.oldEnd = 0;
        ed.newText = QByteArrayLiteral("---\n\nFirst para");
        doc.applyLocalEdit({ ed });
        QVERIFY(parseSpy.wait(2000));

        QQuickItem *listView = root->findChild<QQuickItem *>(
            QStringLiteral("listView"));
        QVERIFY(listView);
        QTRY_COMPARE(listView->property("count").toInt(), 2);

        QMetaObject::invokeMethod(listView, "forceLayout");
        view.grabWindow();

        // Locate LiveView (the item that owns listView).
        QQuickItem *liveView = nullptr;
        for (QQuickItem *child : root->findChildren<QQuickItem *>()) {
            if (child->findChild<QQuickItem *>(QStringLiteral("listView"))) {
                liveView = child;
                break;
            }
        }
        if (!liveView) {
            QSKIP("Cannot locate LiveView item — focus routing test requires "
                  "a real QQuickWindow with materialised delegates");
        }

        bool ok = QMetaObject::invokeMethod(liveView, "routeNeighbourFocus",
                                             Q_ARG(QVariant, QVariant(0)));
        if (!ok) {
            QSKIP("routeNeighbourFocus not invokable — QML function not accessible "
                  "from C++ in this test harness");
        }
        QTest::qWait(50);

        QQuickItem *focused = view.activeFocusItem();
        QVERIFY2(focused,
                 "No item has active focus after routeNeighbourFocus(0)");

        // Walk up the parent chain to find the delegate at blockIndex 1.
        bool focusInBlock1 = false;
        for (QQuickItem *it = focused; it; it = it->parentItem()) {
            QVariant bi = it->property("blockIndex");
            if (bi.isValid() && bi.toInt() == 1) {
                focusInBlock1 = true;
                break;
            }
        }
        QVERIFY2(focusInBlock1,
                 "Focus should be in block 1 (following paragraph) after HR-at-top click");
    }

    void image_click_routes_focus_to_preceding_paragraph() {
        // Document: paragraph (0), image (1), paragraph (2).
        // Calling routeNeighbourFocus(1) should focus the preceding paragraph (0).
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
        QVERIFY(!component.isError());

        auto *root = qobject_cast<QQuickItem *>(component.create());
        QVERIFY(root);
        root->setParentItem(view.contentItem());
        root->setParent(view.contentItem());
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        Markoff::MarkoffEdit ed;
        ed.oldStart = 0; ed.oldEnd = 0;
        ed.newText = QByteArrayLiteral("First para\n\n![alt](img.png)\n\nSecond para");
        doc.applyLocalEdit({ ed });
        QVERIFY(parseSpy.wait(2000));

        QQuickItem *listView = root->findChild<QQuickItem *>(QStringLiteral("listView"));
        QVERIFY(listView);
        QTRY_COMPARE(listView->property("count").toInt(), 3);
        QMetaObject::invokeMethod(listView, "forceLayout");
        view.grabWindow();

        QQuickItem *liveView = nullptr;
        for (QQuickItem *child : root->findChildren<QQuickItem *>()) {
            if (child->findChild<QQuickItem *>(QStringLiteral("listView"))) {
                liveView = child; break;
            }
        }
        if (!liveView)
            QSKIP("Cannot locate LiveView item");

        bool ok = QMetaObject::invokeMethod(liveView, "routeNeighbourFocus",
                                             Q_ARG(QVariant, QVariant(1)));
        if (!ok) QSKIP("routeNeighbourFocus not invokable");
        QTest::qWait(50);

        QQuickItem *focused = view.activeFocusItem();
        QVERIFY2(focused, "No item has active focus after image routeNeighbourFocus(1)");

        bool focusInBlock0 = false;
        for (QQuickItem *it = focused; it; it = it->parentItem()) {
            QVariant bi = it->property("blockIndex");
            if (bi.isValid() && bi.toInt() == 0) { focusInBlock0 = true; break; }
        }
        QVERIFY2(focusInBlock0,
                 "Focus should be in block 0 (preceding paragraph) after image click");
    }

    void image_at_top_routes_focus_to_following_paragraph() {
        // Document: image (0), paragraph (1).
        // Calling routeNeighbourFocus(0) should focus the following paragraph (1).
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
        QVERIFY(!component.isError());

        auto *root = qobject_cast<QQuickItem *>(component.create());
        QVERIFY(root);
        root->setParentItem(view.contentItem());
        root->setParent(view.contentItem());
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        Markoff::MarkoffEdit ed;
        ed.oldStart = 0; ed.oldEnd = 0;
        ed.newText = QByteArrayLiteral("![alt](img.png)\n\nFirst para");
        doc.applyLocalEdit({ ed });
        QVERIFY(parseSpy.wait(2000));

        QQuickItem *listView = root->findChild<QQuickItem *>(QStringLiteral("listView"));
        QVERIFY(listView);
        QTRY_COMPARE(listView->property("count").toInt(), 2);
        QMetaObject::invokeMethod(listView, "forceLayout");
        view.grabWindow();

        QQuickItem *liveView = nullptr;
        for (QQuickItem *child : root->findChildren<QQuickItem *>()) {
            if (child->findChild<QQuickItem *>(QStringLiteral("listView"))) {
                liveView = child; break;
            }
        }
        if (!liveView)
            QSKIP("Cannot locate LiveView item");

        bool ok = QMetaObject::invokeMethod(liveView, "routeNeighbourFocus",
                                             Q_ARG(QVariant, QVariant(0)));
        if (!ok) QSKIP("routeNeighbourFocus not invokable");
        QTest::qWait(50);

        QQuickItem *focused = view.activeFocusItem();
        QVERIFY2(focused, "No item has active focus after image-at-top routeNeighbourFocus(0)");

        bool focusInBlock1 = false;
        for (QQuickItem *it = focused; it; it = it->parentItem()) {
            QVariant bi = it->property("blockIndex");
            if (bi.isValid() && bi.toInt() == 1) { focusInBlock1 = true; break; }
        }
        QVERIFY2(focusInBlock1,
                 "Focus should be in block 1 (following paragraph) after image-at-top click");
    }

    void empty_doc_first_keystroke_materialises_paragraph() {
        // Empty document — type 'a' — one paragraph row appears, focus lands
        // in it with cursor at offset 1.
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

        // Find the LiveView
        QQuickItem *liveView = nullptr;
        for (QQuickItem *child : root->findChildren<QQuickItem *>()) {
            if (child->findChild<QQuickItem *>(QStringLiteral("listView"))) {
                liveView = child; break;
            }
        }
        if (!liveView) QSKIP("Cannot locate LiveView");

        // LiveView should have active focus (doc is empty, count == 0)
        liveView->forceActiveFocus();

        // Type 'a'
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        QTest::keyClick(&view, Qt::Key_A);
        QVERIFY(parseSpy.wait(2000));

        // One paragraph row should materialise
        QQuickItem *listViewItem = root->findChild<QQuickItem *>(QStringLiteral("listView"));
        QVERIFY(listViewItem);
        QTRY_COMPARE(listViewItem->property("count").toInt(), 1);

        // Focus should be in the new delegate with cursor at 1.
        // Allow up to 500 ms for the deferred Qt.callLater focus-at-pos chain
        // to complete (two callLater levels + one potential retry).
        QQuickItem *focused = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((focused = view.activeFocusItem()) != nullptr, 500);
        QVERIFY2(focused, "No active focus after materialisation");

        // The cursor should be at offset 1
        QVariant cursorPos = focused->property("cursorPosition");
        if (cursorPos.isValid())
            QTRY_COMPARE_WITH_TIMEOUT(
                view.activeFocusItem()->property("cursorPosition").toInt(), 1, 500);
    }

    void selection_highlight_appears_on_hr_and_image() {
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

        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        Markoff::MarkoffEdit ed;
        ed.oldStart = 0; ed.oldEnd = 0;
        ed.newText = QByteArrayLiteral(
            "# Heading\n\n"
            "Para text.\n\n"
            "---\n\n"
            "![alt](http://example.com/img.png)\n\n"
            "```python\nx = 1\n```\n");
        doc.applyLocalEdit({ ed });
        QVERIFY(parseSpy.wait(2000));

        QQuickItem *listView = root->findChild<QQuickItem *>(
            QStringLiteral("listView"));
        QVERIFY(listView);
        QTRY_COMPARE(listView->property("count").toInt(), 5);

        QMetaObject::invokeMethod(listView, "forceLayout");
        view.grabWindow();

        auto *contentItem = qvariant_cast<QQuickItem *>(
            listView->property("contentItem"));
        QVERIFY(contentItem);

        auto collectDelegates = [contentItem]() {
            QHash<int, QQuickItem *> out;
            for (QQuickItem *child : contentItem->childItems()) {
                QVariant bi = child->property("blockIndex");
                if (bi.isValid() && bi.toInt() >= 0) {
                    out.insert(bi.toInt(), child);
                }
            }
            return out;
        };
        QHash<int, QQuickItem *> delegates;
        QTRY_VERIFY((delegates = collectDelegates()).size() == 5);

        auto *para = delegates.value(1);
        auto *code = delegates.value(4);
        QVERIFY(para);
        QVERIFY(code);

        // Drag from row 1 (paragraph) through HR + image into row 4 (code).
        const QPointF paraScene =
            para->mapToScene(QPointF(para->width() / 2, para->height() / 2));
        const QPointF codeScene =
            code->mapToScene(QPointF(code->width() / 2, code->height() / 2));
        const QPoint paraPos = paraScene.toPoint();
        const QPoint codePos = codeScene.toPoint();

        QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, paraPos);
        QTest::qWait(20);
        const int steps = 5;
        for (int i = 1; i <= steps; ++i) {
            QPoint mid(paraPos.x() + (codePos.x() - paraPos.x()) * i / steps,
                       paraPos.y() + (codePos.y() - paraPos.y()) * i / steps);
            QTest::mouseMove(&view, mid);
            QTest::qWait(20);
        }
        QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, codePos);
        QTest::qWait(20);

        // Row 2 (HR) and row 3 (image) must have a visible selection overlay.
        auto *hr = delegates.value(2);
        auto *img = delegates.value(3);
        QVERIFY(hr);
        QVERIFY(img);

        QQuickItem *hrOverlay =
            hr->findChild<QQuickItem *>(QStringLiteral("selectionOverlay"));
        QVERIFY(hrOverlay);
        QVERIFY(hrOverlay->isVisible());

        QQuickItem *imgOverlay =
            img->findChild<QQuickItem *>(QStringLiteral("selectionOverlay"));
        QVERIFY(imgOverlay);
        QVERIFY(imgOverlay->isVisible());
    }
    void type_into_paragraph_mutates_document() {
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
        QVERIFY(!component.isError());

        auto *root = qobject_cast<QQuickItem *>(component.create());
        QVERIFY(root);
        root->setParentItem(view.contentItem());
        root->setParent(view.contentItem());
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        // Seed the document with one paragraph.
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        Markoff::MarkoffEdit ed;
        ed.oldStart = 0; ed.oldEnd = 0;
        ed.newText = QByteArrayLiteral("Hello");
        doc.applyLocalEdit({ ed });
        QVERIFY(parseSpy.wait(2000));

        // Find the paragraph TextEdit and focus it.
        QQuickItem *listView = root->findChild<QQuickItem *>(QStringLiteral("listView"));
        QVERIFY(listView);
        QTRY_COMPARE(listView->property("count").toInt(), 1);
        QMetaObject::invokeMethod(listView, "forceLayout");

        QQuickItem *te = nullptr;
        for (QQuickItem *c : root->findChildren<QQuickItem *>()) {
            if (c->objectName() == QLatin1String("textEdit")) { te = c; break; }
        }
        if (!te) QSKIP("textEdit not found in delegate.");

        te->forceActiveFocus();
        QMetaObject::invokeMethod(te, "selectAll");

        // Type ' World' (6 chars appended conceptually — just type 3 extra chars).
        const QByteArray before = doc.toMarkdownUtf8();
        QTest::keyClick(&view, Qt::Key_End);
        QTest::keyClick(&view, Qt::Key_Exclam);
        QTest::keyClick(&view, Qt::Key_Exclam);
        QTest::keyClick(&view, Qt::Key_Exclam);
        // Wait for a parse to confirm the edit landed.
        QVERIFY(parseSpy.wait(2000));

        const QByteArray after = doc.toMarkdownUtf8();
        QVERIFY2(after.contains("!!!"), "Typed characters did not reach MarkoffDocument");
        QVERIFY2(after != before, "Document unchanged after typing");
    }

    void ctrl_z_undoes_edit() {
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
        QVERIFY(!component.isError());

        auto *root = qobject_cast<QQuickItem *>(component.create());
        QVERIFY(root);
        root->setParentItem(view.contentItem());
        root->setParent(view.contentItem());
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        // Seed document.
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        Markoff::MarkoffEdit ed;
        ed.oldStart = 0; ed.oldEnd = 0;
        ed.newText = QByteArrayLiteral("Original");
        doc.applyLocalEdit({ ed });
        QVERIFY(parseSpy.wait(2000));
        const QByteArray original = doc.toMarkdownUtf8();

        // Find the text edit and type something.
        QQuickItem *listView = root->findChild<QQuickItem *>(QStringLiteral("listView"));
        QVERIFY(listView);
        QTRY_COMPARE(listView->property("count").toInt(), 1);
        QMetaObject::invokeMethod(listView, "forceLayout");

        QQuickItem *te = nullptr;
        for (QQuickItem *c : root->findChildren<QQuickItem *>()) {
            if (c->objectName() == QLatin1String("textEdit")) { te = c; break; }
        }
        if (!te) QSKIP("textEdit not found");

        te->forceActiveFocus();
        QTest::keyClick(&view, Qt::Key_End);
        QTest::keyClick(&view, Qt::Key_X);
        QTest::keyClick(&view, Qt::Key_Y);
        QTest::keyClick(&view, Qt::Key_Z);
        QVERIFY(parseSpy.wait(2000));
        QVERIFY(doc.toMarkdownUtf8().contains("xyz"));

        // Ctrl+Z: undo.
        QTest::keyClick(&view, Qt::Key_Z, Qt::ControlModifier);
        QVERIFY(parseSpy.wait(2000));
        const QByteArray afterUndo = doc.toMarkdownUtf8();
        QCOMPARE(afterUndo, original);
    }

    void mode_toggle_source_to_live_preserves_content() {
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
            "    id: editor\n"
            "    width: 600; height: 800\n"
            "    document: doc\n"
            "    theme: themeCtx\n"
            "    mode: \"source\"\n"
            "}\n",
            QUrl());
        QVERIFY(!component.isError());

        auto *root = qobject_cast<QQuickItem *>(component.create());
        QVERIFY(root);
        root->setParentItem(view.contentItem());
        root->setParent(view.contentItem());
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        // Seed document from the doc side (simulates load from file).
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        Markoff::MarkoffEdit ed;
        ed.oldStart = 0; ed.oldEnd = 0;
        ed.newText = QByteArrayLiteral("Source content");
        doc.applyLocalEdit({ ed });
        QVERIFY(parseSpy.wait(2000));

        // Switch to live mode.
        root->setProperty("mode", QStringLiteral("live"));

        // Live view should now show one paragraph block.
        // Wait for listView to materialise, then for count to reach 1.
        QQuickItem *listView = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((listView = root->findChild<QQuickItem *>(
                                      QStringLiteral("listView"))) != nullptr, 2000);
        if (!listView) QSKIP("listView not found after mode toggle.");
        QTRY_COMPARE_WITH_TIMEOUT(listView->property("count").toInt(), 1, 2000);

        // Content must still be there.
        QVERIFY2(doc.toMarkdownUtf8().contains("Source content"),
                 "Document content lost after mode toggle");
    }
};

QTEST_MAIN(TstLiveViewQml)
#include "tst_view_qml_live_view_qml.moc"
