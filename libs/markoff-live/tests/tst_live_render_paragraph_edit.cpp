// SPDX-License-Identifier: GPL-3.0-or-later
//
// NOTE ON TEST SETUP: QTextDocument::contentsChange(int,int,int) — the
// signal wired by LiveEditBinding — only fires when the document is
// managed by a QTextControl (i.e. when it belongs to a QTextEdit or a
// QQuickTextEdit). A bare QTextDocument only fires the no-arg
// contentsChanged(). Therefore all tests use QTextEdit (Widgets) to
// get a properly-wired QTextDocument; setRawTextDocument(editor.document())
// wires the binding to the editor's underlying document.
//
// Tests use loadFromMarkdown + structureChanged. Model rows arrive via
// onD2Changed driven by the IdListProxy and KindTagMap signals.

#include <QTest>
#include <QSignalSpy>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QApplication>

#include <markoff/live/LiveEditBinding.h>
#include <markoff/live/LiveListModelBinding.h>
#include <markoff/live/LiveBlockModel.h>
#include <markoff/live/BlockKind.h>
#include <markoff/live/Coordinates.h>
#include <markoff/live/LiveSelectionView.h>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/CrdtProxies.h>

using namespace Markoff::Live;

// Helper: load markdown into a D2 document and wait for the model rows to
// populate via the structureChanged/mapChanged → onD2Changed pipeline.
// loadFromMarkdown fires structureChanged synchronously, so rows are typically
// populated immediately; the spy.wait fallback covers deferred cases.
static bool waitForModelRows(LiveListModelBinding &binding,
                              Markoff::MarkoffDocument &doc,
                              const QByteArray &content,
                              int expectedRows,
                              int timeoutMs = 2000)
{
    doc.loadFromMarkdown(content);
    if (binding.model()->rowCount() == expectedRows)
        return true;
    // structureChanged fires synchronously in loadFromMarkdown; if rows aren't
    // there yet, poll briefly in case there's a deferred emit.
    QSignalSpy spy(doc.idListProxy(), &Markoff::IdListProxy::structureChanged);
    if (!spy.wait(timeoutMs))
        return binding.model()->rowCount() == expectedRows;
    return binding.model()->rowCount() == expectedRows;
}

class TstLiveRenderParagraphEdit : public QObject {
    Q_OBJECT

private Q_SLOTS:

    void typing_one_char_updates_block_text() {
        Markoff::MarkoffDocument document(/*replicaId=*/1);

        LiveListModelBinding binding;
        binding.setDocument(&document);

        QVERIFY(waitForModelRows(binding, document, "hello world", 1));
        QCOMPARE(binding.model()->rowCount(), 1);

        const Markoff::BlockId blockId = binding.model()->recordAt(0).blockAnchor;

        // QTextEdit-backed document fires contentsChange(int,int,int) correctly.
        QTextEdit editor;
        editor.setPlainText("hello world");

        LiveEditBinding eb;
        eb.setBinding(&binding);
        eb.setModelIndex(0);
        // Mirror QML ordering: text Q_PROPERTY is bound to model.text BEFORE
        // the document is wired.
        eb.setText("hello world");
        eb.setRawTextDocument(editor.document());

        const quint64 seqBefore = document.d2EditSequence();

        // Simulate typing 'A' at position 5: "hello world" -> "helloA world".
        QTextCursor cur(editor.document());
        cur.setPosition(5);
        cur.insertText("A");

        QVERIFY(document.d2EditSequence() > seqBefore);
        QCOMPARE(document.blockText(blockId), QByteArrayLiteral("helloA world"));
    }

    void deletion_emits_correct_block_change() {
        Markoff::MarkoffDocument document(/*replicaId=*/1);

        LiveListModelBinding binding;
        binding.setDocument(&document);

        QVERIFY(waitForModelRows(binding, document, "abcdef", 1));

        const Markoff::BlockId blockId = binding.model()->recordAt(0).blockAnchor;

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

        QCOMPARE(document.blockText(blockId), QByteArrayLiteral("abef"));
    }

    void utf8_multibyte_byte_offsets_correct() {
        // "héllo" is 6 UTF-8 bytes; é is 2 bytes (0xC3 0xA9). The QChar
        // count is 5 — so qtPos 2 == byte offset 3 ('l').
        Markoff::MarkoffDocument document(/*replicaId=*/1);

        LiveListModelBinding binding;
        binding.setDocument(&document);

        QVERIFY(waitForModelRows(binding, document, "héllo", 1));

        const Markoff::BlockId blockId = binding.model()->recordAt(0).blockAnchor;

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

        QCOMPARE(document.blockText(blockId), QByteArrayLiteral("héXllo"));
    }

    void typing_two_chars_before_model_update_does_not_scramble() {
        // Reproduces the stale-record-text race: user types 'A' then 'B'
        // before a D2 change notification delivers the post-'A' state to
        // the model. With m_previousText caching, edits compute against the
        // CRDT-coherent before-state and stay correct.
        Markoff::MarkoffDocument document(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&document);
        QVERIFY(waitForModelRows(binding, document, "hello", 1));

        const Markoff::BlockId blockId = binding.model()->recordAt(0).blockAnchor;

        QTextEdit editor;
        editor.setPlainText("hello");
        LiveEditBinding eb;
        eb.setBinding(&binding);
        eb.setModelIndex(0);
        eb.setText("hello");
        eb.setRawTextDocument(editor.document());

        // Type 'A' at end.
        QTextCursor cur(editor.document());
        cur.setPosition(5);
        cur.insertText("A");

        // Type 'B' at end without waiting for model update.
        cur.setPosition(6);
        cur.insertText("B");

        // CRDT must reflect "helloAB", not "helloBA".
        QCOMPARE(document.blockText(blockId), QByteArrayLiteral("helloAB"));
    }

    void ime_composition_defers_then_flushes_on_commit() {
        Markoff::MarkoffDocument document(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&document);
        QVERIFY(waitForModelRows(binding, document, "hello", 1));

        const Markoff::BlockId blockId = binding.model()->recordAt(0).blockAnchor;

        QTextEdit editor;
        editor.setPlainText("hello");
        LiveEditBinding eb;
        eb.setBinding(&binding);
        eb.setModelIndex(0);
        eb.setText("hello");
        eb.setRawTextDocument(editor.document());

        const quint64 seqBefore = document.d2EditSequence();

        // Simulate composition start: composing = true.
        eb.setComposing(true);

        // Simulate preedit character changes. None of these should call
        // d2ApplyBufferEdit because the composing guard skips them.
        QTextCursor cur(editor.document());
        cur.setPosition(5);
        cur.insertText("a");        // -> "helloa" (preedit-stage 1)
        cur.insertText("b");        // -> "helloab"
        cur.insertText("c");        // -> "helloabc"

        // d2EditSequence MUST NOT have advanced — preedit is deferred.
        QCOMPARE(document.d2EditSequence(), seqBefore);

        // Composition commits: composing = false. One flush fires.
        eb.setComposing(false);

        QVERIFY(document.d2EditSequence() > seqBefore);
        QCOMPARE(document.blockText(blockId), QByteArrayLiteral("helloabc"));
    }

    void model_update_does_not_echo_back_to_d2() {
        // Verify the core cycle-guard invariant: applyingModelUpdate() is true
        // while onD2Changed is mutating the model.
        // In D2, loads produce Insert ops (new block IDs) not Equal/dataChanged,
        // so we check via rowsInserted which fires from beginInsertRows/endInsertRows.
        Markoff::MarkoffDocument document(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&document);
        QVERIFY(waitForModelRows(binding, document, "aaa", 1));

        // Now assert the principle: during rowsInserted from onD2Changed,
        // applyingModelUpdate is true.
        // loadFromMarkdown fires documentLoaded → onD2Changed synchronously.
        bool flagSeenDuringInsert = false;
        bool rowsInsertedFired = false;
        auto conn = QObject::connect(binding.model(), &QAbstractItemModel::rowsInserted,
                                      [&](){
                                          rowsInsertedFired = true;
                                          flagSeenDuringInsert = binding.applyingModelUpdate();
                                      });

        // Load a two-paragraph doc — guaranteed to fire rowsInserted for row 1.
        document.loadFromMarkdown("first\n\nsecond");

        QObject::disconnect(conn);
        QVERIFY2(rowsInsertedFired, "rowsInserted should have fired during onD2Changed");
        QVERIFY2(flagSeenDuringInsert,
                 "applyingModelUpdate should be true while rowsInserted fires");
    }

    void typing_flushes_d2_changed_synchronously_before_paint() {
        // Regression: when a char is typed mid-paragraph, the QSyntaxHighlighter
        // subscribed to QTextDocument::contentsChange runs highlightBlock
        // synchronously inside the same emission chain. If the model→delegate
        // span-update cascade is still queued (debounced d2DocumentChanged via
        // QTimer::singleShot(0)), the highlighter formats the post-edit text
        // with the *pre-edit* span offsets — inline delimiters at offsets after
        // the insertion point appear visible for one paint frame before the
        // queued timer fires and corrects them. Visible as a flicker.
        //
        // The fix is in LiveEditBinding::onContentsChange, which calls
        // MarkoffDocument::flushPendingD2Changed() after applying the buffer
        // edit. This test asserts that d2DocumentChanged has fired before the
        // QTextDocument::contentsChange emission chain returns.
        Markoff::MarkoffDocument document(/*replicaId=*/1);

        LiveListModelBinding binding;
        binding.setDocument(&document);

        QVERIFY(waitForModelRows(binding, document, "hello world", 1));

        QTextEdit editor;
        editor.setPlainText("hello world");
        LiveEditBinding eb;
        eb.setBinding(&binding);
        eb.setModelIndex(0);
        eb.setText("hello world");
        eb.setRawTextDocument(editor.document());

        QSignalSpy d2Spy(&document, &Markoff::MarkoffDocument::d2DocumentChanged);
        d2Spy.clear();

        // Insert one char — fires QTextDocument::contentsChange synchronously,
        // which routes through LiveEditBinding::onContentsChange. By the time
        // insertText() returns, d2DocumentChanged must already have fired
        // (synchronously, via flushPendingD2Changed) so any QSyntaxHighlighter
        // running next in the contentsChange emission chain sees fresh spans.
        QTextCursor cur(editor.document());
        cur.setPosition(5);
        cur.insertText("X");

        QVERIFY2(d2Spy.count() >= 1,
                 "d2DocumentChanged must fire synchronously inside the "
                 "contentsChange emission chain — otherwise QSyntaxHighlighter "
                 "subscribers see stale inline spans, causing a one-frame "
                 "flicker of inline-delimiter formatting after the edit point.");
    }
};

QTEST_MAIN(TstLiveRenderParagraphEdit)
#include "tst_live_render_paragraph_edit.moc"
