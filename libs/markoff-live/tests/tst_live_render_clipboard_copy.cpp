// SPDX-License-Identifier: GPL-3.0-or-later
//
// Cross-block copy: regression test for the dogfood-surfaced bug where
// copyToClipboard previously took a `QStringList blockTexts` argument the
// QML side built by walking `ListView.itemAtIndex(i)` for i ∈ [0, count).
// ListView only realises the visible window, so off-screen rows in a
// cross-block selection contributed empty strings. The fix moves text
// reads into C++ — copyToClipboard now reads from the bound LiveBlockModel
// directly.

#include <QTest>
#include <QApplication>
#include <QClipboard>

#include <markoff/live/LiveListModelBinding.h>
#include <markoff/live/LiveSelectionView.h>
#include <markoff/live/LiveBlockModel.h>

#include <markoff/core/MarkoffDocument.h>

using namespace Markoff::Live;

class TstLiveRenderClipboardCopy : public QObject {
    Q_OBJECT

private Q_SLOTS:

    void cross_block_copy_captures_all_selected_rows_including_offscreen() {
        // Five paragraph blocks; selection spans all five. The QML-walking
        // pre-fix code would (in a real ListView) miss blocks 1..3 if the
        // viewport only realised blocks 0 and 4 — yielding "alpha\n\n\n\nepsilon".
        // The model-driven path produces every row regardless of realisation.
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown(
            "alpha\n\nbeta\n\ngamma\n\ndelta\n\nepsilon");
        QCOMPARE(binding.model()->rowCount(), 5);

        auto *sv = binding.selectionView();
        QVERIFY(sv);

        // Selection covers the full extent of all five blocks.
        sv->begin(/*blockIndex=*/0, /*qtPos=*/0);
        sv->extend(/*blockIndex=*/4, /*qtPos=*/binding.model()->recordAt(4).text.length());
        QVERIFY(sv->hasSelection());

        QApplication::clipboard()->clear();
        sv->copyToClipboard();

        const QString clip = QApplication::clipboard()->text();
        // Each block's text contributed; blocks separated by '\n'.
        QCOMPARE(clip, QStringLiteral("alpha\nbeta\ngamma\ndelta\nepsilon"));
    }

    void cross_block_copy_partial_endpoints_clipped_correctly() {
        // Verify the start/end clipping in copyToClipboard still works
        // against model-driven texts.
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("alphabet\n\nbravado\n\ncharlie");
        QCOMPARE(binding.model()->rowCount(), 3);

        auto *sv = binding.selectionView();
        QVERIFY(sv);

        // Select from "abet" of "alphabet" (qtPos 4) through "char" (qtPos 4 of charlie).
        sv->begin(/*blockIndex=*/0, /*qtPos=*/4);
        sv->extend(/*blockIndex=*/2, /*qtPos=*/4);
        QVERIFY(sv->hasSelection());

        QApplication::clipboard()->clear();
        sv->copyToClipboard();

        const QString clip = QApplication::clipboard()->text();
        QCOMPARE(clip, QStringLiteral("abet\nbravado\nchar"));
    }
};

QTEST_MAIN(TstLiveRenderClipboardCopy)
#include "tst_live_render_clipboard_copy.moc"
