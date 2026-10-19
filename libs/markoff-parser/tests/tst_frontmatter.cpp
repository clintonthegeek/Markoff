// SPDX-License-Identifier: GPL-3.0-or-later
#include <QObject>
#include <QTest>
#include <QFile>
#include <QDir>
#include <markoff/parser/Document.h>
#include <markoff/parser/YamlValue.h>

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
    void closingFenceMustBeItsOwnLine();
    void dashLinesAfterFrontmatterSurvive();
    void emptyBlockHasNoRawContent();

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

    // setChildFrom tests
    void testSetChildFromCopiesNestedMapVerbatim();
    void testSetChildFromReplacesExistingKey();
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
    // The structured API preserves YAML scalar semantics; callers choose any
    // application-specific interpretation.
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

// A closing fence is exactly `---` on its own line. The old prefix search
// accepted `----`, `--- ` and `---foo` and then ate three of those dashes as if
// they were the fence, truncating the body on the next round-trip.
void TestFrontmatter::closingFenceMustBeItsOwnLine()
{
    const QStringList nearFences{
        QStringLiteral("----"),
        QStringLiteral("--- "),
        QStringLiteral("---foo"),
    };
    for (const QString &fence : nearFences) {
        const QString src = QStringLiteral("---\ntitle: Note\n") + fence
            + QStringLiteral("\nBody text");
        auto doc = Document::fromMarkdown(src);
        QVERIFY2(!doc->frontmatterSpan().has_value(), qPrintable(fence));
        QVERIFY2(doc->frontmatterRaw().isEmpty(), qPrintable(fence));
        QCOMPARE(doc->markdownContent(), src);
        // Nothing was recognised, so stripping the (absent) frontmatter is a
        // byte-for-byte no-op — the source survives untouched.
        QCOMPARE(doc->withFrontmatter(YamlValue()), src);
    }
}

// The body may legitimately contain lines that start with dashes; only the
// first complete fence closes the block, and the rest of the body must survive
// in full.
void TestFrontmatter::dashLinesAfterFrontmatterSurvive()
{
    auto doc = Document::fromMarkdown(QStringLiteral(
        "---\ntitle: A\n---\nBody\n----\n---\nMore"));
    QCOMPARE(doc->frontmatterRaw(), QStringLiteral("title: A"));
    QCOMPARE(doc->markdownContent(), QStringLiteral("Body\n----\n---\nMore"));
}

// A closing fence on the line right after the opening one has no content at
// all; QString::mid with a negative length used to return the rest of the file.
void TestFrontmatter::emptyBlockHasNoRawContent()
{
    auto doc = Document::fromMarkdown(QStringLiteral("---\n---\nBody"));
    QVERIFY(doc->frontmatterRaw().isEmpty());
    QCOMPARE(doc->markdownContent(), QStringLiteral("Body"));
    QCOMPARE(doc->withFrontmatter(YamlValue()), QStringLiteral("Body"));
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

void TestFrontmatter::testSetChildFromCopiesNestedMapVerbatim()
{
    using namespace Markoff;
    QString err;
    YamlValue src = YamlValue::parse(QStringLiteral(
        "outer:\n  inner: value\n  count: 3\n  nested:\n    deep: yes\n"), &err);
    QVERIFY(err.isEmpty());

    YamlValue dst = YamlValue::emptyMap();
    dst.setString(QStringLiteral("first"), QStringLiteral("a"));
    dst.setChildFrom(QStringLiteral("outer"), src.get(QStringLiteral("outer")));

    // Verbatim: the copied subtree stringifies identically to the source's.
    QCOMPARE(dst.get(QStringLiteral("outer")).stringify(),
             src.get(QStringLiteral("outer")).stringify());
    // Appended last → order is first, then outer.
    QCOMPARE(dst.keys(), QStringList({QStringLiteral("first"), QStringLiteral("outer")}));
}

void TestFrontmatter::testSetChildFromReplacesExistingKey()
{
    using namespace Markoff;
    YamlValue dst = YamlValue::emptyMap();
    dst.setString(QStringLiteral("k"), QStringLiteral("old"));
    YamlValue src = YamlValue::parse(QStringLiteral("k:\n  - x\n  - y\n"));
    dst.setChildFrom(QStringLiteral("k"), src.get(QStringLiteral("k")));
    QCOMPARE(dst.keys(), QStringList({QStringLiteral("k")}));
    QVERIFY(dst.get(QStringLiteral("k")).isSeq());
    QCOMPARE(dst.get(QStringLiteral("k")).size(), 2);
}

QTEST_APPLESS_MAIN(TestFrontmatter)
#include "tst_frontmatter.moc"
