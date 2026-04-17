// libs/markoff/tests/tst_document_queries.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff-parser/Document.h>

class TestDocumentQueries : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testHeadings();
    void testHeadingsEmpty();
    void testLinks();
    void testWikiLinks();
    void testTags();
    void testWordCount();
    void testCharacterCount();
    void testFootnotes();
};

void TestDocumentQueries::testHeadings()
{
    auto doc = Markoff::Document::fromMarkdown(QStringLiteral(
        "# First\n\nParagraph.\n\n## Second\n\n### Third\n"));
    auto headings = doc->headings();
    QCOMPARE(headings.size(), 3);
    QCOMPARE(headings[0].level, 1);
    QCOMPARE(headings[0].text, QStringLiteral("First"));
    QCOMPARE(headings[1].level, 2);
    QCOMPARE(headings[1].text, QStringLiteral("Second"));
    QCOMPARE(headings[2].level, 3);
    QCOMPARE(headings[2].text, QStringLiteral("Third"));
}

void TestDocumentQueries::testHeadingsEmpty()
{
    auto doc = Markoff::Document::fromMarkdown(QStringLiteral("Just a paragraph."));
    QVERIFY(doc->headings().isEmpty());
}

void TestDocumentQueries::testLinks()
{
    auto doc = Markoff::Document::fromMarkdown(QStringLiteral(
        "A [link](https://example.com) and [[WikiNote]] here.\n"));
    auto links = doc->links();
    QVERIFY(links.size() >= 2);
    // Should contain at least one Standard and one Wiki link
    bool hasStandard = false, hasWiki = false;
    for (const auto &l : links) {
        if (l.type == Markoff::LinkInfo::Standard) hasStandard = true;
        if (l.type == Markoff::LinkInfo::Wiki) hasWiki = true;
    }
    QVERIFY(hasStandard);
    QVERIFY(hasWiki);
}

void TestDocumentQueries::testWikiLinks()
{
    auto doc = Markoff::Document::fromMarkdown(QStringLiteral(
        "See [[Note One]] and [[Note Two|display]] and [standard](url).\n"));
    auto wikiLinks = doc->wikiLinks();
    QCOMPARE(wikiLinks.size(), 2);
    QCOMPARE(wikiLinks[0].type, Markoff::LinkInfo::Wiki);
    QCOMPARE(wikiLinks[1].type, Markoff::LinkInfo::Wiki);
}

void TestDocumentQueries::testTags()
{
    auto doc = Markoff::Document::fromMarkdown(QStringLiteral(
        "Some text with #project and #status/active tags.\n"));
    auto tags = doc->tags();
    QVERIFY(tags.size() >= 2);
    QStringList tagNames;
    for (const auto &t : tags)
        tagNames << t.name;
    QVERIFY(tagNames.contains(QStringLiteral("project")));
    QVERIFY(tagNames.contains(QStringLiteral("status/active")));
}

void TestDocumentQueries::testWordCount()
{
    auto doc = Markoff::Document::fromMarkdown(QStringLiteral("One two three four five."));
    QCOMPARE(doc->wordCount(), 5);
}

void TestDocumentQueries::testCharacterCount()
{
    auto doc = Markoff::Document::fromMarkdown(QStringLiteral("Hello"));
    QCOMPARE(doc->characterCount(), 5);
}

void TestDocumentQueries::testFootnotes()
{
    auto doc = Markoff::Document::fromMarkdown(QStringLiteral(
        "Text with a reference[^1].\n\n[^1]: This is the footnote content.\n"));
    auto footnotes = doc->footnotes();
    QCOMPARE(footnotes.size(), 1);
    QCOMPARE(footnotes[0].number, 1);
    QCOMPARE(footnotes[0].label, QStringLiteral("1"));
    QVERIFY(footnotes[0].content.contains(QStringLiteral("footnote content")));
}

QTEST_MAIN(TestDocumentQueries)
#include "tst_document_queries.moc"
