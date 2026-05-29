// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include "../src/KindTransition.h"
#include <markoff/live/BlockKind.h>
#include <markoff/live/LiveListModelBinding.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Cmd/D2.h>

using namespace Markoff::Live;

class TstKindTransition : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void heading_h1()           { QCOMPARE(inferBlockKind(QStringLiteral("# Hello")),    BlockKind::Heading); }
    void heading_h6()           { QCOMPARE(inferBlockKind(QStringLiteral("###### x")),   BlockKind::Heading); }
    void heading_no_space()     { QCOMPARE(inferBlockKind(QStringLiteral("#Hello")),      BlockKind::Paragraph); }
    void code_block_backtick()  { QCOMPARE(inferBlockKind(QStringLiteral("```cpp")),     BlockKind::CodeBlock); }
    void code_block_tilde()     { QCOMPARE(inferBlockKind(QStringLiteral("~~~")),        BlockKind::CodeBlock); }
    void hr_dashes()            { QCOMPARE(inferBlockKind(QStringLiteral("---")),        BlockKind::HorizontalRule); }
    void hr_stars()             { QCOMPARE(inferBlockKind(QStringLiteral("***")),        BlockKind::HorizontalRule); }
    void hr_underscores()       { QCOMPARE(inferBlockKind(QStringLiteral("___")),        BlockKind::HorizontalRule); }
    void image_detected()       { QCOMPARE(inferBlockKind(QStringLiteral("![alt](url)")), BlockKind::Image); }
    void math_display()         { QCOMPARE(inferBlockKind(QStringLiteral("$$x^2$$")),   BlockKind::Math); }
    void math_inline()          { QCOMPARE(inferBlockKind(QStringLiteral("$x$")),        BlockKind::Math); }
    void list_dash()            { QCOMPARE(inferBlockKind(QStringLiteral("- item")),     BlockKind::ListItem); }
    void list_star()            { QCOMPARE(inferBlockKind(QStringLiteral("* item")),     BlockKind::ListItem); }
    void list_plus()            { QCOMPARE(inferBlockKind(QStringLiteral("+ item")),     BlockKind::ListItem); }
    void list_ordered_dot()     { QCOMPARE(inferBlockKind(QStringLiteral("1. item")),   BlockKind::ListItem); }
    void list_ordered_paren()   { QCOMPARE(inferBlockKind(QStringLiteral("2) item")),   BlockKind::ListItem); }
    void blockquote()           { QCOMPARE(inferBlockKind(QStringLiteral("> quote")),   BlockKind::Blockquote); }
    void paragraph_plain()      { QCOMPARE(inferBlockKind(QStringLiteral("just text")), BlockKind::Paragraph); }
    void empty_is_paragraph()   { QCOMPARE(inferBlockKind(QString{}),                   BlockKind::Paragraph); }
    void display_mode_true() {
        bool d = false;
        inferBlockKind(QStringLiteral("$$x$$"), &d);
        QVERIFY(d);
    }
    void display_mode_false() {
        bool d = true;
        inferBlockKind(QStringLiteral("$x$"), &d);
        QVERIFY(!d);
    }
    void count_leading_hashes_h3()   { QCOMPARE(countLeadingHashes(QStringLiteral("### x")), 3); }
    void count_leading_hashes_none() { QCOMPARE(countLeadingHashes(QStringLiteral("hello")),  0); }
    void count_leading_hashes_7()    { QCOMPARE(countLeadingHashes(QStringLiteral("####### x")), 0); }  // >6 = not heading

    void inferBlockKind_setextH2_returnsHeading();
    void inferBlockKind_setextH1_returnsHeading();
    void inferBlockKind_bareDashes_stillReturnsHorizontalRule();
    void inferBlockKind_emptyTextLineThenDashes_returnsParagraph();
    void inferBlockKind_blankLineAboveUnderline_returnsParagraph();
    void inferBlockKind_setextWithLeadingWhitespace_returnsHeading();
    void inferBlockKind_setextWithTrailingWhitespace_returnsHeading();
    void inferBlockKind_mixedDashesAndEquals_returnsParagraph();

    void atxHeading_allHashesDeleted_demotesToParagraph();
    void setextHeading_singleLineBuffer_doesNotDemote();
    void setextHeading_explicitChangeKind_demotes();
};

void TstKindTransition::inferBlockKind_setextH2_returnsHeading()
{
    QCOMPARE(inferBlockKind(QStringLiteral("Heading\n---")),
             BlockKind::Heading);
}

void TstKindTransition::inferBlockKind_setextH1_returnsHeading()
{
    QCOMPARE(inferBlockKind(QStringLiteral("Title\n===")),
             BlockKind::Heading);
}

void TstKindTransition::inferBlockKind_bareDashes_stillReturnsHorizontalRule()
{
    // Single-line `---` is HR — regression check.
    QCOMPARE(inferBlockKind(QStringLiteral("---")),
             BlockKind::HorizontalRule);
}

void TstKindTransition::inferBlockKind_emptyTextLineThenDashes_returnsParagraph()
{
    // `\n---` — line above the underline is blank (empty); not setext.
    QCOMPARE(inferBlockKind(QStringLiteral("\n---")),
             BlockKind::Paragraph);
}

void TstKindTransition::inferBlockKind_blankLineAboveUnderline_returnsParagraph()
{
    // `Heading\n\n---` — line directly above underline is blank; not setext.
    QCOMPARE(inferBlockKind(QStringLiteral("Heading\n\n---")),
             BlockKind::Paragraph);
}

void TstKindTransition::inferBlockKind_setextWithLeadingWhitespace_returnsHeading()
{
    QCOMPARE(inferBlockKind(QStringLiteral("Heading\n  ---")),
             BlockKind::Heading);
}

void TstKindTransition::inferBlockKind_setextWithTrailingWhitespace_returnsHeading()
{
    QCOMPARE(inferBlockKind(QStringLiteral("Heading\n--- ")),
             BlockKind::Heading);
}

void TstKindTransition::inferBlockKind_mixedDashesAndEquals_returnsParagraph()
{
    // Mixed underline chars are not valid setext.
    QCOMPARE(inferBlockKind(QStringLiteral("Heading\n=-=")),
             BlockKind::Paragraph);
}

// ── Task 3D: Form-aware demote ───────────────────────────────────────────────

using Markoff::Live::LiveListModelBinding;

// Helper: wait for the model to have the expected row count with the given markdown.
// Returns true if successful within timeout.
static bool waitForModelRows(LiveListModelBinding &binding,
                              Markoff::MarkoffDocument &doc,
                              const QByteArray &markdown, int expectedRows)
{
    doc.loadFromMarkdown(markdown);
    for (int i = 0; i < 50; ++i) {
        QTest::qWait(10);
        if (binding.model()->rowCount() == expectedRows)
            return true;
    }
    return binding.model()->rowCount() == expectedRows;
}

void TstKindTransition::atxHeading_allHashesDeleted_demotesToParagraph()
{
    Markoff::MarkoffDocument doc(/*replicaId=*/1);
    LiveListModelBinding binding;
    binding.setDocument(&doc);
    QVERIFY(waitForModelRows(binding, doc, "## Heading\n", 1));
    QCOMPARE(binding.model()->recordAt(0).kind, BlockKind::Heading);

    // Simulate user deleting "## " (3 bytes).
    auto id = binding.model()->recordAt(0).blockAnchor;
    {
        Markoff::UndoLog::Transaction t(doc.d2UndoLog());
        doc.d2ApplyBufferEdit(id, 0, 3, QByteArray{}, t);
    }
    QTest::qWait(100);

    QCOMPARE(binding.model()->recordAt(0).kind, BlockKind::Paragraph);
}

// Setext buffer-canonicalisation contract (spec
// 2026-05-29-setext-heading-buffer-canonicalisation-design.md): the
// underline is stripped at load and is reconstructed from
// (content.size(), level) on save. The buffer for a setext heading is
// content-only — single-line, no '\n', no '='/'-' underline. Two
// retired tests (`setextHeading_underlineDeleted_demotesToParagraph`,
// `setextHeading_levelChangeDashesToEquals_updatesLevel`) probed a
// buffer-edit transition path that no longer exists under this
// contract; replaced by the two slots below pinning the new shape.

void TstKindTransition::setextHeading_singleLineBuffer_doesNotDemote()
{
    // Load a setext heading. After canonicalisation the buffer is a
    // single line ("Heading") with kind=Heading and headingForm=setext.
    // The kind-transition cascade must NOT demote it to Paragraph just
    // because `matchesSetextShape("Heading") == 0` — single-line is the
    // canonical setext buffer shape now.
    Markoff::MarkoffDocument doc(/*replicaId=*/1);
    LiveListModelBinding binding;
    binding.setDocument(&doc);
    QVERIFY(waitForModelRows(binding, doc, "Heading\n---\n", 1));

    auto id = binding.model()->recordAt(0).blockAnchor;
    QCOMPARE(doc.blockText(id), QByteArrayLiteral("Heading"));
    QCOMPARE(binding.model()->recordAt(0).kind, BlockKind::Heading);
    QCOMPARE(binding.model()->recordAt(0).headingForm, QString("setext"));
    QCOMPARE(binding.model()->recordAt(0).headingLevel, 2);

    // Cascade once more (no-op edit) and re-check — the cascade is the
    // demote risk we're pinning against.
    {
        Markoff::UndoLog::Transaction t(doc.d2UndoLog());
        doc.d2ApplyBufferEdit(id, /*offset=*/7, /*removedBytes=*/0,
                              QByteArrayLiteral("!"), t);
    }
    QTest::qWait(100);

    QCOMPARE(binding.model()->recordAt(0).kind, BlockKind::Heading);
    QCOMPARE(binding.model()->recordAt(0).headingForm, QString("setext"));
}

void TstKindTransition::setextHeading_explicitChangeKind_demotes()
{
    // Under the new contract the only path from setext → Paragraph is an
    // explicit Cmd::changeKind (toolbar action / kind cycling / etc.).
    // Buffer-text inference no longer triggers because the underline isn't
    // in the buffer.
    Markoff::MarkoffDocument doc(/*replicaId=*/1);
    LiveListModelBinding binding;
    binding.setDocument(&doc);
    QVERIFY(waitForModelRows(binding, doc, "Heading\n---\n", 1));
    QCOMPARE(binding.model()->recordAt(0).kind, BlockKind::Heading);

    auto id = binding.model()->recordAt(0).blockAnchor;
    Markoff::Cmd::changeKind(doc, Markoff::BlockId(id),
                             Markoff::BlockKind::Paragraph, {}, {});
    QTest::qWait(100);

    QCOMPARE(binding.model()->recordAt(0).kind, BlockKind::Paragraph);
}

QTEST_MAIN(TstKindTransition)
#include "tst_live_render_kind_transition.moc"
