// SPDX-License-Identifier: GPL-3.0-or-later
//
// Phase 3a pipeline test: verifies that ReadingPipeline splits a markdown
// fixture into the expected section count and boundary offsets.

#include "corbomite/readingview/ReadingPipeline.h"
#include "corbomite/readingview/ReadingSection.h"

#include <QTest>

using namespace Corbomite::ReadingView;

class TestReadingPipelineSectionSplit : public QObject
{
    Q_OBJECT

private slots:
    void splitsThreeH1Sections();
    void splitsH1WithNestedH2();
    void identifiesFrontmatter();
    void noHeadingsSingleSection();
    void emptyInput();
    void skipsHeadingsInsideFencedCodeBlock();
    void usesFrontMatterDetected();
};

void TestReadingPipelineSectionSplit::splitsThreeH1Sections()
{
    ReadingPipeline pipeline;
    const QString md = QStringLiteral(
        "# A\nalpha\n\n# B\nbeta\n\n# C\ngamma\n");
    const auto sections = pipeline.splitIntoSections(md);
    QCOMPARE(sections.size(), 3);
    QCOMPARE(sections.at(0)->headingLevel(), 1);
    QCOMPARE(sections.at(1)->headingLevel(), 1);
    QCOMPARE(sections.at(2)->headingLevel(), 1);
    QVERIFY(sections.at(0)->sourceRange().from < sections.at(1)->sourceRange().from);
    QVERIFY(sections.at(1)->sourceRange().from < sections.at(2)->sourceRange().from);
    QCOMPARE(sections.at(2)->sourceRange().to, md.size());
}

void TestReadingPipelineSectionSplit::splitsH1WithNestedH2()
{
    ReadingPipeline pipeline;
    const QString md = QStringLiteral(
        "# Outer\n\n## Inner A\nfoo\n\n## Inner B\nbar\n\n# Next\nend\n");
    const auto sections = pipeline.splitIntoSections(md);
    // # Outer (level 1, spans until next # Next), ## Inner A (level 2,
    // spans until ## Inner B), ## Inner B (level 2), # Next (level 1).
    QCOMPARE(sections.size(), 4);
    QCOMPARE(sections.at(0)->headingLevel(), 1);
    QCOMPARE(sections.at(1)->headingLevel(), 2);
    QCOMPARE(sections.at(2)->headingLevel(), 2);
    QCOMPARE(sections.at(3)->headingLevel(), 1);
}

void TestReadingPipelineSectionSplit::identifiesFrontmatter()
{
    ReadingPipeline pipeline;
    const QString md = QStringLiteral(
        "---\ntitle: Hi\n---\n# Body\nline\n");
    const auto sections = pipeline.splitIntoSections(md);
    QVERIFY(sections.size() >= 2);
    QVERIFY(sections.at(0)->isFrontMatterSection());
    QCOMPARE(sections.at(0)->headingLevel(), 0);
    QVERIFY(!sections.at(0)->usesFrontMatter());
    // The next section must be the H1 body.
    QCOMPARE(sections.at(1)->headingLevel(), 1);
}

void TestReadingPipelineSectionSplit::noHeadingsSingleSection()
{
    ReadingPipeline pipeline;
    const QString md = QStringLiteral("just a paragraph\nwith two lines\n");
    const auto sections = pipeline.splitIntoSections(md);
    QCOMPARE(sections.size(), 1);
    QCOMPARE(sections.at(0)->headingLevel(), 0);
    QCOMPARE(sections.at(0)->sourceRange().from, 0);
    QCOMPARE(sections.at(0)->sourceRange().to, md.size());
}

void TestReadingPipelineSectionSplit::emptyInput()
{
    ReadingPipeline pipeline;
    const auto sections = pipeline.splitIntoSections(QString());
    QCOMPARE(sections.size(), 0);
}

void TestReadingPipelineSectionSplit::skipsHeadingsInsideFencedCodeBlock()
{
    ReadingPipeline pipeline;
    const QString md = QStringLiteral(
        "# Real\nfoo\n\n```\n# not a heading\n```\n\n# Also real\n");
    const auto sections = pipeline.splitIntoSections(md);
    QCOMPARE(sections.size(), 2);
    QCOMPARE(sections.at(0)->headingLevel(), 1);
    QCOMPARE(sections.at(1)->headingLevel(), 1);
}

void TestReadingPipelineSectionSplit::usesFrontMatterDetected()
{
    ReadingPipeline pipeline;
    const QString md = QStringLiteral(
        "---\ntitle: Hi\n---\n"
        "# Intro\n\nHello {{title}} world.\n\n"
        "# Plain\n\nNo tokens here.\n");
    const auto sections = pipeline.splitIntoSections(md);
    QVERIFY(sections.size() >= 3);

    // section[0] is frontmatter (source — stays false).
    QVERIFY(sections.at(0)->isFrontMatterSection());
    QVERIFY(!sections.at(0)->usesFrontMatter());

    // section[1] is "# Intro" — contains {{title}}.
    QCOMPARE(sections.at(1)->headingLevel(), 1);
    QVERIFY(sections.at(1)->usesFrontMatter());

    // section[2] is "# Plain" — no template token.
    QCOMPARE(sections.at(2)->headingLevel(), 1);
    QVERIFY(!sections.at(2)->usesFrontMatter());
}

QTEST_MAIN(TestReadingPipelineSectionSplit)
#include "tst_readingpipeline_section_split.moc"
