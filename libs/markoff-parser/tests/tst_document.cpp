// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff-parser/Document.h>

class TestDocument : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testEmptyDocument();
    void testNonEmptyDocument();
    void testExtractSubpathHeading();
    void testExtractSubpathBlockId();
    void testExtractSubpathNotFound();
    void extract_doesNotInsertSupHtml();
    void extract_bodyEqualsSourceWhenNoFrontmatter();
    void extract_bodyEqualsPostFrontmatterSliceWithFrontmatter();
    void extract_keepsFootnoteDefinitionLinesInBody();
    void footnoteRefs_emptyForDocWithNoFootnotes();
    void footnoteRefs_singleRefReturnsOneEntry();
    void footnoteRefs_twoLabelsTwoRefsEachInOrder();
    void footnoteRefs_definitionPrefixIsNotARef();
    void footnoteRefs_unresolvedRefHasZeroNumber();
};

void TestDocument::testEmptyDocument()
{
    auto doc = Markoff::Document::fromMarkdown(QString());
    QVERIFY(doc != nullptr);
    QVERIFY(doc->isEmpty());
}

void TestDocument::testNonEmptyDocument()
{
    auto doc = Markoff::Document::fromMarkdown(QStringLiteral("# Hello"));
    QVERIFY(doc != nullptr);
    QVERIFY(!doc->isEmpty());
    QCOMPARE(doc->sourceText(), QStringLiteral("# Hello"));
}

void TestDocument::testExtractSubpathHeading()
{
    const QString markdown = QStringLiteral(
        "# Introduction\n"
        "\n"
        "Some intro text.\n"
        "\n"
        "## Details\n"
        "\n"
        "Here are the details.\n"
        "\n"
        "More detail text.\n"
        "\n"
        "## Summary\n"
        "\n"
        "A summary.\n"
    );

    auto doc = Markoff::Document::fromMarkdown(markdown);
    QVERIFY(doc != nullptr);

    const QString result = doc->extractSubpath(QStringLiteral("#Details"));
    QVERIFY(!result.isEmpty());
    QVERIFY(result.contains(QStringLiteral("## Details")));
    QVERIFY(result.contains(QStringLiteral("Here are the details.")));
    QVERIFY(result.contains(QStringLiteral("More detail text.")));
    // Must not include the next heading's content
    QVERIFY(!result.contains(QStringLiteral("A summary.")));
}

void TestDocument::testExtractSubpathBlockId()
{
    const QString markdown = QStringLiteral(
        "First paragraph here.\n"
        "\n"
        "Second paragraph with some text and ^myblock marker here.\n"
        "\n"
        "Third paragraph after.\n"
    );

    auto doc = Markoff::Document::fromMarkdown(markdown);
    QVERIFY(doc != nullptr);

    const QString result = doc->extractSubpath(QStringLiteral("#^myblock"));
    QVERIFY(!result.isEmpty());
    QVERIFY(result.contains(QStringLiteral("Second paragraph")));
    // The block-id marker itself should be stripped
    QVERIFY(!result.contains(QStringLiteral("^myblock")));
    // Should not include unrelated paragraphs
    QVERIFY(!result.contains(QStringLiteral("First paragraph")));
    QVERIFY(!result.contains(QStringLiteral("Third paragraph")));
}

void TestDocument::testExtractSubpathNotFound()
{
    const QString markdown = QStringLiteral(
        "# Introduction\n"
        "\n"
        "Some text.\n"
    );

    auto doc = Markoff::Document::fromMarkdown(markdown);
    QVERIFY(doc != nullptr);

    const QString result = doc->extractSubpath(QStringLiteral("#NonExistentHeading"));
    QVERIFY(result.isEmpty());
}

void TestDocument::extract_doesNotInsertSupHtml()
{
    const QString src =
        QStringLiteral("Text with[^1] a reference.\n\n[^1]: defn.\n");
    const auto extracted = Markoff::Document::extract(src);
    QVERIFY2(!extracted.body.contains(QStringLiteral("<sup>")),
             qPrintable(QStringLiteral("body still contains <sup>: ")
                        + extracted.body));
}

void TestDocument::extract_bodyEqualsSourceWhenNoFrontmatter()
{
    const QString src = QStringLiteral(
        "Plain text[^a].\n\n[^a]: definition lives here.\nMore text.\n");
    const auto extracted = Markoff::Document::extract(src);
    QCOMPARE(extracted.body, src);
}

void TestDocument::extract_bodyEqualsPostFrontmatterSliceWithFrontmatter()
{
    const QString src = QStringLiteral(
        "---\nkey: value\n---\nbody[^1] line.\n\n[^1]: defn.\n");
    const auto extracted = Markoff::Document::extract(src);
    QVERIFY(extracted.frontmatterBlockEnd > 0);
    QCOMPARE(extracted.body, src.mid(extracted.frontmatterBlockEnd));
}

void TestDocument::extract_keepsFootnoteDefinitionLinesInBody()
{
    const QString src = QStringLiteral(
        "ref[^1].\n\n[^1]: This is the definition content.\n");
    const auto extracted = Markoff::Document::extract(src);
    QVERIFY2(extracted.body.contains(
                 QStringLiteral("[^1]: This is the definition content.")),
             qPrintable(QStringLiteral("definition line missing from body: ")
                        + extracted.body));
}

void TestDocument::footnoteRefs_emptyForDocWithNoFootnotes()
{
    auto doc = Markoff::Document::fromMarkdown(QStringLiteral("plain text"));
    QVERIFY(doc->footnoteRefs().isEmpty());
}

void TestDocument::footnoteRefs_singleRefReturnsOneEntry()
{
    auto doc = Markoff::Document::fromMarkdown(QStringLiteral(
        "Text[^1] more.\n\n[^1]: definition.\n"));
    const auto refs = doc->footnoteRefs();
    QCOMPARE(refs.size(), 1);
    QCOMPARE(refs[0].label, QStringLiteral("1"));
    QCOMPARE(refs[0].number, 1);
    // sourceOffset points into body (== source here, no frontmatter).
    QCOMPARE(refs[0].sourceOffset, 4);  // "Text" = 4 chars; "[^1]" follows.
}

void TestDocument::footnoteRefs_twoLabelsTwoRefsEachInOrder()
{
    auto doc = Markoff::Document::fromMarkdown(QStringLiteral(
        "see[^a], also[^b], more[^a], end[^b].\n\n[^a]: A.\n[^b]: B.\n"));
    const auto refs = doc->footnoteRefs();
    QCOMPARE(refs.size(), 4);
    QCOMPARE(refs[0].label, QStringLiteral("a"));
    QCOMPARE(refs[0].number, 1);
    QCOMPARE(refs[1].label, QStringLiteral("b"));
    QCOMPARE(refs[1].number, 2);
    QCOMPARE(refs[2].label, QStringLiteral("a"));
    QCOMPARE(refs[2].number, 1);
    QCOMPARE(refs[3].label, QStringLiteral("b"));
    QCOMPARE(refs[3].number, 2);
    // Offsets must be strictly ascending.
    for (int i = 1; i < refs.size(); ++i)
        QVERIFY(refs[i].sourceOffset > refs[i - 1].sourceOffset);
}

void TestDocument::footnoteRefs_definitionPrefixIsNotARef()
{
    auto doc = Markoff::Document::fromMarkdown(QStringLiteral(
        "see[^1].\n\n[^1]: definition body.\n"));
    const auto refs = doc->footnoteRefs();
    QCOMPARE(refs.size(), 1);  // only the inline reference, not [^1]: prefix
    QCOMPARE(refs[0].label, QStringLiteral("1"));
}

void TestDocument::footnoteRefs_unresolvedRefHasZeroNumber()
{
    auto doc = Markoff::Document::fromMarkdown(QStringLiteral(
        "see[^missing] and continue.\n"));
    const auto refs = doc->footnoteRefs();
    QCOMPARE(refs.size(), 1);
    QCOMPARE(refs[0].label, QStringLiteral("missing"));
    QCOMPARE(refs[0].number, 0);  // no definition → no number
}

QTEST_MAIN(TestDocument)
#include "tst_document.moc"
