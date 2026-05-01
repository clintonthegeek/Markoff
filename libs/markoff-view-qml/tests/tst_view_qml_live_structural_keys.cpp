// SPDX-License-Identifier: GPL-3.0-or-later
#include <QObject>
#include <QSignalSpy>
#include <QTest>
#include <Qt>

#include <markoff/view/qml/EditorBackend.h>
#include <markoff/view/qml/LiveBlockModel.h>
#include <markoff/view/qml/LiveListModelBinding.h>
#include <markoff/view/qml/LiveProjectionLayer.h>
#include <markoff/view/qml/LiveStructuralKeyHandler.h>
#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>
#include <markoff-foundation/Origin.h>

using Markoff::MarkoffDocument;
using Markoff::BlockAnchor;
using Markoff::MarkoffEdit;
using Markoff::View::Qml::LiveStructuralKeyHandler;
using Markoff::View::Qml::LiveBlockModel;
using Markoff::View::Qml::LiveListModelBinding;
using Markoff::View::Qml::EditorBackend;

namespace {

// Seed the document and spin until the model has the expected row count.
void seedAndWait(MarkoffDocument &doc, EditorBackend &be, LiveListModelBinding &binding,
                 const QByteArray &content, int expectedRows)
{
    doc.resetContent(content, Markoff::Origin::TestFixture);
    LiveBlockModel *model = binding.model();
    QVERIFY(model);
    QTRY_COMPARE(model->rowCount(), expectedRows);
    Q_UNUSED(be);
}

BlockAnchor anchorForRow(MarkoffDocument &doc, int row)
{
    const auto opt = doc.blockAnchorAt(row);
    Q_ASSERT(opt.has_value());
    return opt.value();
}

}  // namespace

class TstLiveStructuralKeys : public QObject {
    Q_OBJECT

private Q_SLOTS:

    // -----------------------------------------------------------------------
    // Backspace at offset 0 merges with previous block
    // -----------------------------------------------------------------------

    void backspace_at_offset_zero_merges_blocks()
    {
        MarkoffDocument doc(1);
        EditorBackend be;
        be.setDocument(&doc);
        LiveListModelBinding binding;
        binding.setEditorBackend(&be);

        // Two paragraphs separated by \n\n
        seedAndWait(doc, be, binding, "first\n\nsecond", 2);

        LiveBlockModel *model = binding.model();
        const BlockAnchor anchor1 = anchorForRow(doc, 1);  // "second" block

        LiveStructuralKeyHandler handler;
        handler.setDocument(&doc);
        handler.setModel(model);

        const bool handled = handler.tryHandle(
            Qt::Key_Backspace, Qt::NoModifier,
            anchor1, 1,
            0,    // qtPos at start of block
            true, // selectionEmpty
            QStringLiteral("second")
        );
        QVERIFY(handled);

        // After merge, one paragraph remains.
        const QByteArray result = doc.toMarkdownUtf8();
        QVERIFY(!result.contains("\n\n"));
        QVERIFY(result.contains("first"));
        QVERIFY(result.contains("second"));
    }

    // -----------------------------------------------------------------------
    // Backspace at offset 0 on first block: no-op (no previous block)
    // -----------------------------------------------------------------------

    void backspace_at_offset_zero_first_block_returns_false()
    {
        MarkoffDocument doc(1);
        EditorBackend be;
        be.setDocument(&doc);
        LiveListModelBinding binding;
        binding.setEditorBackend(&be);

        seedAndWait(doc, be, binding, "only paragraph", 1);

        LiveBlockModel *model = binding.model();
        const BlockAnchor anchor0 = anchorForRow(doc, 0);

        LiveStructuralKeyHandler handler;
        handler.setDocument(&doc);
        handler.setModel(model);

        const bool handled = handler.tryHandle(
            Qt::Key_Backspace, Qt::NoModifier,
            anchor0, 0, 0, true, QStringLiteral("only paragraph")
        );
        QVERIFY(!handled);
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("only paragraph"));
    }

    // -----------------------------------------------------------------------
    // Delete at end of block merges with next block
    // -----------------------------------------------------------------------

    void delete_at_end_of_block_merges_blocks()
    {
        MarkoffDocument doc(1);
        EditorBackend be;
        be.setDocument(&doc);
        LiveListModelBinding binding;
        binding.setEditorBackend(&be);

        seedAndWait(doc, be, binding, "alpha\n\nbeta", 2);

        LiveBlockModel *model = binding.model();
        const BlockAnchor anchor0 = anchorForRow(doc, 0);  // "alpha" block

        LiveStructuralKeyHandler handler;
        handler.setDocument(&doc);
        handler.setModel(model);

        const QString blockText = QStringLiteral("alpha");
        const bool handled = handler.tryHandle(
            Qt::Key_Delete, Qt::NoModifier,
            anchor0, 0,
            blockText.length(),  // cursor at end
            true,
            blockText
        );
        QVERIFY(handled);

        const QByteArray result = doc.toMarkdownUtf8();
        QVERIFY(!result.contains("\n\n"));
        QVERIFY(result.contains("alpha"));
        QVERIFY(result.contains("beta"));
    }

    // -----------------------------------------------------------------------
    // Delete at end of last block: no-op
    // -----------------------------------------------------------------------

    void delete_at_end_of_last_block_returns_false()
    {
        MarkoffDocument doc(1);
        EditorBackend be;
        be.setDocument(&doc);
        LiveListModelBinding binding;
        binding.setEditorBackend(&be);

        seedAndWait(doc, be, binding, "solo", 1);

        LiveBlockModel *model = binding.model();
        const BlockAnchor anchor0 = anchorForRow(doc, 0);

        LiveStructuralKeyHandler handler;
        handler.setDocument(&doc);
        handler.setModel(model);

        const bool handled = handler.tryHandle(
            Qt::Key_Delete, Qt::NoModifier,
            anchor0, 0, 4, true, QStringLiteral("solo")
        );
        QVERIFY(!handled);
    }

    // -----------------------------------------------------------------------
    // Enter at end of non-code block inserts paragraph break
    // -----------------------------------------------------------------------

    void enter_at_end_inserts_paragraph_break()
    {
        // Shape change (T20 of the live-projection-layer plan): Enter at
        // end-of-block no longer mutates source directly. It opens a
        // transient projection-layer hole; commit happens later (idle
        // timer / explicit Enter / focus-out). Source bytes are NOT
        // touched until commit.
        using Markoff::View::Qml::LiveProjectionLayer;

        MarkoffDocument doc(1);
        EditorBackend be;
        be.setDocument(&doc);
        LiveListModelBinding binding;
        binding.setEditorBackend(&be);

        seedAndWait(doc, be, binding, "hello world", 1);

        LiveBlockModel *model = binding.model();
        const BlockAnchor anchor0 = anchorForRow(doc, 0);

        // Without a layer, Enter-at-EOB is now a no-op (returns false).
        LiveStructuralKeyHandler handler;
        handler.setDocument(&doc);
        handler.setModel(model);

        const QString blockText = QStringLiteral("hello world");
        const bool handledNoLayer = handler.tryHandle(
            Qt::Key_Return, Qt::NoModifier,
            anchor0, 0,
            blockText.length(),
            true,
            blockText
        );
        QVERIFY(!handledNoLayer);
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("hello world"));

        // With a layer wired, Enter-at-EOB creates a pending hole.
        // Source remains untouched; the hole's reify offset is the end
        // of the current block.
        handler.setProjectionLayer(binding.projectionLayer());
        const bool handled = handler.tryHandle(
            Qt::Key_Return, Qt::NoModifier,
            anchor0, 0,
            blockText.length(),
            true,
            blockText
        );
        QVERIFY(handled);
        QVERIFY(binding.projectionLayer()->hasPendingBlockHole());
        // Source bytes unchanged at this point.
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("hello world"));
    }

    // -----------------------------------------------------------------------
    // Enter mid-block splits the block at cursor
    // -----------------------------------------------------------------------

    void enter_mid_block_splits_at_cursor()
    {
        MarkoffDocument doc(1);
        EditorBackend be;
        be.setDocument(&doc);
        LiveListModelBinding binding;
        binding.setEditorBackend(&be);

        // "hello world" — split after "hello" at qtPos 5
        seedAndWait(doc, be, binding, "hello world", 1);

        LiveBlockModel *model = binding.model();
        const BlockAnchor anchor0 = anchorForRow(doc, 0);

        LiveStructuralKeyHandler handler;
        handler.setDocument(&doc);
        handler.setModel(model);

        const bool handled = handler.tryHandle(
            Qt::Key_Return, Qt::NoModifier,
            anchor0, 0,
            5,    // cursor after "hello"
            true,
            QStringLiteral("hello world")
        );
        QVERIFY(handled);

        const QByteArray result = doc.toMarkdownUtf8();
        // Should contain "hello\n\n world" or similar split.
        QVERIFY(result.contains("hello"));
        QVERIFY(result.contains("world"));
        QVERIFY(result.contains("\n\n"));
    }

    // -----------------------------------------------------------------------
    // Enter inside code_block: handler returns false (pass-through)
    // -----------------------------------------------------------------------

    void enter_inside_code_block_returns_false()
    {
        MarkoffDocument doc(1);
        EditorBackend be;
        be.setDocument(&doc);
        LiveListModelBinding binding;
        binding.setEditorBackend(&be);

        seedAndWait(doc, be, binding, "```\nsome code\n```", 1);

        LiveBlockModel *model = binding.model();
        // Verify the block is indeed a code_block.
        const QString kind = model->data(
            model->index(0, 0), model->roleForName("kind")).toString();
        if (kind != QStringLiteral("code_block")) {
            QSKIP("Parser did not produce code_block — check fence syntax");
        }

        const BlockAnchor anchor0 = anchorForRow(doc, 0);

        LiveStructuralKeyHandler handler;
        handler.setDocument(&doc);
        handler.setModel(model);

        const quint64 seqBefore = doc.editSequence();

        const bool handled = handler.tryHandle(
            Qt::Key_Return, Qt::NoModifier,
            anchor0, 0, 4, true, QStringLiteral("some code")
        );
        QVERIFY(!handled);
        QCOMPARE(doc.editSequence(), seqBefore);  // no edit was applied
    }

    // -----------------------------------------------------------------------
    // Tab inside code_block: handler returns false
    // -----------------------------------------------------------------------

    void tab_inside_code_block_returns_false()
    {
        MarkoffDocument doc(1);
        EditorBackend be;
        be.setDocument(&doc);
        LiveListModelBinding binding;
        binding.setEditorBackend(&be);

        seedAndWait(doc, be, binding, "```\nsome code\n```", 1);

        LiveBlockModel *model = binding.model();
        const QString kind = model->data(
            model->index(0, 0), model->roleForName("kind")).toString();
        if (kind != QStringLiteral("code_block")) {
            QSKIP("Parser did not produce code_block — check fence syntax");
        }

        const BlockAnchor anchor0 = anchorForRow(doc, 0);

        LiveStructuralKeyHandler handler;
        handler.setDocument(&doc);
        handler.setModel(model);

        const quint64 seqBefore = doc.editSequence();

        const bool handled = handler.tryHandle(
            Qt::Key_Tab, Qt::NoModifier,
            anchor0, 0, 2, true, QStringLiteral("some code")
        );
        QVERIFY(!handled);
        QCOMPARE(doc.editSequence(), seqBefore);
    }

    // -----------------------------------------------------------------------
    // Unrelated keys always return false without applying any edit
    // -----------------------------------------------------------------------

    void unrelated_keys_return_false()
    {
        MarkoffDocument doc(1);
        EditorBackend be;
        be.setDocument(&doc);
        LiveListModelBinding binding;
        binding.setEditorBackend(&be);

        seedAndWait(doc, be, binding, "text", 1);

        LiveBlockModel *model = binding.model();
        const BlockAnchor anchor0 = anchorForRow(doc, 0);

        LiveStructuralKeyHandler handler;
        handler.setDocument(&doc);
        handler.setModel(model);

        const quint64 seqBefore = doc.editSequence();

        // Ctrl+A, regular char 'A', arrow key — none should be consumed.
        for (int key : { (int)Qt::Key_A, (int)Qt::Key_Left, (int)Qt::Key_Right,
                         (int)Qt::Key_Up, (int)Qt::Key_Down }) {
            const bool handled = handler.tryHandle(
                key, Qt::NoModifier,
                anchor0, 0, 2, true, QStringLiteral("text")
            );
            QVERIFY(!handled);
        }

        QCOMPARE(doc.editSequence(), seqBefore);
    }

    // -----------------------------------------------------------------------
    // Stage C-5: focusAfterStructuralEdit signal is emitted only for
    // mid-block Enter, with the correct expectedRow + qtPos.
    // -----------------------------------------------------------------------

    void mid_block_enter_emits_focus_after_structural_edit()
    {
        MarkoffDocument doc(1);
        EditorBackend be;
        be.setDocument(&doc);
        LiveListModelBinding binding;
        binding.setEditorBackend(&be);

        seedAndWait(doc, be, binding, "hello world", 1);

        LiveBlockModel *model = binding.model();
        const BlockAnchor anchor0 = anchorForRow(doc, 0);

        LiveStructuralKeyHandler handler;
        handler.setDocument(&doc);
        handler.setModel(model);

        QSignalSpy spy(&handler, &LiveStructuralKeyHandler::focusAfterStructuralEdit);

        const bool handled = handler.tryHandle(
            Qt::Key_Return, Qt::NoModifier,
            anchor0, 0,
            5,    // mid-block: cursor after "hello"
            true,
            QStringLiteral("hello world")
        );
        QVERIFY(handled);
        QCOMPARE(spy.count(), 1);
        const auto args = spy.takeFirst();
        QCOMPARE(args.at(0).toInt(), 1);  // expectedRow = blockIndex + 1
        QCOMPARE(args.at(1).toInt(), 0);  // qtPos = 0 (start of new row)
    }

    void enter_at_eob_does_not_emit_focus_after_structural_edit()
    {
        using Markoff::View::Qml::LiveProjectionLayer;

        MarkoffDocument doc(1);
        EditorBackend be;
        be.setDocument(&doc);
        LiveListModelBinding binding;
        binding.setEditorBackend(&be);

        seedAndWait(doc, be, binding, "hello world", 1);

        LiveBlockModel *model = binding.model();
        const BlockAnchor anchor0 = anchorForRow(doc, 0);

        LiveStructuralKeyHandler handler;
        handler.setDocument(&doc);
        handler.setModel(model);
        handler.setProjectionLayer(binding.projectionLayer());

        QSignalSpy spy(&handler, &LiveStructuralKeyHandler::focusAfterStructuralEdit);

        const QString blockText = QStringLiteral("hello world");
        const bool handled = handler.tryHandle(
            Qt::Key_Return, Qt::NoModifier,
            anchor0, 0,
            blockText.length(),  // EOB — goes through the hole flow
            true,
            blockText
        );
        QVERIFY(handled);
        QCOMPARE(spy.count(), 0);
    }

    void enter_at_qtpos_zero_does_not_emit_focus_after_structural_edit()
    {
        MarkoffDocument doc(1);
        EditorBackend be;
        be.setDocument(&doc);
        LiveListModelBinding binding;
        binding.setEditorBackend(&be);

        seedAndWait(doc, be, binding, "hello world", 1);

        LiveBlockModel *model = binding.model();
        const BlockAnchor anchor0 = anchorForRow(doc, 0);

        LiveStructuralKeyHandler handler;
        handler.setDocument(&doc);
        handler.setModel(model);

        QSignalSpy spy(&handler, &LiveStructuralKeyHandler::focusAfterStructuralEdit);

        const bool handled = handler.tryHandle(
            Qt::Key_Return, Qt::NoModifier,
            anchor0, 0,
            0,    // qtPos == 0: creates a new row ABOVE this one
            true,
            QStringLiteral("hello world")
        );
        QVERIFY(handled);
        QCOMPARE(spy.count(), 0);
    }

    void backspace_does_not_emit_focus_after_structural_edit()
    {
        MarkoffDocument doc(1);
        EditorBackend be;
        be.setDocument(&doc);
        LiveListModelBinding binding;
        binding.setEditorBackend(&be);

        seedAndWait(doc, be, binding, "first\n\nsecond", 2);

        LiveBlockModel *model = binding.model();
        const BlockAnchor anchor1 = anchorForRow(doc, 1);

        LiveStructuralKeyHandler handler;
        handler.setDocument(&doc);
        handler.setModel(model);

        QSignalSpy spy(&handler, &LiveStructuralKeyHandler::focusAfterStructuralEdit);

        const bool handled = handler.tryHandle(
            Qt::Key_Backspace, Qt::NoModifier,
            anchor1, 1,
            0, true, QStringLiteral("second")
        );
        QVERIFY(handled);
        QCOMPARE(spy.count(), 0);
    }

    void delete_does_not_emit_focus_after_structural_edit()
    {
        MarkoffDocument doc(1);
        EditorBackend be;
        be.setDocument(&doc);
        LiveListModelBinding binding;
        binding.setEditorBackend(&be);

        seedAndWait(doc, be, binding, "alpha\n\nbeta", 2);

        LiveBlockModel *model = binding.model();
        const BlockAnchor anchor0 = anchorForRow(doc, 0);

        LiveStructuralKeyHandler handler;
        handler.setDocument(&doc);
        handler.setModel(model);

        QSignalSpy spy(&handler, &LiveStructuralKeyHandler::focusAfterStructuralEdit);

        const QString blockText = QStringLiteral("alpha");
        const bool handled = handler.tryHandle(
            Qt::Key_Delete, Qt::NoModifier,
            anchor0, 0,
            blockText.length(), true, blockText
        );
        QVERIFY(handled);
        QCOMPARE(spy.count(), 0);
    }
};

QTEST_MAIN(TstLiveStructuralKeys)
#include "tst_view_qml_live_structural_keys.moc"
