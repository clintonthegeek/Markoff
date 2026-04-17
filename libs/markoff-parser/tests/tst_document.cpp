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

QTEST_MAIN(TestDocument)
#include "tst_document.moc"
