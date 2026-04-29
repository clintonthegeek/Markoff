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
            "theme", QVariant::fromValue(Markoff::Theme::defaultLight()));
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
            "    theme: theme\n"
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
};

QTEST_MAIN(TstLiveViewQml)
#include "tst_view_qml_live_view_qml.moc"
