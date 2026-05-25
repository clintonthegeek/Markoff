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
    void nonFreshReset_replacePlain_noResidue();
    void nonFreshReset_replaceHeader_noResidue();
    void nonFreshReset_replaceUnicode_noResidue();
    void nonFreshReset_externalReloadClean_noResidue();
    void nonFreshReset_externalReloadResolved_noResidue();
    void nonFreshReset_userRevertToSaved_noResidue();
    void nonFreshReset_firstOpen_noResidue();
    void loadFromMarkdown_calledTwice_replacesNotAppends();
    void reset_clearsFrontmatterFromPrior();
    void reset_clearsFootnotesFromPrior();
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

void TstD2ResetContent::nonFreshReset_replacePlain_noResidue()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("original content\n");
    doc.resetContent("modified content\n", Origin::TestFixture);
    QCOMPARE(doc.serializeForSave(), QByteArray("modified content\n"));
}

void TstD2ResetContent::nonFreshReset_replaceHeader_noResidue()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("# Note 1\n");
    doc.resetContent("# Modified Note 1\n", Origin::TestFixture);
    QCOMPARE(doc.serializeForSave(), QByteArray("# Modified Note 1\n"));
}

void TstD2ResetContent::nonFreshReset_replaceUnicode_noResidue()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown(QString::fromUtf8("日本語 café 🎉 résumé\n").toUtf8());
    doc.resetContent(
        QString::fromUtf8("日本語 café 🎉 résumé\n\nMore text\n").toUtf8(),
        Origin::TestFixture);
    QCOMPARE(doc.serializeForSave(),
             QString::fromUtf8("日本語 café 🎉 résumé\n\nMore text\n").toUtf8());
}

void TstD2ResetContent::nonFreshReset_externalReloadClean_noResidue()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("original content\n");
    doc.resetContent("modified content\n", Origin::ExternalReloadClean);
    QCOMPARE(doc.serializeForSave(), QByteArray("modified content\n"));
}

void TstD2ResetContent::nonFreshReset_externalReloadResolved_noResidue()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("original content\n");
    doc.resetContent("modified content\n", Origin::ExternalReloadResolved);
    QCOMPARE(doc.serializeForSave(), QByteArray("modified content\n"));
}

void TstD2ResetContent::nonFreshReset_userRevertToSaved_noResidue()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("original content\n");
    doc.resetContent("modified content\n", Origin::UserRevertToSaved);
    QCOMPARE(doc.serializeForSave(), QByteArray("modified content\n"));
}

void TstD2ResetContent::nonFreshReset_firstOpen_noResidue()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("original content\n");
    doc.resetContent("modified content\n", Origin::FirstOpen);
    QCOMPARE(doc.serializeForSave(), QByteArray("modified content\n"));
}

void TstD2ResetContent::loadFromMarkdown_calledTwice_replacesNotAppends()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("first\n");
    doc.loadFromMarkdown("second\n");
    QCOMPARE(doc.serializeForSave(), QByteArray("second\n"));
    QCOMPARE(doc.iterateBlocks().size(), size_t{1});
}

void TstD2ResetContent::reset_clearsFrontmatterFromPrior()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("---\ntitle: A\n---\n\nBody A\n");
    doc.resetContent("Body B with no frontmatter\n",
                     Origin::ExternalReloadClean);
    QVERIFY(!doc.frontmatterValue("raw").has_value());
    QCOMPARE(doc.serializeForSave(),
             QByteArray("Body B with no frontmatter\n"));
}

void TstD2ResetContent::reset_clearsFootnotesFromPrior()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("Text[^1]\n\n[^1]: footnote A\n");
    doc.resetContent("Plain text\n", Origin::ExternalReloadClean);
    // Stale footnote def must not survive into the serialized output.
    QCOMPARE(doc.serializeForSave(), QByteArray("Plain text\n"));
}

QTEST_GUILESS_MAIN(TstD2ResetContent)
#include "tst_d2_reset_content.moc"
