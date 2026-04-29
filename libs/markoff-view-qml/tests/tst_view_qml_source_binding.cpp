// SPDX-License-Identifier: GPL-3.0-or-later
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickTextDocument>
#include <QSignalSpy>
#include <QTest>

#include <markoff/view/qml/EditorBackend.h>
#include <markoff/view/qml/SourceTextDocumentBinding.h>

using namespace Markoff::View::Qml;

class TstViewQmlSourceBinding : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void default_constructed_has_null_pointers() {
        SourceTextDocumentBinding b;
        QCOMPARE(b.editorBackend(), nullptr);
        QCOMPARE(b.qtQuickDocument(), nullptr);
    }

    void setting_editor_backend_emits_signal() {
        SourceTextDocumentBinding b;
        EditorBackend backend;
        QSignalSpy spy(&b, &SourceTextDocumentBinding::editorBackendChanged);
        b.setEditorBackend(&backend);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(b.editorBackend(), &backend);
    }

    // QQuickTextDocument requires a real QQuickItem parent. The cleanest way
    // to test capture is to construct a TextArea via QML and grab its
    // textDocument. We do that with a one-line QML doc string.
    void setting_qt_quick_document_disables_undo_redo_when_paired_with_backend() {
        QQmlApplicationEngine engine;
        engine.loadData(
            R"qml(
                import QtQuick
                import QtQuick.Controls
                ApplicationWindow {
                    visible: false
                    TextArea { id: ta; objectName: "ta" }
                }
            )qml"
        );
        if (engine.rootObjects().isEmpty()) {
            QSKIP("QML engine failed to load — offscreen QtQuick unavailable");
        }
        QObject *root = engine.rootObjects().value(0);
        QVERIFY(root);
        QObject *ta = root->findChild<QObject *>("ta");
        QVERIFY(ta);
        QQuickTextDocument *qqtd =
            qvariant_cast<QQuickTextDocument *>(ta->property("textDocument"));
        QVERIFY(qqtd);

        SourceTextDocumentBinding b;
        EditorBackend backend;
        b.setEditorBackend(&backend);
        b.setQtQuickDocument(qqtd);

        QVERIFY(qqtd->textDocument()->isUndoRedoEnabled() == false);
    }
};

QTEST_MAIN(TstViewQmlSourceBinding)
#include "tst_view_qml_source_binding.moc"
