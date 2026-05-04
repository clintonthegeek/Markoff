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
#include <QClipboard>
#include <QApplication>

#include <markoff/live-render/LiveEditBinding.h>
#include <markoff/live-render/LiveListModelBinding.h>
#include <markoff/live-render/LiveBlockModel.h>
#include <markoff/live-render/BlockKind.h>
#include <markoff/live-render/Coordinates.h>
#include <markoff/live-render/Marker.h>
#include <markoff/live-render/MarkerScrubber.h>
#include <markoff/live-render/LiveSelectionView.h>

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
        // Mirror QML ordering: text Q_PROPERTY is bound to model.text BEFORE
        // the document is wired. Without this, setRawTextDocument's
        // pushTextToDocument() pass would silently overwrite the test's
        // pre-populated QTextEdit with the empty default m_text.
        eb.setText("hello world");
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
        // (eb.setText below mirrors QML ordering — see test 1 for rationale.)

        LiveEditBinding eb;
        eb.setBinding(&binding);
        eb.setModelIndex(1);
        eb.setText("second");
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
        eb.setText("abcdef");
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
        eb.setText("héllo");
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
        eb.setText("hello");
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

    void ime_composition_defers_then_flushes_on_commit() {
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
        eb.setText("hello");
        eb.setRawTextDocument(editor.document());

        const quint64 seqBefore = document.editSequence();

        // Simulate composition start: composing = true.
        eb.setComposing(true);

        // Simulate preedit character changes. None of these should call
        // applyLocalEdit because the composing guard skips them.
        QTextCursor cur(editor.document());
        cur.setPosition(5);
        cur.insertText("a");        // -> "helloa" (preedit-stage 1)
        cur.insertText("b");        // -> "helloab"
        cur.insertText("c");        // -> "helloabc"

        // editSequence MUST NOT have advanced — preedit is deferred.
        QCOMPARE(document.editSequence(), seqBefore);

        // Composition commits: composing = false. One applyLocalEdit fires.
        eb.setComposing(false);

        QVERIFY(document.editSequence() > seqBefore);
        QCOMPARE(document.toMarkdown(), QString("helloabc"));
    }

    void in_flight_parse_does_not_clobber_model_text_when_stale() {
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

        // User types 'X'. Model text is "hello"; row seq stamped.
        QTextCursor cur(editor.document());
        cur.setPosition(5);
        cur.insertText("X");
        const quint64 rowSeqAfterType = binding.model()->rowEditSequence(0);
        QVERIFY(rowSeqAfterType > 0);

        // Stale parse arrives with DIFFERENT text (simulating a parser
        // normalization the user's edit hasn't been folded into yet).
        BlockRecord staleRec;
        staleRec.kind        = BlockKind::Paragraph;
        staleRec.text        = QStringLiteral("STALE-NORMALIZED-TEXT");
        staleRec.blockAnchor = binding.model()->recordAt(0).blockAnchor;

        QList<BlockKey> keys{ BlockKey{ staleRec.kind, staleRec.blockAnchor } };
        binding.model()->applyOps(AstBlockDiff::diff(keys, keys), { staleRec },
                                  /*parseInputEditSeq=*/rowSeqAfterType - 1);

        // Stale rule: model.text PRESERVED at its pre-stale-parse value.
        QCOMPARE(binding.model()->recordAt(0).text, QString("hello"));

        // Fresh parse arrives covering the edit. Now model.text accepts.
        BlockRecord freshRec;
        freshRec.kind        = BlockKind::Paragraph;
        freshRec.text        = QStringLiteral("helloX");
        freshRec.blockAnchor = binding.model()->recordAt(0).blockAnchor;

        binding.model()->applyOps(AstBlockDiff::diff(keys, keys), { freshRec },
                                  /*parseInputEditSeq=*/rowSeqAfterType);
        QCOMPARE(binding.model()->recordAt(0).text, QString("helloX"));
    }

    void firstEdit_intoMarkerOnlyBlock_bundlesScrubAndInsert();

    void copyToClipboard_acrossMarker_stripsMarkerChars();

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

void TstLiveRenderParagraphEdit::firstEdit_intoMarkerOnlyBlock_bundlesScrubAndInsert()
{
    using namespace Markoff::LiveRender;

    Markoff::MarkoffDocument document(/*replicaId=*/1);
    LiveListModelBinding binding;
    binding.setDocument(&document);

    // Set up: a doc with two paragraphs: "alpha" and a marker-only paragraph.
    // "alpha\n\n<MARKER>\n" => 2 blocks in the model.
    const QString markerContent = QStringLiteral("alpha\n\n%1\n").arg(kMarkerChar);
    QVERIFY(waitForModelRows(binding, document, markerContent.toUtf8(), 2));
    QCOMPARE(binding.model()->rowCount(), 2);

    // Sanity: second block's text is the marker.
    QVERIFY(MarkerScrubber::isMarkerOnly(binding.model()->recordAt(1).text));

    // QTextEdit-backed document fires contentsChange(int,int,int) correctly.
    QTextEdit editor;
    editor.setPlainText(QString(kMarkerChar));  // mirrors row 1's content

    LiveEditBinding eb;
    eb.setBinding(&binding);
    eb.setModelIndex(1);
    // Mirror QML ordering: setText before wiring the document.
    eb.setText(QString(kMarkerChar));
    eb.setRawTextDocument(editor.document());

    // Sanity: pre-edit doc still has the marker.
    QVERIFY(document.toMarkdown().contains(kMarkerChar));

    const quint64 preEditSeq = document.editSequence();

    // Simulate the user typing 'x' at qtPos 0 (before the marker).
    QTextCursor cur(editor.document());
    cur.setPosition(0);
    cur.insertText(QStringLiteral("x"));

    // After the bundling: source paragraph contains "x" only (no marker),
    // and exactly ONE editSequence bump occurred (one batched edit, not two).
    QVERIFY(!document.toMarkdown().contains(kMarkerChar));
    QCOMPARE(document.toMarkdown(), QStringLiteral("alpha\n\nx\n"));
    QCOMPARE(document.editSequence(), preEditSeq + 1);
}

void TstLiveRenderParagraphEdit::copyToClipboard_acrossMarker_stripsMarkerChars()
{
    using namespace Markoff::LiveRender;

    Markoff::MarkoffDocument document(/*replicaId=*/1);
    LiveListModelBinding binding;
    binding.setDocument(&document);

    // Set up: a doc with three paragraphs: "alpha", marker-only, and "beta".
    // "alpha\n\n<MARKER>\n\nbeta\n" => 3 blocks in the model.
    const QString content = QStringLiteral("alpha\n\n%1\n\nbeta\n").arg(kMarkerChar);
    QVERIFY(waitForModelRows(binding, document, content.toUtf8(), 3));
    QCOMPARE(binding.model()->rowCount(), 3);

    // Sanity: middle block's text is the marker.
    QVERIFY(MarkerScrubber::isMarkerOnly(binding.model()->recordAt(1).text));

    LiveSelectionView selView;
    selView.setDocument(&document);
    selView.setSession(nullptr);  // selection sync doesn't happen without session
    selView.setModel(binding.model());

    // Select all three blocks by setting anchor at (0,0) and active at (2, end).
    selView.begin(0, 0);
    selView.extend(2, binding.model()->recordAt(2).text.length());

    // Gather block texts from the model.
    QStringList blockTexts;
    for (int i = 0; i < binding.model()->rowCount(); ++i) {
        blockTexts << binding.model()->recordAt(i).text;
    }

    // Copy to clipboard.
    selView.copyToClipboard(blockTexts);

    // Verify the clipboard text contains no marker characters.
    QString clipboardText = QApplication::clipboard()->text();
    QVERIFY(!clipboardText.contains(kMarkerChar));
    // Clipboard should have "alpha", marker stripped, "beta" joined with newlines.
    QCOMPARE(clipboardText, QStringLiteral("alpha\n\nbeta"));
}

QTEST_MAIN(TstLiveRenderParagraphEdit)
#include "tst_live_render_paragraph_edit.moc"
