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
        doc.setCoalescingIdleMs(0);

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
        doc.setCoalescingIdleMs(0);

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

        QCOMPARE(selModel->property("anchorBlock").toInt(), 1);
        QCOMPARE(selModel->property("activeBlock").toInt(), 3);
        QVERIFY(selModel->property("anchorOffset").toInt() > 0);
    }

    void delegates_consume_theme_colors() {
        Markoff::MarkoffDocument doc(1);
        doc.setCoalescingIdleMs(0);

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

    void selection_highlight_appears_on_hr_and_image() {
        Markoff::MarkoffDocument doc(1);
        doc.setCoalescingIdleMs(0);

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
};

QTEST_MAIN(TstLiveViewQml)
#include "tst_view_qml_live_view_qml.moc"
