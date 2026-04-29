// SPDX-License-Identifier: GPL-3.0-or-later
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickTextDocument>
#include <QSignalSpy>
#include <QTest>
#include <QTextCursor>

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

    // -----------------------------------------------------------------------
    // UTF-8 / UTF-16 conversion helpers (T11)
    // -----------------------------------------------------------------------

    void qt_pos_to_byte_offset_data() {
        QTest::addColumn<QString>("text");
        QTest::addColumn<int>("qtOffset");
        QTest::addColumn<quint32>("expected");

        QTest::newRow("ascii at start") << QStringLiteral("hello") << 0 << quint32(0);
        QTest::newRow("ascii at end")   << QStringLiteral("hello") << 5 << quint32(5);
        QTest::newRow("ascii mid")      << QStringLiteral("hello") << 3 << quint32(3);

        // 'é' is U+00E9 — 1 UTF-16 code unit, 2 UTF-8 bytes.
        QTest::newRow("e-accent before") << QStringLiteral("éhello") << 1 << quint32(2);
        QTest::newRow("e-accent at end") << QStringLiteral("éhello") << 6 << quint32(7);

        // 'カ' is U+30AB (Katakana KA) — 1 UTF-16 code unit, 3 UTF-8 bytes.
        QTest::newRow("katakana before") << QStringLiteral("カhello") << 1 << quint32(3);
        QTest::newRow("katakana at end") << QStringLiteral("カhello") << 6 << quint32(8);

        // '🎉' is U+1F389 (party popper) — 2 UTF-16 code units (surrogate pair),
        // 4 UTF-8 bytes.
        QTest::newRow("emoji before") << QStringLiteral("🎉hello") << 2 << quint32(4);
        QTest::newRow("emoji at end") << QStringLiteral("🎉hello") << 7 << quint32(9);

        // Out-of-bounds clamps.
        QTest::newRow("negative qtOffset clamps to 0")
            << QStringLiteral("hello") << -5 << quint32(0);
        QTest::newRow("over-end qtOffset clamps to text byte size")
            << QStringLiteral("hello") << 999 << quint32(5);
    }

    void qt_pos_to_byte_offset() {
        QFETCH(QString, text);
        QFETCH(int, qtOffset);
        QFETCH(quint32, expected);
        QCOMPARE(SourceTextDocumentBinding::qtPosToByteOffset(text, qtOffset), expected);
    }

    void byte_offset_to_qt_pos_data() {
        QTest::addColumn<QByteArray>("utf8");
        QTest::addColumn<quint32>("byteOffset");
        QTest::addColumn<int>("expected");

        QTest::newRow("ascii at start") << QByteArray("hello") << quint32(0) << 0;
        QTest::newRow("ascii at end")   << QByteArray("hello") << quint32(5) << 5;
        QTest::newRow("ascii mid")      << QByteArray("hello") << quint32(3) << 3;

        // "éhello" = 0xC3 0xA9 + "hello" = 7 bytes. byte 2 → after é → qtPos 1.
        QTest::newRow("after e-accent")
            << QByteArray("\xC3\xA9hello") << quint32(2) << 1;
        QTest::newRow("after e-accent then h")
            << QByteArray("\xC3\xA9hello") << quint32(3) << 2;

        // "カhello" = 0xE3 0x82 0xAB + "hello" = 8 bytes. byte 3 → after カ → qtPos 1.
        QTest::newRow("after katakana")
            << QByteArray("\xE3\x82\xABhello") << quint32(3) << 1;

        // "🎉hello" = 0xF0 0x9F 0x8E 0x89 + "hello" = 9 bytes. byte 4 → after emoji → qtPos 2.
        QTest::newRow("after emoji")
            << QByteArray("\xF0\x9F\x8E\x89hello") << quint32(4) << 2;

        QTest::newRow("byte 0") << QByteArray("hello") << quint32(0) << 0;
        QTest::newRow("over-end clamps to total qt size")
            << QByteArray("hello") << quint32(999) << 5;
    }

    void byte_offset_to_qt_pos() {
        QFETCH(QByteArray, utf8);
        QFETCH(quint32, byteOffset);
        QFETCH(int, expected);
        QCOMPARE(SourceTextDocumentBinding::byteOffsetToQtPos(utf8, byteOffset), expected);
    }

    void roundtrip_qt_to_byte_to_qt_for_mixed_content() {
        const QString text = QStringLiteral("hello é world カ test 🎉 end");
        const QByteArray utf8 = text.toUtf8();
        for (int qtPos = 0; qtPos <= text.size(); ++qtPos) {
            const quint32 byteOff = SourceTextDocumentBinding::qtPosToByteOffset(text, qtPos);
            const int qtPosBack = SourceTextDocumentBinding::byteOffsetToQtPos(utf8, byteOff);
            // Note: positions inside a surrogate pair won't roundtrip exactly.
            // We test only positions on UTF-16 code-unit boundaries that are also
            // UTF-8 character boundaries — every position OUTSIDE the surrogate pair.
            // The emoji '🎉' occupies qtPos 21..22; skip qtPos 22 specifically.
            if (qtPos < text.size() && text.at(qtPos).isLowSurrogate()) continue;
            QCOMPARE(qtPosBack, qtPos);
        }
    }

    // -----------------------------------------------------------------------
    // Forward edit path: QTextDocument → MarkoffDocument (T12)
    // -----------------------------------------------------------------------

    // Helper: load a QML TextArea, return its QQuickTextDocument.
    // Returns nullptr if the QML engine fails (offscreen unavailable).
    static QQuickTextDocument *seedQQuickTextDocument(QQmlApplicationEngine &engine)
    {
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
        QObject *root = engine.rootObjects().value(0);
        if (!root) return nullptr;
        QObject *ta = root->findChild<QObject *>("ta");
        if (!ta) return nullptr;
        return qvariant_cast<QQuickTextDocument *>(ta->property("textDocument"));
    }

    void typing_into_qtextdocument_propagates_to_markoffdocument() {
        QQmlApplicationEngine engine;
        QQuickTextDocument *qqtd = seedQQuickTextDocument(engine);
        if (!qqtd) QSKIP("QML engine failed to load — offscreen QtQuick unavailable");

        Markoff::MarkoffDocument doc(1);
        doc.setCoalescingIdleMs(0);
        EditorBackend backend;
        backend.setDocument(&doc);
        SourceTextDocumentBinding binding;
        binding.setEditorBackend(&backend);
        binding.setQtQuickDocument(qqtd);

        QTextCursor cursor(qqtd->textDocument());
        cursor.insertText(QStringLiteral("hello"));

        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("hello"));
    }

    void deleting_text_in_qtextdocument_propagates_to_markoffdocument() {
        QQmlApplicationEngine engine;
        QQuickTextDocument *qqtd = seedQQuickTextDocument(engine);
        if (!qqtd) QSKIP("QML engine failed to load — offscreen QtQuick unavailable");

        Markoff::MarkoffDocument doc(1);
        doc.setCoalescingIdleMs(0);
        EditorBackend backend;
        backend.setDocument(&doc);
        SourceTextDocumentBinding binding;
        binding.setEditorBackend(&backend);
        binding.setQtQuickDocument(qqtd);

        QTextCursor cursor(qqtd->textDocument());
        cursor.insertText(QStringLiteral("hello world"));
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("hello world"));

        // Select " world" and delete.
        cursor.setPosition(5);
        cursor.setPosition(11, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("hello"));
    }

    void replacing_text_in_qtextdocument_propagates_to_markoffdocument() {
        QQmlApplicationEngine engine;
        QQuickTextDocument *qqtd = seedQQuickTextDocument(engine);
        if (!qqtd) QSKIP("QML engine failed to load — offscreen QtQuick unavailable");

        Markoff::MarkoffDocument doc(1);
        doc.setCoalescingIdleMs(0);
        EditorBackend backend;
        backend.setDocument(&doc);
        SourceTextDocumentBinding binding;
        binding.setEditorBackend(&backend);
        binding.setQtQuickDocument(qqtd);

        QTextCursor cursor(qqtd->textDocument());
        cursor.insertText(QStringLiteral("hello world"));

        // Replace "world" with "there".
        cursor.setPosition(6);
        cursor.setPosition(11, QTextCursor::KeepAnchor);
        cursor.insertText(QStringLiteral("there"));
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("hello there"));
    }

    void typing_non_ascii_propagates_correctly() {
        QQmlApplicationEngine engine;
        QQuickTextDocument *qqtd = seedQQuickTextDocument(engine);
        if (!qqtd) QSKIP("QML engine failed to load — offscreen QtQuick unavailable");

        Markoff::MarkoffDocument doc(1);
        doc.setCoalescingIdleMs(0);
        EditorBackend backend;
        backend.setDocument(&doc);
        SourceTextDocumentBinding binding;
        binding.setEditorBackend(&backend);
        binding.setQtQuickDocument(qqtd);

        // Type "héllo" — 'é' is U+00E9, 1 UTF-16 unit, 2 UTF-8 bytes.
        QTextCursor cursor(qqtd->textDocument());
        cursor.insertText(QStringLiteral("héllo"));

        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("h\xC3\xA9llo"));
    }
    // -----------------------------------------------------------------------
    // Reverse edit path: MarkoffDocument → QTextDocument (T13)
    // -----------------------------------------------------------------------

    void undo_propagates_back_to_qtextdocument() {
        QQmlApplicationEngine engine;
        QQuickTextDocument *qqtd = seedQQuickTextDocument(engine);
        if (!qqtd) QSKIP("QML engine failed to load — offscreen QtQuick unavailable");

        Markoff::MarkoffDocument doc(1);
        doc.setCoalescingIdleMs(0);
        EditorBackend backend;
        backend.setDocument(&doc);
        SourceTextDocumentBinding binding;
        binding.setEditorBackend(&backend);
        binding.setQtQuickDocument(qqtd);

        // Type into TextArea, propagating to MarkoffDocument.
        QTextCursor cursor(qqtd->textDocument());
        cursor.insertText(QStringLiteral("hello"));
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("hello"));
        QCOMPARE(qqtd->textDocument()->toPlainText(), QStringLiteral("hello"));

        // Undo via foundation; T13's reverse path applies the change to QTextDocument.
        backend.undo();
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray());
        QCOMPARE(qqtd->textDocument()->toPlainText(), QString());
    }

    void direct_markoff_edit_propagates_to_qtextdocument() {
        QQmlApplicationEngine engine;
        QQuickTextDocument *qqtd = seedQQuickTextDocument(engine);
        if (!qqtd) QSKIP("QML engine failed to load — offscreen QtQuick unavailable");

        Markoff::MarkoffDocument doc(1);
        doc.setCoalescingIdleMs(0);
        EditorBackend backend;
        backend.setDocument(&doc);
        SourceTextDocumentBinding binding;
        binding.setEditorBackend(&backend);
        binding.setQtQuickDocument(qqtd);

        // Apply edit directly via the foundation, NOT via TextArea.
        // T13 reverse path should apply it to QTextDocument.
        Markoff::MarkoffEdit ed;
        ed.oldStart = 0; ed.oldEnd = 0; ed.newText = QByteArray("hello world");
        doc.applyLocalEdit({ ed });

        QCOMPARE(qqtd->textDocument()->toPlainText(), QStringLiteral("hello world"));
    }

    void local_edit_does_not_double_apply() {
        // The most paranoid test: a TextArea edit echoes back via contentsChanged.
        // The forward path cycle guard (m_applyingLocalEdit) must suppress the
        // reverse path; the reverse path's removed-text re-apply must NOT happen.
        QQmlApplicationEngine engine;
        QQuickTextDocument *qqtd = seedQQuickTextDocument(engine);
        if (!qqtd) QSKIP("QML engine failed to load — offscreen QtQuick unavailable");

        Markoff::MarkoffDocument doc(1);
        doc.setCoalescingIdleMs(0);
        EditorBackend backend;
        backend.setDocument(&doc);
        SourceTextDocumentBinding binding;
        binding.setEditorBackend(&backend);
        binding.setQtQuickDocument(qqtd);

        QTextCursor cursor(qqtd->textDocument());
        cursor.insertText(QStringLiteral("hello"));

        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("hello"));
        QCOMPARE(qqtd->textDocument()->toPlainText(), QStringLiteral("hello"));
        // Crucially, after the local edit + echo, BOTH sides hold "hello",
        // not "hellohello" (which would happen if the cycle guard was missing).
    }

    void undo_then_redo_round_trips_via_both_paths() {
        QQmlApplicationEngine engine;
        QQuickTextDocument *qqtd = seedQQuickTextDocument(engine);
        if (!qqtd) QSKIP("QML engine failed to load — offscreen QtQuick unavailable");

        Markoff::MarkoffDocument doc(1);
        doc.setCoalescingIdleMs(0);
        EditorBackend backend;
        backend.setDocument(&doc);
        SourceTextDocumentBinding binding;
        binding.setEditorBackend(&backend);
        binding.setQtQuickDocument(qqtd);

        QTextCursor cursor(qqtd->textDocument());
        cursor.insertText(QStringLiteral("hello"));

        backend.undo();
        QCOMPARE(qqtd->textDocument()->toPlainText(), QString());

        backend.redo();
        QCOMPARE(qqtd->textDocument()->toPlainText(), QStringLiteral("hello"));

        backend.undo();
        QCOMPARE(qqtd->textDocument()->toPlainText(), QString());
    }
};

QTEST_MAIN(TstViewQmlSourceBinding)
#include "tst_view_qml_source_binding.moc"
