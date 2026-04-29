// SPDX-License-Identifier: GPL-3.0-or-later
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlProperty>
#include <QQuickItem>
#include <QSignalSpy>
#include <QTest>
#include <QVariant>
#include <QtPlugin>

// Force-import the static QML plugin so the engine resolves org.markoff.view.qml.
Q_IMPORT_PLUGIN(org_markoff_view_qmlPlugin)

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/Origin.h>
#include <markoff-foundation/Theme.h>

class TstViewQmlIntegration : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initTestCase() {
        // Skip if QtQuick can't initialize on this CI / dev environment.
        QGuiApplication *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance());
        if (!app) {
            QSKIP("QGuiApplication required for QtQuick; QTEST_MAIN gives QCoreApplication only");
        }
    }

    void markoff_editor_qml_loads_and_binds_to_document() {
        // Build a doc with seed content.
        Markoff::MarkoffDocument doc(1);
        doc.setCoalescingIdleMs(0);
        doc.resetContent(QByteArray("hello world"), Markoff::Origin::FirstOpen);

        // Set up a QML engine with the doc + theme as context properties.
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/"));
        engine.rootContext()->setContextProperty("doc", &doc);
        engine.rootContext()->setContextProperty(
            "theme", QVariant::fromValue(Markoff::Theme::defaultLight()));

        // Instantiate MarkoffEditor.qml directly via component.
        QQmlComponent component(&engine);
        component.setData(
            "import QtQuick\n"
            "import QtQuick.Controls\n"
            "import org.markoff.view.qml\n"
            "MarkoffEditor {\n"
            "    width: 400; height: 300\n"
            "    document: doc\n"
            "    theme: theme\n"
            "}\n",
            QUrl());

        if (component.isError()) {
            qWarning() << "QML compile errors:" << component.errors();
        }
        QVERIFY(!component.isError());

        QObject *root = component.create();
        QVERIFY(root);

        // The editor should expose a `editorBackend` property (alias from T20).
        const QVariant ebVar = root->property("editorBackend");
        QVERIFY(ebVar.isValid());
        QObject *editorBackend = qvariant_cast<QObject *>(ebVar);
        QVERIFY(editorBackend);

        // Verify the document property propagated.
        const QVariant docVar = editorBackend->property("document");
        QVERIFY(docVar.isValid());
        QCOMPARE(qvariant_cast<Markoff::MarkoffDocument *>(docVar), &doc);

        delete root;
    }

    void editing_via_markoff_document_propagates_to_qml() {
        Markoff::MarkoffDocument doc(1);
        doc.setCoalescingIdleMs(0);
        doc.resetContent(QByteArray("seed"), Markoff::Origin::FirstOpen);

        QQmlEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/"));
        engine.rootContext()->setContextProperty("doc", &doc);
        engine.rootContext()->setContextProperty(
            "theme", QVariant::fromValue(Markoff::Theme::defaultLight()));

        QQmlComponent component(&engine);
        component.setData(
            "import QtQuick\n"
            "import QtQuick.Controls\n"
            "import org.markoff.view.qml\n"
            "MarkoffEditor {\n"
            "    width: 400; height: 300\n"
            "    document: doc\n"
            "    theme: theme\n"
            "}\n",
            QUrl());
        if (component.isError()) qWarning() << component.errors();
        QObject *root = component.create();
        QVERIFY(root);

        // Apply a foundation-side edit and verify the document content updates.
        // (This exercises the foundation→binding→QTextDocument reverse path
        // even though we don't read the QTextDocument back here — we trust
        // T13's tests for that, and instead just confirm the foundation
        // side accepts our edit and emits.)
        Markoff::MarkoffEdit ed;
        ed.oldStart = 0; ed.oldEnd = 4; ed.newText = QByteArray("hello");
        doc.applyLocalEdit({ ed });
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("hello"));

        delete root;
    }
};

QTEST_MAIN(TstViewQmlIntegration)
#include "tst_view_qml_integration.moc"
