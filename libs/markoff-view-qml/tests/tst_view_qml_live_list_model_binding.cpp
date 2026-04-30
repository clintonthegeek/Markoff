// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>

#include <markoff/view/qml/EditorBackend.h>
#include <markoff/view/qml/LiveListModelBinding.h>
#include <markoff/view/qml/LiveBlockModel.h>
#include <markoff/view/qml/LiveSelectionModel.h>
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

        LiveSelectionModel *sel = binding.selectionModel();
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

        LiveSelectionModel *sel = binding.selectionModel();
        sel->begin(2, 0);
        sel->extend(2, 1);

        // Edit the first paragraph (block 0). Append "!" after "a" at byte 1.
        Markoff::MarkoffEdit edit;
        edit.oldStart = 1;
        edit.oldEnd   = 1;
        edit.newText  = QByteArrayLiteral("!");
        doc.applyLocalEdit({ edit });

        // Wait for the model to reflect the edit, then verify selection
        // survived. The first paragraph's text should now be "a!".
        QTRY_COMPARE(model->data(model->index(0, 0), model->roleForName("text")).toString(),
                     QStringLiteral("a!"));
        QVERIFY(sel->hasSelection());
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

        // Final paragraph should read "beta!!!".
        QTRY_COMPARE(
            model->data(model->index(1, 0), model->roleForName("text")).toString(),
            QStringLiteral("beta!!!"));
        QCOMPARE(model->rowCount(), 2);
    }
};

QTEST_MAIN(TstLiveListModelBinding)
#include "tst_view_qml_live_list_model_binding.moc"
