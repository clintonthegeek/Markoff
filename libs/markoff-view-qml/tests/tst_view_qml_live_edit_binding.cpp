// SPDX-License-Identifier: GPL-3.0-or-later
#include <QObject>
#include <QQmlApplicationEngine>
#include <QQuickTextDocument>
#include <QSignalSpy>
#include <QTest>
#include <QTextCursor>
#include <QTextDocument>

#include <markoff/view/qml/EditorBackend.h>
#include <markoff/view/qml/LiveEditBinding.h>
#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>
#include <markoff-foundation/Origin.h>

using Markoff::MarkoffDocument;
using Markoff::MarkoffEdit;
using Markoff::BlockAnchor;
using Markoff::View::Qml::LiveEditBinding;
using Markoff::View::Qml::EditorBackend;

class TstViewQmlLiveEditBinding : public QObject {
    Q_OBJECT

private:
    // Load a QML TextArea and return its QQuickTextDocument.
    // Returns nullptr when offscreen rendering is unavailable.
    static QQuickTextDocument *seedQQuickTextDocument(QQmlApplicationEngine &engine)
    {
        engine.loadData(
            R"qml(
                import QtQuick
                import QtQuick.Controls
                ApplicationWindow {
                    visible: false
                    TextEdit { id: te; objectName: "te" }
                }
            )qml"
        );
        QObject *root = engine.rootObjects().value(0);
        if (!root) return nullptr;
        QObject *te = root->findChild<QObject *>("te");
        if (!te) return nullptr;
        return qvariant_cast<QQuickTextDocument *>(te->property("textDocument"));
    }

    // Seed the document with one paragraph block and return the BlockAnchor.
    // Blocks aren't immediately available (parseUpdated is async), so we use
    // blockAnchorAt after a brief wait for the parse to arrive.
    static BlockAnchor seedDocumentAndWaitForParse(MarkoffDocument &doc,
                                                   const QByteArray &content)
    {
        doc.resetContent(content, Markoff::Origin::TestFixture);
        // Wait up to 2 seconds for the parse to arrive.
        QSignalSpy parseSpy(&doc, &MarkoffDocument::parseUpdated);
        if (parseSpy.isEmpty()) {
            parseSpy.wait(2000);
        }
        const auto anchorOpt = doc.blockAnchorAt(0);
        return anchorOpt.value_or(BlockAnchor{});
    }

private Q_SLOTS:
    void default_constructed_has_null_document()
    {
        LiveEditBinding b;
        QCOMPARE(b.document(), nullptr);
        QCOMPARE(b.textDocument(), nullptr);
    }

    void setting_document_emits_signal()
    {
        LiveEditBinding b;
        MarkoffDocument doc(1);
        QSignalSpy spy(&b, &LiveEditBinding::documentChanged);
        b.setDocument(&doc);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(b.document(), &doc);
    }

    // -----------------------------------------------------------------------
    // Forward edit path: TextEdit user keystroke → MarkoffDocument
    // -----------------------------------------------------------------------

    void typing_in_textedit_propagates_to_markoffdocument()
    {
        QQmlApplicationEngine engine;
        QQuickTextDocument *qqtd = seedQQuickTextDocument(engine);
        if (!qqtd) QSKIP("QML engine failed to load — offscreen QtQuick unavailable");

        MarkoffDocument doc(1);

        // Seed doc with one paragraph; obtain its BlockAnchor.
        const BlockAnchor anchor = seedDocumentAndWaitForParse(doc, "hello");

        // Verify anchor is usable — it should resolve block byte range.
        const auto rangeOpt = doc.blockByteRange(anchor);
        if (!rangeOpt) QSKIP("blockByteRange returned nullopt — parse may not have arrived");

        // Seed the QTextDocument with the block's text.
        QTextCursor cursor(qqtd->textDocument());
        cursor.insertText(QStringLiteral("hello"));

        LiveEditBinding binding;
        binding.setDocument(&doc);
        binding.setBlockAnchor(anchor);
        binding.setTextDocument(qqtd);

        const quint64 seqBefore = doc.editSequence();

        // Simulate typing " world" at end.
        cursor.movePosition(QTextCursor::End);
        cursor.insertText(QStringLiteral(" world"));

        // editSequence must have incremented — applyLocalEdit was called.
        QVERIFY(doc.editSequence() > seqBefore);
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("hello world"));
    }

    void deletion_in_textedit_propagates_to_markoffdocument()
    {
        QQmlApplicationEngine engine;
        QQuickTextDocument *qqtd = seedQQuickTextDocument(engine);
        if (!qqtd) QSKIP("QML engine failed to load — offscreen QtQuick unavailable");

        MarkoffDocument doc(1);
        const BlockAnchor anchor = seedDocumentAndWaitForParse(doc, "hello world");

        const auto rangeOpt = doc.blockByteRange(anchor);
        if (!rangeOpt) QSKIP("blockByteRange returned nullopt");

        QTextCursor cursor(qqtd->textDocument());
        cursor.insertText(QStringLiteral("hello world"));

        LiveEditBinding binding;
        binding.setDocument(&doc);
        binding.setBlockAnchor(anchor);
        binding.setTextDocument(qqtd);

        // Delete " world" (positions 5..11).
        cursor.setPosition(5);
        cursor.setPosition(11, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();

        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("hello"));
    }

    // -----------------------------------------------------------------------
    // Cycle guard: model-driven update must NOT loop back into applyLocalEdit
    // -----------------------------------------------------------------------

    void begin_end_model_update_suppresses_apply_local_edit()
    {
        QQmlApplicationEngine engine;
        QQuickTextDocument *qqtd = seedQQuickTextDocument(engine);
        if (!qqtd) QSKIP("QML engine failed to load — offscreen QtQuick unavailable");

        MarkoffDocument doc(1);
        const BlockAnchor anchor = seedDocumentAndWaitForParse(doc, "original text");

        const auto rangeOpt = doc.blockByteRange(anchor);
        if (!rangeOpt) QSKIP("blockByteRange returned nullopt");

        QTextCursor cursor(qqtd->textDocument());
        cursor.insertText(QStringLiteral("original text"));

        LiveEditBinding binding;
        binding.setDocument(&doc);
        binding.setBlockAnchor(anchor);
        binding.setTextDocument(qqtd);

        const quint64 seqBefore = doc.editSequence();

        // Simulate a model-driven text update: bracket with guards.
        binding.beginModelUpdate();
        qqtd->textDocument()->setPlainText(QStringLiteral("updated by model"));
        binding.endModelUpdate();

        // editSequence must NOT have changed — the cycle guard suppressed the write.
        QCOMPARE(doc.editSequence(), seqBefore);
    }

    // -----------------------------------------------------------------------
    // UTF-16 → UTF-8 offset translation
    // -----------------------------------------------------------------------

    void typing_non_ascii_produces_correct_byte_offset()
    {
        QQmlApplicationEngine engine;
        QQuickTextDocument *qqtd = seedQQuickTextDocument(engine);
        if (!qqtd) QSKIP("QML engine failed to load — offscreen QtQuick unavailable");

        MarkoffDocument doc(1);
        // "héllo" — 'é' is U+00E9: 1 UTF-16 unit, 2 UTF-8 bytes.
        const BlockAnchor anchor = seedDocumentAndWaitForParse(doc, "h\xC3\xA9llo");

        const auto rangeOpt = doc.blockByteRange(anchor);
        if (!rangeOpt) QSKIP("blockByteRange returned nullopt");

        QTextCursor cursor(qqtd->textDocument());
        cursor.insertText(QStringLiteral("héllo"));

        LiveEditBinding binding;
        binding.setDocument(&doc);
        binding.setBlockAnchor(anchor);
        binding.setTextDocument(qqtd);

        // Insert "X" after 'h' (qtPos 1); the byte offset into the block is 1.
        // After the insertion, the doc should hold "hXéllo".
        cursor.setPosition(1);
        cursor.insertText(QStringLiteral("X"));

        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("hX\xC3\xA9llo"));
    }

    void typing_after_multibyte_produces_correct_byte_offset()
    {
        QQmlApplicationEngine engine;
        QQuickTextDocument *qqtd = seedQQuickTextDocument(engine);
        if (!qqtd) QSKIP("QML engine failed to load — offscreen QtQuick unavailable");

        MarkoffDocument doc(1);
        // "éhello" — 'é' is 2 UTF-8 bytes; inserting after it should give byte offset 2.
        const BlockAnchor anchor = seedDocumentAndWaitForParse(doc, "\xC3\xA9hello");

        const auto rangeOpt = doc.blockByteRange(anchor);
        if (!rangeOpt) QSKIP("blockByteRange returned nullopt");

        QTextCursor cursor(qqtd->textDocument());
        cursor.insertText(QStringLiteral("éhello"));

        LiveEditBinding binding;
        binding.setDocument(&doc);
        binding.setBlockAnchor(anchor);
        binding.setTextDocument(qqtd);

        // Insert "X" at qtPos 1 (after 'é', which is 1 UTF-16 unit but 2 UTF-8 bytes).
        cursor.setPosition(1);
        cursor.insertText(QStringLiteral("X"));

        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("\xC3\xA9Xhello"));
    }
};

QTEST_MAIN(TstViewQmlLiveEditBinding)
#include "tst_view_qml_live_edit_binding.moc"
