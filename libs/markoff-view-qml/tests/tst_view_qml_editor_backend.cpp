// SPDX-License-Identifier: GPL-3.0-or-later
#include <QSignalSpy>
#include <QTest>

#include <markoff/view/qml/EditorBackend.h>

using namespace Markoff::View::Qml;

class TstViewQmlEditorBackend : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void default_constructed_has_null_document() {
        EditorBackend backend;
        QCOMPARE(backend.document(), nullptr);
    }

    void set_document_emits_change_signal_once() {
        EditorBackend backend;
        QSignalSpy spy(&backend, &EditorBackend::documentChanged);

        Markoff::MarkoffDocument doc(1);
        backend.setDocument(&doc);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(backend.document(), &doc);

        // Idempotent: setting the same doc again does NOT re-emit.
        backend.setDocument(&doc);
        QCOMPARE(spy.count(), 1);
    }

    void set_document_to_null_emits_change() {
        EditorBackend backend;
        Markoff::MarkoffDocument doc(1);
        backend.setDocument(&doc);

        QSignalSpy spy(&backend, &EditorBackend::documentChanged);
        backend.setDocument(nullptr);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(backend.document(), nullptr);
    }
};

QTEST_APPLESS_MAIN(TstViewQmlEditorBackend)
#include "tst_view_qml_editor_backend.moc"
