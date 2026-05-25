// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Markoff contributors, GPL-3.0-or-later.
//
// Asserts that MarkoffDocument::resetContent populates D2 per-block state
// (not just the legacy d->buffer). Surfaced 2026-05-20 by Corbomite Vault's
// first-open path: resetContent left the document with visibleLength>0 but
// iterateBlocks() empty, so the live view had no rows to render.
//
// Port-first session recap §"Open Markoff-side issues" #1:
// "Either resetContent should also build D2 (likely the right fix —
// Origin enum then drives only undo-stack handling) or it should be
// documented as legacy-buffer-only and consumers always use
// loadFromMarkdown."
//
// This test pins the "also builds D2" choice for the fresh-document
// origin cases (FirstOpen, TestFixture). The non-fresh "wholesale
// replace" origins (ExternalReloadClean/Resolved, UserRevertToSaved)
// require a D2 state wipe pass that the IdList CRDT doesn't yet
// expose cleanly; that's tracked as a follow-up.

#include <QTest>
#include <QSignalSpy>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/BlockKind.h>

using namespace Markoff;

class TstD2ResetContent : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void firstOpen_emptyContent_zeroBlocks();
    void firstOpen_singleParagraph_oneBlock();
    void firstOpen_headingAndParagraph_twoBlocksWithCorrectKinds();
    void testFixture_buildsD2Blocks();
    void firstOpen_extractsFrontmatter();
    void firstOpen_iterateBlocksMatchesLoadFromMarkdown();
};

void TstD2ResetContent::firstOpen_emptyContent_zeroBlocks()
{
    MarkoffDocument doc(1);
    doc.resetContent(QByteArray{}, Origin::FirstOpen);
    QCOMPARE(doc.iterateBlocks().size(), static_cast<size_t>(0));
}

void TstD2ResetContent::firstOpen_singleParagraph_oneBlock()
{
    MarkoffDocument doc(1);
    doc.resetContent(QByteArray("Hello world\n"), Origin::FirstOpen);
    QCOMPARE(doc.iterateBlocks().size(), static_cast<size_t>(1));
}

void TstD2ResetContent::firstOpen_headingAndParagraph_twoBlocksWithCorrectKinds()
{
    MarkoffDocument doc(1);
    doc.resetContent(QByteArray("# Heading\n\nParagraph\n"), Origin::FirstOpen);
    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), static_cast<size_t>(2));
    QCOMPARE(doc.blockKind(blocks[0]), BlockKind::Heading);
    QCOMPARE(doc.blockKind(blocks[1]), BlockKind::Paragraph);
}

void TstD2ResetContent::testFixture_buildsD2Blocks()
{
    MarkoffDocument doc(1);
    doc.resetContent(QByteArray("# H\n\nP\n"), Origin::TestFixture);
    QCOMPARE(doc.iterateBlocks().size(), static_cast<size_t>(2));
}

void TstD2ResetContent::firstOpen_extractsFrontmatter()
{
    MarkoffDocument doc(1);
    const QByteArray src =
        "---\n"
        "title: Test\n"
        "---\n"
        "\n"
        "body paragraph\n";
    doc.resetContent(src, Origin::FirstOpen);
    // Frontmatter materialized; body parsed into a single paragraph block.
    QCOMPARE(doc.iterateBlocks().size(), static_cast<size_t>(1));
    const auto fm = doc.frontmatterValue(QByteArray("raw"));
    QVERIFY(fm.has_value());
    QVERIFY(fm->contains("title: Test"));
}

void TstD2ResetContent::firstOpen_iterateBlocksMatchesLoadFromMarkdown()
{
    // The invariant Corbomite's Vault depends on: resetContent(bytes, FirstOpen)
    // and loadFromMarkdown(bytes) produce the same iterateBlocks() result.
    const QByteArray src = "# A\n\nB\n\n## C\n\nD\n";

    MarkoffDocument loaded(1);
    loaded.loadFromMarkdown(src);

    MarkoffDocument reset(1);
    reset.resetContent(src, Origin::FirstOpen);

    const auto loadedBlocks = loaded.iterateBlocks();
    const auto resetBlocks  = reset.iterateBlocks();
    QCOMPARE(resetBlocks.size(), loadedBlocks.size());
    for (size_t i = 0; i < loadedBlocks.size(); ++i) {
        QCOMPARE(reset.blockKind(resetBlocks[i]),
                 loaded.blockKind(loadedBlocks[i]));
        QCOMPARE(reset.blockText(resetBlocks[i]),
                 loaded.blockText(loadedBlocks[i]));
    }
}

QTEST_GUILESS_MAIN(TstD2ResetContent)
#include "tst_d2_reset_content.moc"
