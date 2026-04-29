// SPDX-License-Identifier: GPL-3.0-or-later
#include <QColor>
#include <QSignalSpy>
#include <QTest>

#include <markoff-foundation/Session.h>
#include <markoff-foundation/Theme.h>
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

    void session_is_null_when_no_document() {
        EditorBackend backend;
        QCOMPARE(backend.session(), nullptr);
    }

    void session_created_when_document_set() {
        EditorBackend backend;
        QSignalSpy spy(&backend, &EditorBackend::sessionChanged);

        Markoff::MarkoffDocument doc(1);
        backend.setDocument(&doc);

        QCOMPARE(spy.count(), 1);
        QVERIFY(backend.session() != nullptr);
    }

    void session_replaced_when_document_swapped() {
        EditorBackend backend;
        Markoff::MarkoffDocument docA(1);
        Markoff::MarkoffDocument docB(2);
        backend.setDocument(&docA);
        Markoff::Session *sessionA = backend.session();
        QVERIFY(sessionA != nullptr);

        QSignalSpy spy(&backend, &EditorBackend::sessionChanged);
        backend.setDocument(&docB);

        // Two emissions: one for cleanup of old session, one for creation of new.
        QVERIFY(spy.count() >= 1);
        QVERIFY(backend.session() != nullptr);
        QVERIFY(backend.session() != sessionA);
    }

    void session_destroyed_when_document_set_to_null() {
        EditorBackend backend;
        Markoff::MarkoffDocument doc(1);
        backend.setDocument(&doc);
        QVERIFY(backend.session() != nullptr);

        QSignalSpy spy(&backend, &EditorBackend::sessionChanged);
        backend.setDocument(nullptr);
        QVERIFY(spy.count() >= 1);
        QCOMPARE(backend.session(), nullptr);
    }

    void default_theme_is_light() {
        EditorBackend backend;
        Markoff::Theme defaultLight = Markoff::Theme::defaultLight();
        QCOMPARE(backend.theme().color(Markoff::Theme::Slot::TextDefault).name(),
                 defaultLight.color(Markoff::Theme::Slot::TextDefault).name());
    }

    void set_theme_emits_change_signal() {
        EditorBackend backend;
        QSignalSpy spy(&backend, &EditorBackend::themeChanged);

        Markoff::Theme custom = Markoff::Theme::defaultLight();
        custom.setColor(Markoff::Theme::Slot::TextDefault, QColor("#ff0000"));
        backend.setTheme(custom);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(backend.theme().color(Markoff::Theme::Slot::TextDefault).name(),
                 QColor("#ff0000").name());
    }
};

QTEST_APPLESS_MAIN(TstViewQmlEditorBackend)
#include "tst_view_qml_editor_backend.moc"
