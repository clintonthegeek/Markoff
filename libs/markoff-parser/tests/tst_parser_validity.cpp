// SPDX-License-Identifier: GPL-3.0-or-later
//
// Contract tests for parser state validity.
//
//  - Document::parsedFrontmatter() is lazy and thread-safe: concurrent readers
//    must all see the parsed value (the old flag-then-fill order could hand a
//    second reader a default-constructed YamlValue).
//  - TreeSitterParser reports validity: before a parse, and after a failed one,
//    hasTree() is false and every accessor returns empty rather than reading a
//    tree against a byte map from a different document.

#include <QObject>
#include <QTest>

#include <barrier>
#include <array>
#include <thread>
#include <vector>

#include <markoff/parser/Document.h>
#include <markoff/parser/TreeSitterParser.h>

using namespace Markoff;

class TestParserValidity : public QObject {
    Q_OBJECT
private slots:
    void concurrentFrontmatterReadsSeeTheParsedValue();
    void parserAccessorsAreEmptyBeforeAnyParse();
    void staleInlineSpansAreNeverObservable();
    void reparsingReplacesTreesWholesale();
};

void TestParserValidity::concurrentFrontmatterReadsSeeTheParsedValue()
{
    auto doc = Document::fromMarkdown(QStringLiteral(
        "---\ntitle: My Note\ncount: 3\ntags:\n  - foo\n---\nBody"));

    constexpr int readerCount = 8;
    std::barrier start(readerCount);
    // std::array<bool, N>, not vector<bool>: the bit-packed specialization puts
    // several readers' flags in one byte, which is itself a data race.
    std::array<bool, readerCount> sawParsedMap{};

    std::vector<std::thread> readers;
    readers.reserve(readerCount);
    for (int i = 0; i < readerCount; ++i) {
        readers.emplace_back([&, i] {
            start.arrive_and_wait();
            const YamlValue fm = doc->parsedFrontmatter();
            sawParsedMap[i] = fm.isMap()
                && fm.get(QStringLiteral("title")).asString() == QStringLiteral("My Note")
                && fm.size() == 3;
        });
    }
    for (auto &t : readers)
        t.join();

    for (int i = 0; i < readerCount; ++i)
        QVERIFY(sawParsedMap[i]);

    // The cached value keeps answering after the first parse.
    QCOMPARE(doc->parsedFrontmatter().get(QStringLiteral("count")).asString(),
             QStringLiteral("3"));
    QVERIFY(doc->frontmatterParseError().isEmpty());
}

void TestParserValidity::parserAccessorsAreEmptyBeforeAnyParse()
{
    TreeSitterParser parser;
    QVERIFY(!parser.hasTree());
    QVERIFY(parser.buildSpanMap().isEmpty());
    QVERIFY(parser.findBlockBoundaries().isEmpty());
    const DocumentQueryResult queries = parser.buildDocumentQueries();
    QVERIFY(queries.headings.isEmpty());
    QVERIFY(queries.links.isEmpty());
    QVERIFY(queries.tags.isEmpty());
    QVERIFY(queries.topLevelBlocks.isEmpty());
}

// A null block tree cannot be forced through the public API (tree-sitter
// returns null only on cancellation, timeout or allocation failure), so the
// failure path is pinned structurally: parse() drops every tree before it
// touches the byte map, and every accessor gates on validity. What is
// observable is that a span can never point outside the current source — the
// symptom the mixed-state bug produced (old trees measured against a new map).
void TestParserValidity::staleInlineSpansAreNeverObservable()
{
    TreeSitterParser parser;
    QVERIFY(parser.parse(QStringLiteral(
        "# Heading with **bold** and [link](https://example.com)\n\n"
        "> quoted *italic* text\n\n"
        "`code` and **more** spans here")));
    QVERIFY(!parser.buildSpanMap().isEmpty());

    // Re-parse a much shorter document. Any span retained from the first parse
    // would now run past the end of utf8Source().
    const QByteArray short2 = QByteArray("*i*");
    QVERIFY(parser.parse(QString::fromUtf8(short2)));
    QCOMPARE(parser.utf8Source(), short2);
    for (const SourceSpan &span : parser.buildSpanMap()) {
        QVERIFY(span.utf8Offset >= 0);
        QVERIFY(span.utf8Offset + span.utf8Length <= short2.size());
    }
}

void TestParserValidity::reparsingReplacesTreesWholesale()
{
    TreeSitterParser parser;
    QVERIFY(parser.parse(QStringLiteral("# First\n\nplain **bold** text")));
    QVERIFY(parser.hasTree());
    QVERIFY(!parser.buildSpanMap().isEmpty());

    QVERIFY(parser.parse(QStringLiteral("no markup here")));
    QVERIFY(parser.hasTree());
    // Spans from the first document must not survive into the second.
    QCOMPARE(parser.utf8Source(), QByteArray("no markup here"));
}

QTEST_MAIN(TestParserValidity)
#include "tst_parser_validity.moc"
