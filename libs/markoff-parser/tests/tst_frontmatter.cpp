// SPDX-License-Identifier: GPL-3.0-or-later
#include <QObject>
#include <QTest>
#include <QFile>
#include <QDir>
#include <markoff-parser/Document.h>
#include <markoff-parser/YamlValue.h>

using namespace Markoff;

class TestFrontmatter : public QObject {
    Q_OBJECT
private slots:
    void standardFrontmatter();
    void listStyleTags();
    void commaStyleTags();
    void emptyFrontmatter();
    void invalidYaml();
    void booleanValues();
    void numericValues();
    void noFrontmatter();

    // New tests for ryml port
    void yaml12BooleanStrictness();
    void nestedMapAndList();
    void frontmatterSpan();
    void frontmatterEofClose();
    void withFrontmatter();
    void withFrontmatterStrip();
    void withFrontmatterPrepend();
    void yamlValueMutation();
    void yamlValueStringify();
    void frontmatterParseErrorDiagnostic();
    void unicodeKeysAndValues();
    void roundTrip();

    // Legacy compatibility
    void legacyApi();
};

void TestFrontmatter::standardFrontmatter()
{
    auto doc = Document::fromMarkdown(QStringLiteral(
        "---\ntitle: My Note\ntags:\n  - foo\n  - bar\naliases:\n  - mn\n---\nBody"));
    auto fm = doc->parsedFrontmatter();
    QVERIFY(fm.isMap());
    QCOMPARE(fm.size(), 3);

    QCOMPARE(fm.get(QStringLiteral("title")).asString(), QStringLiteral("My Note"));

    auto tags = fm.get(QStringLiteral("tags"));
    QVERIFY(tags.isSeq());
    QCOMPARE(tags.asStringList(), QStringList({QStringLiteral("foo"), QStringLiteral("bar")}));

    auto aliases = fm.get(QStringLiteral("aliases"));
    QVERIFY(aliases.isSeq());
    QCOMPARE(aliases.asStringList(), QStringList({QStringLiteral("mn")}));

    // Key order preserved
    QStringList keys = fm.keys();
    QCOMPARE(keys, QStringList({QStringLiteral("title"), QStringLiteral("tags"), QStringLiteral("aliases")}));
}

void TestFrontmatter::listStyleTags()
{
    auto doc = Document::fromMarkdown(QStringLiteral(
        "---\ntags: [alpha, beta]\n---\nBody"));
    auto fm = doc->parsedFrontmatter();
    auto tags = fm.get(QStringLiteral("tags"));
    QVERIFY(tags.isSeq());
    QCOMPARE(tags.asStringList(), QStringList({QStringLiteral("alpha"), QStringLiteral("beta")}));
}

void TestFrontmatter::commaStyleTags()
{
    // "tags: alpha, beta" is a plain scalar in YAML — Obsidian treats it as
    // comma-separated. The new API preserves raw YAML semantics (it's a string).
    // Comma-splitting is the caller's responsibility (libs/core normalizeListValue).
    auto doc = Document::fromMarkdown(QStringLiteral(
        "---\ntags: alpha, beta\n---\nBody"));
    auto fm = doc->parsedFrontmatter();
    auto tags = fm.get(QStringLiteral("tags"));
    QVERIFY(tags.isString());
    QCOMPARE(tags.asString(), QStringLiteral("alpha, beta"));
}

void TestFrontmatter::emptyFrontmatter()
{
    auto doc = Document::fromMarkdown(QStringLiteral("---\n---\nBody"));
    auto fm = doc->parsedFrontmatter();
    // Empty frontmatter — raw is empty, parsed returns null
    QVERIFY(fm.isNull());
}

void TestFrontmatter::invalidYaml()
{
    auto doc = Document::fromMarkdown(QStringLiteral(
        "---\n: invalid: yaml: [[\n---\nBody"));
    auto fm = doc->parsedFrontmatter();
    QVERIFY(fm.isNull()); // graceful empty, no crash
    QVERIFY(!doc->frontmatterParseError().isEmpty());
}

void TestFrontmatter::booleanValues()
{
    auto doc = Document::fromMarkdown(QStringLiteral(
        "---\npublish: true\ndraft: false\n---\nBody"));
    auto fm = doc->parsedFrontmatter();
    QVERIFY(fm.isMap());
    QCOMPARE(fm.size(), 2);
    QVERIFY(fm.get(QStringLiteral("publish")).isBool());
    QCOMPARE(fm.get(QStringLiteral("publish")).asBool(), true);
    QVERIFY(fm.get(QStringLiteral("draft")).isBool());
    QCOMPARE(fm.get(QStringLiteral("draft")).asBool(), false);
}

void TestFrontmatter::numericValues()
{
    auto doc = Document::fromMarkdown(QStringLiteral(
        "---\nweight: 42\nrating: 3.5\n---\nBody"));
    auto fm = doc->parsedFrontmatter();
    QVERIFY(fm.isMap());
    QCOMPARE(fm.size(), 2);
    QVERIFY(fm.get(QStringLiteral("weight")).isInt());
    QCOMPARE(fm.get(QStringLiteral("weight")).asInt(), int64_t(42));
    QVERIFY(fm.get(QStringLiteral("rating")).isDouble());
    QCOMPARE(fm.get(QStringLiteral("rating")).asDouble(), 3.5);
}

void TestFrontmatter::noFrontmatter()
{
    auto doc = Document::fromMarkdown(QStringLiteral("Just body text"));
    auto fm = doc->parsedFrontmatter();
    QVERIFY(fm.isNull());
    QVERIFY(!doc->frontmatterSpan().has_value());
}

// ---------------------------------------------------------------------------
// New tests for ryml port
// ---------------------------------------------------------------------------

void TestFrontmatter::yaml12BooleanStrictness()
{
    // YAML 1.1 booleans (yes/no/on/off/y/n) must remain strings in YAML 1.2
    auto doc = Document::fromMarkdown(QStringLiteral(
        "---\nactive: yes\ndisabled: no\ntoggle: on\nswitch: off\nflag: y\nother: n\n---\nBody"));
    auto fm = doc->parsedFrontmatter();
    QVERIFY(fm.isMap());

    // All must be String, not Bool
    QVERIFY(fm.get(QStringLiteral("active")).isString());
    QCOMPARE(fm.get(QStringLiteral("active")).asString(), QStringLiteral("yes"));
    QVERIFY(fm.get(QStringLiteral("disabled")).isString());
    QCOMPARE(fm.get(QStringLiteral("disabled")).asString(), QStringLiteral("no"));
    QVERIFY(fm.get(QStringLiteral("toggle")).isString());
    QVERIFY(fm.get(QStringLiteral("switch")).isString());
    QVERIFY(fm.get(QStringLiteral("flag")).isString());
    QVERIFY(fm.get(QStringLiteral("other")).isString());
}

void TestFrontmatter::nestedMapAndList()
{
    auto doc = Document::fromMarkdown(QStringLiteral(
        "---\nmeta:\n  author: Alice\n  version: 2\nitems:\n  - name: a\n  - name: b\n---\nBody"));
    auto fm = doc->parsedFrontmatter();
    QVERIFY(fm.isMap());

    auto meta = fm.get(QStringLiteral("meta"));
    QVERIFY(meta.isMap());
    QCOMPARE(meta.get(QStringLiteral("author")).asString(), QStringLiteral("Alice"));
    QCOMPARE(meta.get(QStringLiteral("version")).asInt(), int64_t(2));

    auto items = fm.get(QStringLiteral("items"));
    QVERIFY(items.isSeq());
    QCOMPARE(items.size(), 2);
    QVERIFY(items.at(0).isMap());
    QCOMPARE(items.at(0).get(QStringLiteral("name")).asString(), QStringLiteral("a"));
    QCOMPARE(items.at(1).get(QStringLiteral("name")).asString(), QStringLiteral("b"));
}

void TestFrontmatter::frontmatterSpan()
{
    auto doc = Document::fromMarkdown(QStringLiteral(
        "---\ntitle: Test\n---\nBody text"));
    auto span = doc->frontmatterSpan();
    QVERIFY(span.has_value());
    QCOMPARE(span->first, 0);
    // "---\ntitle: Test\n---\n" = 20 chars
    QCOMPARE(span->second, 20);
    QVERIFY(!doc->frontmatterHasEofClose());
}

void TestFrontmatter::frontmatterEofClose()
{
    // Frontmatter closing at EOF without trailing newline
    QString src = QStringLiteral("---\ntitle: Test\n---");
    auto doc = Document::fromMarkdown(src);
    auto span = doc->frontmatterSpan();
    QVERIFY(span.has_value());
    QVERIFY(doc->frontmatterHasEofClose());
    QCOMPARE(span->second, src.size());
}

void TestFrontmatter::withFrontmatter()
{
    auto doc = Document::fromMarkdown(QStringLiteral(
        "---\ntitle: Old\n---\nBody text"));
    auto fm = doc->parsedFrontmatter().clone();
    fm.setString(QStringLiteral("title"), QStringLiteral("New"));

    QString result = doc->withFrontmatter(fm);
    QVERIFY(result.contains(QStringLiteral("title: New")));
    QVERIFY(result.contains(QStringLiteral("Body text")));
    QVERIFY(result.startsWith(QStringLiteral("---\n")));
}

void TestFrontmatter::withFrontmatterStrip()
{
    auto doc = Document::fromMarkdown(QStringLiteral(
        "---\ntitle: Old\n---\nBody text"));
    QString result = doc->withFrontmatter(YamlValue());
    QCOMPARE(result, QStringLiteral("Body text"));
}

void TestFrontmatter::withFrontmatterPrepend()
{
    auto doc = Document::fromMarkdown(QStringLiteral("Just body"));
    auto fm = YamlValue::emptyMap();
    fm.setString(QStringLiteral("title"), QStringLiteral("New"));
    QString result = doc->withFrontmatter(fm);
    QVERIFY(result.startsWith(QStringLiteral("---\n")));
    QVERIFY(result.contains(QStringLiteral("title: New")));
    QVERIFY(result.contains(QStringLiteral("Just body")));
}

void TestFrontmatter::yamlValueMutation()
{
    auto fm = YamlValue::emptyMap();
    fm.setString(QStringLiteral("title"), QStringLiteral("Hello"));
    fm.setInt(QStringLiteral("weight"), 42);
    fm.setBool(QStringLiteral("draft"), true);
    fm.setSeq(QStringLiteral("tags"), {QStringLiteral("a"), QStringLiteral("b")});

    QCOMPARE(fm.get(QStringLiteral("title")).asString(), QStringLiteral("Hello"));
    QCOMPARE(fm.get(QStringLiteral("weight")).asString(), QStringLiteral("42"));
    QCOMPARE(fm.get(QStringLiteral("draft")).asString(), QStringLiteral("true"));
    QCOMPARE(fm.get(QStringLiteral("tags")).size(), 2);

    fm.remove(QStringLiteral("draft"));
    QVERIFY(!fm.contains(QStringLiteral("draft")));
}

void TestFrontmatter::yamlValueStringify()
{
    auto fm = YamlValue::emptyMap();
    fm.setString(QStringLiteral("title"), QStringLiteral("Test"));
    fm.setSeq(QStringLiteral("tags"), {QStringLiteral("x"), QStringLiteral("y")});

    QString yaml = fm.stringify();
    QVERIFY(!yaml.isEmpty());
    QVERIFY(yaml.contains(QStringLiteral("title")));
    QVERIFY(yaml.contains(QStringLiteral("Test")));
    QVERIFY(yaml.contains(QStringLiteral("tags")));
}

void TestFrontmatter::frontmatterParseErrorDiagnostic()
{
    auto doc = Document::fromMarkdown(QStringLiteral(
        "---\n: bad: yaml: [[\n---\nBody"));
    auto fm = doc->parsedFrontmatter();
    QVERIFY(fm.isNull());
    QString err = doc->frontmatterParseError();
    QVERIFY(!err.isEmpty());
}

void TestFrontmatter::unicodeKeysAndValues()
{
    auto doc = Document::fromMarkdown(QString::fromUtf8(
        "---\n\xF0\x9F\x8E\xB5: music\nauthor: \xD8\xA3\xD8\xAD\xD9\x85\xD8\xAF\n---\nBody"));
    auto fm = doc->parsedFrontmatter();
    QVERIFY(fm.isMap());
    // Emoji key
    QVERIFY(fm.contains(QString::fromUtf8("\xF0\x9F\x8E\xB5")));
    QCOMPARE(fm.get(QString::fromUtf8("\xF0\x9F\x8E\xB5")).asString(), QStringLiteral("music"));
    // RTL value
    QCOMPARE(fm.get(QStringLiteral("author")).asString(), QString::fromUtf8("\xD8\xA3\xD8\xAD\xD9\x85\xD8\xAF"));
}

void TestFrontmatter::roundTrip()
{
    // Parse then withFrontmatter(parsedFrontmatter()) must preserve content
    QString src = QStringLiteral("---\ntitle: Round Trip\ntags:\n  - a\n  - b\nweight: 42\n---\nBody text here.\n");
    auto doc = Document::fromMarkdown(src);
    auto fm = doc->parsedFrontmatter();
    QString result = doc->withFrontmatter(fm);

    // The body must survive
    QVERIFY(result.contains(QStringLiteral("Body text here.")));
    // Frontmatter keys must survive
    QVERIFY(result.contains(QStringLiteral("title: Round Trip")));
    QVERIFY(result.contains(QStringLiteral("weight: 42")));

    // Re-parse the result and verify structural equality
    auto doc2 = Document::fromMarkdown(result);
    auto fm2 = doc2->parsedFrontmatter();
    QVERIFY(fm2.isMap());
    QCOMPARE(fm2.get(QStringLiteral("title")).asString(), QStringLiteral("Round Trip"));
    QCOMPARE(fm2.get(QStringLiteral("weight")).asInt(), int64_t(42));
    QCOMPARE(fm2.get(QStringLiteral("tags")).asStringList(),
             QStringList({QStringLiteral("a"), QStringLiteral("b")}));
}

void TestFrontmatter::legacyApi()
{
    auto doc = Document::fromMarkdown(QStringLiteral(
        "---\ntitle: My Note\ntags:\n  - foo\n  - bar\n---\nBody"));
    auto props = doc->parsedFrontmatterLegacy();
    QCOMPARE(props.size(), 2);
    QCOMPARE(props[0].key, QStringLiteral("title"));
    QCOMPARE(props[0].value.toString(), QStringLiteral("My Note"));
    QCOMPARE(props[1].key, QStringLiteral("tags"));
    QCOMPARE(props[1].value.toStringList(), QStringList({QStringLiteral("foo"), QStringLiteral("bar")}));
}

QTEST_APPLESS_MAIN(TestFrontmatter)
#include "tst_frontmatter.moc"
