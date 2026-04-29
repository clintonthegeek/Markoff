// SPDX-License-Identifier: GPL-3.0-or-later
#include <QColor>
#include <QSignalSpy>
#include <QTest>

#include <crdt/Anchor.h>

#include <markoff-foundation/MarkoffEdit.h>
#include <markoff-foundation/Selection.h>
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

    void cursor_anchor_default_is_zero() {
        EditorBackend backend;
        QCOMPARE(backend.cursorAnchor(), CollabText::Crdt::Anchor{});
    }

    void set_cursor_anchor_lifts_to_session_primary_selection() {
        EditorBackend backend;
        Markoff::MarkoffDocument doc(1);
        backend.setDocument(&doc);

        // Seed text so anchorAt makes sense.
        Markoff::MarkoffEdit ed;
        ed.oldStart = 0; ed.oldEnd = 0; ed.newText = QByteArray("hello world");
        doc.applyLocalEdit({ ed });

        // Build an anchor at byte offset 5.
        const CollabText::Crdt::Anchor a = doc.anchorAt(5, CollabText::Crdt::Bias::Left);
        backend.setCursorAnchor(a);

        // Session's primary selection should now be a degenerate selection at 'a'.
        Markoff::Session *sess = backend.session();
        QVERIFY(sess != nullptr);
        const Markoff::Selection sel = sess->primarySelection();
        QCOMPARE(sel.anchor, a);
        QCOMPARE(sel.active, a);
        QCOMPARE(sel.kind, Markoff::Selection::Kind::Primary);
    }

    void session_primary_selection_change_updates_cursor_anchor() {
        EditorBackend backend;
        Markoff::MarkoffDocument doc(1);
        backend.setDocument(&doc);

        Markoff::MarkoffEdit ed;
        ed.oldStart = 0; ed.oldEnd = 0; ed.newText = QByteArray("hello world");
        doc.applyLocalEdit({ ed });

        const CollabText::Crdt::Anchor a = doc.anchorAt(3, CollabText::Crdt::Bias::Left);
        Markoff::Selection sel;
        sel.anchor = a; sel.active = a; sel.kind = Markoff::Selection::Kind::Primary;

        QSignalSpy spy(&backend, &EditorBackend::cursorAnchorChanged);
        backend.session()->setPrimarySelection(sel);

        QVERIFY(spy.count() >= 1);
        QCOMPARE(backend.cursorAnchor(), a);
    }

    void selection_anchor_and_active_default_to_zero() {
        EditorBackend backend;
        QCOMPARE(backend.selectionAnchor(), CollabText::Crdt::Anchor{});
        QCOMPARE(backend.selectionActive(), CollabText::Crdt::Anchor{});
    }

    void set_selection_anchor_and_active_lifts_range_to_session() {
        EditorBackend backend;
        Markoff::MarkoffDocument doc(1);
        backend.setDocument(&doc);
        Markoff::MarkoffEdit ed;
        ed.oldStart = 0; ed.oldEnd = 0; ed.newText = QByteArray("hello world");
        doc.applyLocalEdit({ ed });

        const auto a3 = doc.anchorAt(3, CollabText::Crdt::Bias::Left);
        const auto a8 = doc.anchorAt(8, CollabText::Crdt::Bias::Right);
        backend.setSelectionAnchor(a3);
        backend.setSelectionActive(a8);

        const Markoff::Selection sel = backend.session()->primarySelection();
        QCOMPARE(sel.anchor, a3);
        QCOMPARE(sel.active, a8);
    }

    void selection_can_be_reversed_active_before_anchor() {
        EditorBackend backend;
        Markoff::MarkoffDocument doc(1);
        backend.setDocument(&doc);
        Markoff::MarkoffEdit ed;
        ed.oldStart = 0; ed.oldEnd = 0; ed.newText = QByteArray("hello world");
        doc.applyLocalEdit({ ed });

        const auto a8 = doc.anchorAt(8, CollabText::Crdt::Bias::Left);
        const auto a3 = doc.anchorAt(3, CollabText::Crdt::Bias::Right);
        backend.setSelectionAnchor(a8);
        backend.setSelectionActive(a3);

        const Markoff::Selection sel = backend.session()->primarySelection();
        QCOMPARE(sel.anchor, a8);
        QCOMPARE(sel.active, a3);
        QVERIFY(sel.isReversed());
    }

    void session_range_selection_updates_both_anchor_and_active() {
        EditorBackend backend;
        Markoff::MarkoffDocument doc(1);
        backend.setDocument(&doc);
        Markoff::MarkoffEdit ed;
        ed.oldStart = 0; ed.oldEnd = 0; ed.newText = QByteArray("hello world");
        doc.applyLocalEdit({ ed });

        const auto a2 = doc.anchorAt(2, CollabText::Crdt::Bias::Left);
        const auto a7 = doc.anchorAt(7, CollabText::Crdt::Bias::Right);
        Markoff::Selection sel;
        sel.anchor = a2; sel.active = a7; sel.kind = Markoff::Selection::Kind::Primary;

        QSignalSpy spyAnchor(&backend, &EditorBackend::selectionAnchorChanged);
        QSignalSpy spyActive(&backend, &EditorBackend::selectionActiveChanged);
        QSignalSpy spyCursor(&backend, &EditorBackend::cursorAnchorChanged);

        backend.session()->setPrimarySelection(sel);

        QVERIFY(spyAnchor.count() >= 1);
        QVERIFY(spyActive.count() >= 1);
        QVERIFY(spyCursor.count() >= 1);
        QCOMPARE(backend.selectionAnchor(), a2);
        QCOMPARE(backend.selectionActive(), a7);
        QCOMPARE(backend.cursorAnchor(), a7);  // cursor follows active end
    }

    void parse_updated_at_relays_from_foundation_with_version() {
        EditorBackend backend;
        Markoff::MarkoffDocument doc(1);
        backend.setDocument(&doc);

        QSignalSpy spy(&backend, &EditorBackend::parseUpdatedAt);

        // Drive an edit to trigger a parse.
        Markoff::MarkoffEdit ed;
        ed.oldStart = 0; ed.oldEnd = 0; ed.newText = QByteArray("# Hello");
        doc.applyLocalEdit({ ed });

        // ParsePool is async; wait for the signal.
        QVERIFY(spy.wait(2000));
        QCOMPARE(spy.count(), 1);
    }

    void undo_reverts_local_edit() {
        EditorBackend backend;
        Markoff::MarkoffDocument doc(1);
        doc.setCoalescingIdleMs(0);
        backend.setDocument(&doc);

        Markoff::MarkoffEdit ed;
        ed.oldStart = 0; ed.oldEnd = 0; ed.newText = QByteArray("hello");
        doc.applyLocalEdit({ ed });
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("hello"));

        backend.undo();
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray());
    }

    void redo_replays_undone_edit() {
        EditorBackend backend;
        Markoff::MarkoffDocument doc(1);
        doc.setCoalescingIdleMs(0);
        backend.setDocument(&doc);

        Markoff::MarkoffEdit ed;
        ed.oldStart = 0; ed.oldEnd = 0; ed.newText = QByteArray("hello");
        doc.applyLocalEdit({ ed });
        backend.undo();
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray());

        backend.redo();
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("hello"));
    }

    void copy_selection_as_markdown_returns_substring() {
        EditorBackend backend;
        Markoff::MarkoffDocument doc(1);
        doc.setCoalescingIdleMs(0);
        backend.setDocument(&doc);

        Markoff::MarkoffEdit ed;
        ed.oldStart = 0; ed.oldEnd = 0; ed.newText = QByteArray("hello world");
        doc.applyLocalEdit({ ed });

        const auto a6 = doc.anchorAt(6, CollabText::Crdt::Bias::Left);
        const auto a11 = doc.anchorAt(11, CollabText::Crdt::Bias::Right);
        backend.setSelectionAnchor(a6);
        backend.setSelectionActive(a11);

        QCOMPARE(backend.copySelectionAsMarkdown(), QStringLiteral("world"));
    }

    void copy_selection_as_markdown_handles_reversed_selection() {
        EditorBackend backend;
        Markoff::MarkoffDocument doc(1);
        doc.setCoalescingIdleMs(0);
        backend.setDocument(&doc);

        Markoff::MarkoffEdit ed;
        ed.oldStart = 0; ed.oldEnd = 0; ed.newText = QByteArray("hello world");
        doc.applyLocalEdit({ ed });

        const auto a11 = doc.anchorAt(11, CollabText::Crdt::Bias::Left);
        const auto a6  = doc.anchorAt(6, CollabText::Crdt::Bias::Right);
        backend.setSelectionAnchor(a11);
        backend.setSelectionActive(a6);

        // Anchor>active should still return the substring in document order.
        QCOMPARE(backend.copySelectionAsMarkdown(), QStringLiteral("world"));
    }

    void copy_selection_returns_empty_when_degenerate() {
        EditorBackend backend;
        Markoff::MarkoffDocument doc(1);
        doc.setCoalescingIdleMs(0);
        backend.setDocument(&doc);

        Markoff::MarkoffEdit ed;
        ed.oldStart = 0; ed.oldEnd = 0; ed.newText = QByteArray("hello");
        doc.applyLocalEdit({ ed });

        const auto a3 = doc.anchorAt(3, CollabText::Crdt::Bias::Left);
        backend.setCursorAnchor(a3);

        QCOMPARE(backend.copySelectionAsMarkdown(), QString());
    }
};

QTEST_MAIN(TstViewQmlEditorBackend)
#include "tst_view_qml_editor_backend.moc"
