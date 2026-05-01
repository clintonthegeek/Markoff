// SPDX-License-Identifier: GPL-3.0-or-later
#include <QSignalSpy>
#include <QTest>

#include <markoff/view/qml/LiveBlockModel.h>
#include <markoff/view/qml/LiveSpeculativeFenceController.h>
#include <markoff/view/qml/BlockKind.h>

using namespace Markoff::View::Qml;
using Markoff::BlockAnchor;

namespace {

BlockRecord makeBlock(const QString &kind, const QString &text = {})
{
    BlockRecord r;
    r.kind = kind;
    r.text = text;
    r.source = text;
    return r;
}

}  // namespace

class TstLiveSpeculativeFence : public QObject {
    Q_OBJECT

private Q_SLOTS:

    // -----------------------------------------------------------------------
    // 1. Fence opener speculatively changes paragraph kind to code_block
    // -----------------------------------------------------------------------
    void fence_opener_speculatively_changes_kind()
    {
        LiveBlockModel model;
        model.setRecords({ makeBlock(BlockKind::Paragraph) });

        LiveSpeculativeFenceController ctrl;
        ctrl.setModel(&model);

        BlockAnchor anchor;
        ctrl.onEditApplied(anchor, 0, QStringLiteral("```cpp"));

        QVERIFY(model.isSpeculative(0));
        QCOMPARE(model.data(model.index(0, 0), model.roleForName("kind")).toString(),
                 BlockKind::CodeBlock);
    }

    // -----------------------------------------------------------------------
    // 2. Fence erased reverts the speculative kind back to paragraph
    // -----------------------------------------------------------------------
    void fence_erased_reverts_kind()
    {
        LiveBlockModel model;
        model.setRecords({ makeBlock(BlockKind::Paragraph) });

        LiveSpeculativeFenceController ctrl;
        ctrl.setModel(&model);

        BlockAnchor anchor;
        // First, speculatively apply
        ctrl.onEditApplied(anchor, 0, QStringLiteral("```cpp"));
        QVERIFY(model.isSpeculative(0));

        // Now user deletes back to non-fence text
        ctrl.onEditApplied(anchor, 0, QStringLiteral("hello"));
        QVERIFY(!model.isSpeculative(0));
        QCOMPARE(model.data(model.index(0, 0), model.roleForName("kind")).toString(),
                 BlockKind::Paragraph);
    }

    // -----------------------------------------------------------------------
    // 3. Parse arrival (applyOps) clears speculation; parser confirms code_block
    // -----------------------------------------------------------------------
    void parse_arrival_clears_speculation()
    {
        LiveBlockModel model;
        model.setRecords({ makeBlock(BlockKind::Paragraph) });

        LiveSpeculativeFenceController ctrl;
        ctrl.setModel(&model);

        BlockAnchor anchor;
        ctrl.onEditApplied(anchor, 0, QStringLiteral("```cpp"));
        QVERIFY(model.isSpeculative(0));

        // Simulate parser arriving with code_block
        BlockRecord confirmed = makeBlock(BlockKind::CodeBlock);
        confirmed.codeLanguage = QStringLiteral("cpp");
        model.applyOps(
            { { AstBlockDiff::OpKind::Equal, 0, 0 } },
            { confirmed });

        QVERIFY(!model.isSpeculative(0));
        QCOMPARE(model.data(model.index(0, 0), model.roleForName("kind")).toString(),
                 BlockKind::CodeBlock);
    }

    // -----------------------------------------------------------------------
    // 4. Wrong speculation corrected by parse (parser says paragraph)
    // -----------------------------------------------------------------------
    void wrong_speculation_corrected_by_parse()
    {
        LiveBlockModel model;
        model.setRecords({ makeBlock(BlockKind::Paragraph) });

        LiveSpeculativeFenceController ctrl;
        ctrl.setModel(&model);

        BlockAnchor anchor;
        // Speculatively set to code_block
        ctrl.onEditApplied(anchor, 0, QStringLiteral("```"));
        QVERIFY(model.isSpeculative(0));

        // Parser arrives and says it's still a paragraph (e.g. single backtick)
        BlockRecord confirmed = makeBlock(BlockKind::Paragraph, QStringLiteral("`"));
        model.applyOps(
            { { AstBlockDiff::OpKind::Equal, 0, 0 } },
            { confirmed });

        QVERIFY(!model.isSpeculative(0));
        QCOMPARE(model.data(model.index(0, 0), model.roleForName("kind")).toString(),
                 BlockKind::Paragraph);
    }

    // -----------------------------------------------------------------------
    // 5. Non-paragraph block is not speculatively changed
    // -----------------------------------------------------------------------
    void non_paragraph_block_not_speculated()
    {
        LiveBlockModel model;
        model.setRecords({ makeBlock(BlockKind::CodeBlock) });

        LiveSpeculativeFenceController ctrl;
        ctrl.setModel(&model);

        BlockAnchor anchor;
        // Feed fence text to a block that's already a code_block
        ctrl.onEditApplied(anchor, 0, QStringLiteral("```python"));

        // Kind should remain code_block (not double-speculated)
        QVERIFY(!model.isSpeculative(0));
        QCOMPARE(model.data(model.index(0, 0), model.roleForName("kind")).toString(),
                 BlockKind::CodeBlock);
    }

    // -----------------------------------------------------------------------
    // 6. Tilde fence opener also triggers speculation
    // -----------------------------------------------------------------------
    void tilde_fence_opener()
    {
        LiveBlockModel model;
        model.setRecords({ makeBlock(BlockKind::Paragraph) });

        LiveSpeculativeFenceController ctrl;
        ctrl.setModel(&model);

        BlockAnchor anchor;
        ctrl.onEditApplied(anchor, 0, QStringLiteral("~~~python"));

        QVERIFY(model.isSpeculative(0));
        QCOMPARE(model.data(model.index(0, 0), model.roleForName("kind")).toString(),
                 BlockKind::CodeBlock);
    }
};

QTEST_MAIN(TstLiveSpeculativeFence)
#include "tst_view_qml_live_speculative_fence.moc"
