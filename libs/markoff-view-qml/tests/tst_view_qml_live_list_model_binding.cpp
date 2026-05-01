// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>

#include <markoff/view/qml/EditorBackend.h>
#include <markoff/view/qml/LiveListModelBinding.h>
#include <markoff/view/qml/LiveBlockModel.h>
#include <markoff/view/qml/LiveSelectionView.h>
#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>

using namespace Markoff::View::Qml;

class TstLiveListModelBinding : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void model_populates_after_initial_parse() {
        Markoff::MarkoffDocument doc(1);
        EditorBackend be;
        be.setDocument(&doc);
        LiveListModelBinding binding;
        binding.setEditorBackend(&be);

        Markoff::MarkoffEdit ed;
        ed.oldStart = 0; ed.oldEnd = 0;
        ed.newText = QByteArrayLiteral("# Title\n\nFirst paragraph.\n");
        doc.applyLocalEdit({ ed });

        // BlockWalker runs off-thread under 1A; spin the event loop until the
        // model has been populated (queued post-back from the worker thread).
        LiveBlockModel *model = binding.model();
        QVERIFY(model != nullptr);
        QTRY_COMPARE(model->rowCount(), 2);
        QCOMPARE(model->data(model->index(0, 0), model->roleForName("kind")).toString(),
                 QStringLiteral("heading"));
        QCOMPARE(model->data(model->index(1, 0), model->roleForName("kind")).toString(),
                 QStringLiteral("paragraph"));
    }

    void single_block_edit_emits_minimal_diff() {
        Markoff::MarkoffDocument doc(1);
        EditorBackend be;
        be.setDocument(&doc);
        LiveListModelBinding binding;
        binding.setEditorBackend(&be);

        Markoff::MarkoffEdit ed;
        ed.oldStart = 0; ed.oldEnd = 0;
        ed.newText = QByteArrayLiteral("# Title\n\npara\n");
        doc.applyLocalEdit({ ed });

        LiveBlockModel *model = binding.model();
        QTRY_COMPARE(model->rowCount(), 2);

        QSignalSpy ins(model, &QAbstractItemModel::rowsInserted);
        QSignalSpy rem(model, &QAbstractItemModel::rowsRemoved);
        QSignalSpy chg(model, &QAbstractItemModel::dataChanged);

        // Append "!" at end of "para" — byte offset just before the trailing "\n".
        // After: "# Title\n\npara!\n"
        // The byte offset of the end of "para" in the source is 13.
        Markoff::MarkoffEdit edit;
        edit.oldStart = 13;
        edit.oldEnd   = 13;
        edit.newText  = QByteArrayLiteral("!");
        doc.applyLocalEdit({ edit });

        // Wait for the post-walk model apply (any of the model touch signals
        // is sufficient). Then assert touch-count + final size.
        QTRY_VERIFY(chg.count() + ins.count() + rem.count() >= 1);
        const int touched = chg.count() + ins.count() + rem.count();
        QVERIFY2(touched >= 1 && touched <= 2,
                 qPrintable(QString("expected 1-2 model touches, got %1").arg(touched)));
        QCOMPARE(model->rowCount(), 2);
    }

    void selection_clears_when_touched_block_removed() {
        Markoff::MarkoffDocument doc(1);
        EditorBackend be;
        be.setDocument(&doc);
        LiveListModelBinding binding;
        binding.setEditorBackend(&be);

        Markoff::MarkoffEdit ed;
        ed.oldStart = 0; ed.oldEnd = 0;
        ed.newText = QByteArrayLiteral("a\n\nb\n\nc\n");
        doc.applyLocalEdit({ ed });
        LiveBlockModel *model = binding.model();
        QTRY_COMPARE(model->rowCount(), 3);

        LiveSelectionView *sel = binding.selectionModel();
        QVERIFY(sel != nullptr);
        sel->begin(1, 0);
        sel->extend(1, 1);
        QVERIFY(sel->hasSelection());

        // Remove the second paragraph entirely. Source layout:
        //   a\n\nb\n\nc\n
        //   0123 4 567 8...   (\n at 1 and 5; "b\n\n" spans bytes 3..6)
        Markoff::MarkoffEdit edit;
        edit.oldStart = 3;
        edit.oldEnd   = 6;
        edit.newText  = QByteArray();
        doc.applyLocalEdit({ edit });

        QTRY_VERIFY(!sel->hasSelection());
    }

    void selection_persists_when_unrelated_block_changes() {
        Markoff::MarkoffDocument doc(1);
        EditorBackend be;
        be.setDocument(&doc);
        LiveListModelBinding binding;
        binding.setEditorBackend(&be);

        Markoff::MarkoffEdit ed;
        ed.oldStart = 0; ed.oldEnd = 0;
        ed.newText = QByteArrayLiteral("a\n\nb\n\nc\n");
        doc.applyLocalEdit({ ed });
        LiveBlockModel *model = binding.model();
        QTRY_COMPARE(model->rowCount(), 3);

        LiveSelectionView *sel = binding.selectionModel();
        sel->begin(2, 0);
        sel->extend(2, 1);

        // Edit the first paragraph (block 0). Append "!" after "a" at byte 1.
        Markoff::MarkoffEdit edit;
        edit.oldStart = 1;
        edit.oldEnd   = 1;
        edit.newText  = QByteArrayLiteral("!");
        doc.applyLocalEdit({ edit });

        // Wait for the model to reflect the edit, then verify selection
        // survived. The first paragraph's text should now be "a!" (Stage
        // C-2/C-3: source-faithful text includes the trailing newline
        // that's part of the paragraph block's byte range).
        QTRY_COMPARE(model->data(model->index(0, 0), model->roleForName("text"))
                         .toString().trimmed(),
                     QStringLiteral("a!"));
        QVERIFY(sel->hasSelection());
    }

    /// Task 7: typing "# " at the start of a paragraph causes a kind-change
    /// (paragraph → heading). The binding must emit focusRestoreRequested with
    /// the new block's anchor and the saved cursor position so the new
    /// HeadingDelegate can restore focus.
    void focus_restore_requested_on_kind_change() {
        Markoff::MarkoffDocument doc(1);
        EditorBackend be;
        be.setDocument(&doc);
        LiveListModelBinding binding;
        binding.setEditorBackend(&be);

        // Seed: one paragraph "hello world"
        Markoff::MarkoffEdit ed;
        ed.oldStart = 0; ed.oldEnd = 0;
        ed.newText = QByteArrayLiteral("hello world\n");
        doc.applyLocalEdit({ ed });

        LiveBlockModel *model = binding.model();
        QTRY_COMPARE(model->rowCount(), 1);
        QCOMPARE(model->data(model->index(0, 0), model->roleForName("kind")).toString(),
                 QStringLiteral("paragraph"));

        // Capture the paragraph's BlockAnchor from the model role.
        const QVariant anchorVar =
            model->data(model->index(0, 0), model->roleForName("blockAnchor"));
        QVERIFY(anchorVar.isValid());
        const Markoff::BlockAnchor para_anchor =
            anchorVar.value<Markoff::BlockAnchor>();

        // Simulate the delegate notifying focus at cursor position 3.
        binding.notifyFocused(para_anchor, 3);
        QVERIFY(binding.isFocusRestoreTarget(para_anchor));

        // Spy on the focusRestoreRequested signal.
        QSignalSpy restoreSpy(&binding, &LiveListModelBinding::focusRestoreRequested);

        // Apply the kind-changing edit: prepend "# " → "# hello world\n"
        Markoff::MarkoffEdit kindEdit;
        kindEdit.oldStart = 0;
        kindEdit.oldEnd = 0;
        kindEdit.newText = QByteArrayLiteral("# ");
        doc.applyLocalEdit({ kindEdit });

        // Wait for the model to reflect the heading kind.
        QTRY_COMPARE(model->data(model->index(0, 0), model->roleForName("kind")).toString(),
                     QStringLiteral("heading"));

        // The focusRestoreRequested signal must have fired.
        // It is queued so we need to pump the event loop.
        QTRY_VERIFY(restoreSpy.count() >= 1);

        // The signal carries the new block's anchor (the heading anchor —
        // BlockAnchor changes on prepend since it's the first-byte CRDT anchor)
        // and the saved cursor position.
        const auto args = restoreSpy.first();
        // The emitted anchor must match the heading block's anchor in the model.
        const Markoff::BlockAnchor heading_anchor =
            model->data(model->index(0, 0), model->roleForName("blockAnchor"))
                  .value<Markoff::BlockAnchor>();
        QCOMPARE(args[0].value<Markoff::BlockAnchor>(), heading_anchor);
        QCOMPARE(args[1].toInt(), 3);
        // isFocusRestoreTarget must now return true for the heading anchor.
        QVERIFY(binding.isFocusRestoreTarget(heading_anchor));
    }

    /// isFocusRestoreTarget returns false when no anchor is focused.
    void is_focus_restore_target_false_by_default() {
        LiveListModelBinding binding;
        Markoff::BlockAnchor dummy{};
        QVERIFY(!binding.isFocusRestoreTarget(dummy));
    }

    /// notifyFocusedCursorMoved updates the saved cursor position.
    void notify_focused_cursor_moved_updates_position() {
        Markoff::MarkoffDocument doc(1);
        EditorBackend be;
        be.setDocument(&doc);
        LiveListModelBinding binding;
        binding.setEditorBackend(&be);

        Markoff::MarkoffEdit ed;
        ed.oldStart = 0; ed.oldEnd = 0;
        ed.newText = QByteArrayLiteral("hello world\n");
        doc.applyLocalEdit({ ed });

        LiveBlockModel *model = binding.model();
        QTRY_COMPARE(model->rowCount(), 1);

        const QVariant anchorVar =
            model->data(model->index(0, 0), model->roleForName("blockAnchor"));
        const Markoff::BlockAnchor anchor = anchorVar.value<Markoff::BlockAnchor>();

        binding.notifyFocused(anchor, 0);
        binding.notifyFocusedCursorMoved(7);

        // Spy to capture the emitted position after a kind-change.
        QSignalSpy restoreSpy(&binding, &LiveListModelBinding::focusRestoreRequested);

        Markoff::MarkoffEdit kindEdit;
        kindEdit.oldStart = 0; kindEdit.oldEnd = 0;
        kindEdit.newText = QByteArrayLiteral("# ");
        doc.applyLocalEdit({ kindEdit });

        QTRY_COMPARE(model->data(model->index(0, 0), model->roleForName("kind")).toString(),
                     QStringLiteral("heading"));
        QTRY_VERIFY(restoreSpy.count() >= 1);

        // Position should be the value set by notifyFocusedCursorMoved (7).
        const auto args = restoreSpy.first();
        QCOMPARE(args[1].toInt(), 7);
    }

    /// 1A: rapid back-to-back edits — the final model state must match the
    /// final document state, regardless of how parses + walks coalesce.
    /// Without the m_walkGeneration cookie an out-of-order walk completion
    /// could clobber the model with a stale snapshot.
    void rapid_edits_converge_to_final_state() {
        Markoff::MarkoffDocument doc(1);
        EditorBackend be;
        be.setDocument(&doc);
        LiveListModelBinding binding;
        binding.setEditorBackend(&be);

        // Initial state.
        Markoff::MarkoffEdit init;
        init.oldStart = 0; init.oldEnd = 0;
        init.newText = QByteArrayLiteral("alpha\n\nbeta\n");
        doc.applyLocalEdit({ init });
        LiveBlockModel *model = binding.model();
        QTRY_COMPARE(model->rowCount(), 2);

        // Issue several edits in rapid succession with no intervening
        // event-loop pumps. Most will coalesce inside the parse pool;
        // the last one is what the model must converge to.
        // Source after init: "alpha\n\nbeta\n" (12 bytes total).
        // Append "!" at byte 11 (just before final \n) three times in a row.
        for (int i = 0; i < 3; ++i) {
            Markoff::MarkoffEdit e;
            e.oldStart = static_cast<quint32>(11 + i);
            e.oldEnd   = static_cast<quint32>(11 + i);
            e.newText  = QByteArrayLiteral("!");
            doc.applyLocalEdit({ e });
        }

        // Final paragraph should read "beta!!!" (source-faithful text
        // includes the trailing newline; trim before comparing).
        QTRY_COMPARE(
            model->data(model->index(1, 0), model->roleForName("text"))
                  .toString().trimmed(),
            QStringLiteral("beta!!!"));
        QCOMPARE(model->rowCount(), 2);
    }
};

QTEST_MAIN(TstLiveListModelBinding)
#include "tst_view_qml_live_list_model_binding.moc"
