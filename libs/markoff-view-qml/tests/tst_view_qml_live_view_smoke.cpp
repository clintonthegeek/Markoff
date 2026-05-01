// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>

#include <markoff/view/qml/EditorBackend.h>
#include <markoff/view/qml/LiveListModelBinding.h>
#include <markoff/view/qml/LiveBlockModel.h>
#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>

using namespace Markoff::View::Qml;

class TstLiveViewSmoke : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void live_view_renders_block_kinds_from_foundation() {
        Markoff::MarkoffDocument doc(1);
        EditorBackend be;
        be.setDocument(&doc);
        LiveListModelBinding binding;
        binding.setEditorBackend(&be);

        Markoff::MarkoffEdit ed;
        ed.oldStart = 0; ed.oldEnd = 0;
        ed.newText = QByteArrayLiteral(
            "# Heading\n\n"
            "Para text.\n\n"
            "---\n\n"
            "![alt](http://example.com/img.png)\n\n"
            "```python\nx = 1\n```\n");
        doc.applyLocalEdit({ ed });

        // Stage C-2: BlockWalker now consumes the foundation's
        // topLevelBlocks() snapshot synchronously inside the
        // parseUpdatedAt slot. The image-only paragraph is no longer
        // recognized as a special Image kind (the foundation does not
        // surface that distinction); it renders as a paragraph with
        // source-faithful text containing the image markdown. Inline
        // image rendering is a future enhancement.
        LiveBlockModel *model = binding.model();
        QTRY_COMPARE(model->rowCount(), 5);

        QStringList kinds;
        for (int i = 0; i < model->rowCount(); ++i) {
            kinds << model->data(model->index(i, 0),
                                 model->roleForName("kind")).toString();
        }
        QCOMPARE(kinds, (QStringList{
            QStringLiteral("heading"),
            QStringLiteral("paragraph"),
            QStringLiteral("hr"),
            QStringLiteral("paragraph"),  // was "image" before C-2
            QStringLiteral("code_block")
        }));
    }
};

QTEST_MAIN(TstLiveViewSmoke)
#include "tst_view_qml_live_view_smoke.moc"
