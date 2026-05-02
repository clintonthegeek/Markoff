// SPDX-License-Identifier: GPL-3.0-or-later
//
// NOTE ON TEST SETUP: QTextDocument::contentsChange(int,int,int) — the
// signal wired by LiveEditBinding — only fires when the document is
// managed by a QTextControl (i.e. when it belongs to a QTextEdit or a
// QQuickTextEdit). A bare QTextDocument only fires the no-arg
// contentsChanged(). Therefore all tests use QTextEdit (Widgets) to
// get a properly-wired QTextDocument; setRawTextDocument(editor.document())
// wires the binding to the editor's underlying document.

#include <QTest>
#include <QSignalSpy>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>

#include <markoff/live-render/LiveEditBinding.h>
#include <markoff/live-render/LiveListModelBinding.h>
#include <markoff/live-render/LiveBlockModel.h>
#include <markoff/live-render/BlockKind.h>
#include <markoff/live-render/Coordinates.h>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>
#include <markoff-foundation/Origin.h>

using namespace Markoff::LiveRender;

// Helper: reset content and wait for the parse to land in the model.
// setDocument must already have been called on the binding so the
// parseUpdated signal is connected before the parse fires.
static bool waitForModelRows(LiveListModelBinding &binding,
                              Markoff::MarkoffDocument &doc,
                              const QByteArray &content,
                              int expectedRows,
                              int timeoutMs = 2000)
{
    QSignalSpy spy(&doc, &Markoff::MarkoffDocument::parseUpdated);
    doc.resetContent(content, Markoff::Origin::FirstOpen);
    if (binding.model()->rowCount() == expectedRows)
        return true;
    if (!spy.wait(timeoutMs))
        return false;
    return binding.model()->rowCount() == expectedRows;
}

class TstLiveRenderParagraphEdit : public QObject {
    Q_OBJECT

private Q_SLOTS:

    void typing_one_char_emits_one_apply_local_edit() {
        Markoff::MarkoffDocument document(/*replicaId=*/1);

        LiveListModelBinding binding;
        binding.setDocument(&document);

        QVERIFY(waitForModelRows(binding, document, "hello world", 1));
        QCOMPARE(binding.model()->rowCount(), 1);

        // QTextEdit-backed document fires contentsChange(int,int,int) correctly.
        QTextEdit editor;
        editor.setPlainText("hello world");

        LiveEditBinding eb;
        eb.setBinding(&binding);
        eb.setModelIndex(0);
        eb.setRawTextDocument(editor.document());

        const quint64 seqBefore = document.editSequence();
        QSignalSpy contentsSpy(&document, &Markoff::MarkoffDocument::contentsChanged);

        // Simulate typing 'A' at position 5: "hello world" -> "helloA world".
        QTextCursor cur(editor.document());
        cur.setPosition(5);
        cur.insertText("A");

        QCOMPARE(contentsSpy.count(), 1);
        QVERIFY(document.editSequence() > seqBefore);
        QCOMPARE(document.toMarkdown(), QString("helloA world"));
        // Row's edit sequence should be stamped to the post-edit value.
        QCOMPARE(binding.model()->rowEditSequence(0), document.editSequence());
    }

    void typing_at_block_offset_translates_to_whole_doc_offset() {
        Markoff::MarkoffDocument document(/*replicaId=*/1);

        LiveListModelBinding binding;
        binding.setDocument(&document);

        QVERIFY(waitForModelRows(binding, document, "first\n\nsecond", 2));
        QCOMPARE(binding.model()->rowCount(), 2);

        // QTextEdit-backed document so contentsChange fires.
        QTextEdit editor;
        editor.setPlainText("second");  // mirrors row 1's text

        LiveEditBinding eb;
        eb.setBinding(&binding);
        eb.setModelIndex(1);
        eb.setRawTextDocument(editor.document());

        // Insert 'X' at qtPos 0 of the SECOND block. Whole-doc byte offset
        // should be the start of the second block (== 7: "first\n\n").
        QTextCursor cur(editor.document());
        cur.setPosition(0);
        cur.insertText("X");

        // The applyLocalEdit should land at byte 7. Verify via post-edit text.
        QCOMPARE(document.toMarkdown(), QString("first\n\nXsecond"));
    }

    void deletion_emits_correct_old_range() {
        Markoff::MarkoffDocument document(/*replicaId=*/1);

        LiveListModelBinding binding;
        binding.setDocument(&document);

        QVERIFY(waitForModelRows(binding, document, "abcdef", 1));

        QTextEdit editor;
        editor.setPlainText("abcdef");
        LiveEditBinding eb;
        eb.setBinding(&binding);
        eb.setModelIndex(0);
        eb.setRawTextDocument(editor.document());

        // Delete "cd": cursor selects 2..4, removeSelectedText.
        QTextCursor cur(editor.document());
        cur.setPosition(2);
        cur.setPosition(4, QTextCursor::KeepAnchor);
        cur.removeSelectedText();

        QCOMPARE(document.toMarkdown(), QString("abef"));
    }

    void utf8_multibyte_byte_offsets_correct() {
        // "héllo" is 6 UTF-8 bytes; é is 2 bytes (0xC3 0xA9). The QChar
        // count is 5 — so qtPos 2 == byte offset 3 ('l').
        Markoff::MarkoffDocument document(/*replicaId=*/1);

        LiveListModelBinding binding;
        binding.setDocument(&document);

        QVERIFY(waitForModelRows(binding, document, "héllo", 1));

        QTextEdit editor;
        editor.setPlainText("héllo");
        LiveEditBinding eb;
        eb.setBinding(&binding);
        eb.setModelIndex(0);
        eb.setRawTextDocument(editor.document());

        // Insert at qtPos 2 (after é). Should land at byte offset 3.
        QTextCursor cur(editor.document());
        cur.setPosition(2);
        cur.insertText("X");

        QCOMPARE(document.toMarkdown(), QString("héXllo"));
    }

    void typing_two_chars_before_parse_arrives_does_not_scramble() {
        // Reproduces the stale-record-text race: user types 'A' then 'B'
        // before a parse cycle delivers the post-'A' state to the model.
        // With m_previousText caching, edits compute against the
        // CRDT-coherent before-state and stay correct.
        Markoff::MarkoffDocument document(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&document);
        document.resetContent("hello", Markoff::Origin::FirstOpen);
        QSignalSpy parseSpy(&document, &Markoff::MarkoffDocument::parseUpdated);
        QVERIFY(parseSpy.wait(2000));

        QTextEdit editor;
        editor.setPlainText("hello");
        LiveEditBinding eb;
        eb.setBinding(&binding);
        eb.setModelIndex(0);
        eb.setRawTextDocument(editor.document());

        // Type 'A' at end. Don't wait for parse.
        QTextCursor cur(editor.document());
        cur.setPosition(5);
        cur.insertText("A");

        // Type 'B' at end. Still no parse arrival; record.text would still
        // be "hello", but m_previousText is now "helloA".
        cur.setPosition(6);
        cur.insertText("B");

        // CRDT must reflect "helloAB", not "helloBA".
        QCOMPARE(document.toMarkdown(), QString("helloAB"));
    }

    void model_update_does_not_echo_back_to_apply_local_edit() {
        // Setup: a doc with one paragraph; binding wired; user has typed
        // and the row sequence is stamped at N.
        Markoff::MarkoffDocument document(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&document);
        document.resetContent("aaa", Markoff::Origin::FirstOpen);
        QSignalSpy parseSpy(&document, &Markoff::MarkoffDocument::parseUpdated);
        QVERIFY(parseSpy.wait(2000));

        QTextEdit editor;
        editor.setPlainText("aaa");
        LiveEditBinding eb;
        eb.setBinding(&binding);
        eb.setModelIndex(0);
        eb.setRawTextDocument(editor.document());

        // Drive a real parse round-trip by causing a content change in
        // the foundation. We need model.text TO change so dataChanged
        // actually fires for the TextRole. Use a different content:
        document.resetContent("bbb", Markoff::Origin::TestFixture);
        QVERIFY(parseSpy.wait(2000));

        // Now assert the principle: during dataChanged from applyOps,
        // applyingModelUpdate is true.
        bool flagSeenDuringUpdate = false;
        bool dataChangedFired = false;
        auto conn = QObject::connect(binding.model(), &QAbstractItemModel::dataChanged,
                                      [&](){
                                          dataChangedFired = true;
                                          flagSeenDuringUpdate = binding.applyingModelUpdate();
                                      });

        // Force another applyOps run by changing the content again.
        document.resetContent("ccc", Markoff::Origin::TestFixture);
        QVERIFY(parseSpy.wait(2000));

        QObject::disconnect(conn);
        QVERIFY2(dataChangedFired, "dataChanged should have fired during applyOps");
        QVERIFY2(flagSeenDuringUpdate,
                 "applyingModelUpdate should be true while dataChanged fires");
    }
};

QTEST_MAIN(TstLiveRenderParagraphEdit)
#include "tst_live_render_paragraph_edit.moc"
