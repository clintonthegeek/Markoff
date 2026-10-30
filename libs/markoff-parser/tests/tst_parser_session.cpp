// SPDX-License-Identifier: GPL-3.0-or-later
//
// Contract tests for the persistent ParserSession (ASK-M31, second half).
//
// A session's snapshot must be indistinguishable from a one-shot
// fromMarkdown() of the same text — through the first parse AND through
// the incremental re-parse path (a small contiguous edit carried by the
// session's retained tree) — and the tree/byte-map must move together:
// no accessor can read a previous document's tree against this call's
// byte map.

#include <QObject>
#include <QTest>

#include <markoff/parser/Document.h>
#include <markoff/parser/ParserSession.h>

using namespace Markoff;

namespace {

// Deep structural equality of the query snapshots a Document carries.
void assertSameQueries(const Document &a, const Document &b)
{
    const auto qa = a.topLevelBlocks();
    const auto qb = b.topLevelBlocks();
    QCOMPARE(qa.size(), qb.size());
    for (int i = 0; i < qa.size(); ++i) {
        QCOMPARE(int(qa[i].kind), int(qb[i].kind));
        QCOMPARE(qa[i].byteStart, qb[i].byteStart);
        QCOMPARE(qa[i].byteEnd, qb[i].byteEnd);
        QCOMPARE(int(qa[i].inlineSpans.size()), int(qb[i].inlineSpans.size()));
        for (int s = 0; s < qa[i].inlineSpans.size(); ++s)
            QVERIFY(qa[i].inlineSpans[s] == qb[i].inlineSpans[s]);
    }
    QCOMPARE(a.headings().size(), b.headings().size());
    QCOMPARE(a.links().size(), b.links().size());
    QCOMPARE(a.wikiLinks().size(), b.wikiLinks().size());
    QCOMPARE(a.tags().size(), b.tags().size());
    QCOMPARE(a.footnotes().size(), b.footnotes().size());
    QCOMPARE(a.footnoteRefs().size(), b.footnoteRefs().size());
}

QString paragraph(int n)
{
    return QStringLiteral(
        "paragraph %1 with *emphasis*, **bold**, `code`, a "
        "[link](https://x.y/1), a [[wikilink]], $x^2$, and #tag%1\n").arg(n);
}

// A vault-like document: frontmatter + repeated varied sections.
QString makeVault(int sections)
{
    QString v = QStringLiteral(
        "---\ntitle: Vault\ntags:\n  - a\n  - b\n---\n\n"
        "# Root heading\n\n");
    for (int i = 0; i < sections; ++i)
        v += QStringLiteral("## Section %1\n\n%2\n").arg(i).arg(paragraph(i));
    return v;
}

} // namespace

class TestParserSession : public QObject {
    Q_OBJECT
private slots:
    void first_parse_matches_one_shot();
    void edited_reparse_matches_one_shot();
    void multibyte_edit_reparse_matches_one_shot();
    void frontmatter_only_change_never_touches_the_parser();
    void repeated_identical_source_is_stable();
    void empty_to_nonempty_and_back();
    void mid_document_edits_across_all_sections();
};

void TestParserSession::first_parse_matches_one_shot()
{
    const QString vault = makeVault(30);
    ParserSession session;
    auto fromSession = session.parse(vault);
    auto oneShot = Document::fromMarkdown(vault);
    QVERIFY(fromSession);
    assertSameQueries(*fromSession, *oneShot);
    QVERIFY(session.lastSource() == vault);
}

void TestParserSession::edited_reparse_matches_one_shot()
{
    QString vault = makeVault(30);
    ParserSession session;
    auto first = session.parse(vault);
    QVERIFY(first);

    // One small contiguous edit: widen a word in section 20.
    const int at = vault.indexOf(QLatin1String("paragraph 20"));
    QVERIFY(at > 0);
    vault.replace(at, 12, QLatin1String("paragraph 20 REDACTED"));

    auto second = session.parse(vault);
    auto fresh = Document::fromMarkdown(vault);
    assertSameQueries(*second, *fresh);
    // And the FIRST document is untouched (value snapshots).
    assertSameQueries(*first, *Document::fromMarkdown(makeVault(30)));
}

void TestParserSession::multibyte_edit_reparse_matches_one_shot()
{
    // Multi-byte content everywhere: an edit picked by the byte-diff must
    // stay scalar-aligned, and the re-parse must equal a fresh parse.
    QString doc = QStringLiteral(
        "# Züge 🚆\n\nnaïve résumé — あいうえお ×%1,\n");
    for (int i = 0; i < 20; ++i) {
        doc += QStringLiteral(
            "## §%1 in Ω\n\nphrase %1 with **élégance** → ⩽ and 🚀\n").arg(i);
    }
    ParserSession session;
    QVERIFY(session.parse(doc));

    doc.replace(QLatin1String("phrase 7 with"),
                QLatin1String("phrase 7 (edited) with"));
    auto fromSession = session.parse(doc);
    auto fresh = Document::fromMarkdown(doc);
    assertSameQueries(*fromSession, *fresh);
}

void TestParserSession::frontmatter_only_change_never_touches_the_parser()
{
    QString edited = makeVault(10);
    ParserSession session;
    auto first = session.parse(edited);
    QVERIFY(first);

    edited.replace(QLatin1String("title: Vault"),
                   QLatin1String("title: Vault v2"));
    auto second = session.parse(edited);
    auto fresh = Document::fromMarkdown(edited);
    assertSameQueries(*second, *fresh);
    QCOMPARE(second->frontmatterRaw(), fresh->frontmatterRaw());
    QCOMPARE(second->markdownContent(), fresh->markdownContent());
    QVERIFY(second->frontmatterSpan().has_value());
    QCOMPARE(second->frontmatterSpan()->first, fresh->frontmatterSpan()->first);
    QCOMPARE(second->frontmatterSpan()->second, fresh->frontmatterSpan()->second);

    // The body queries are unchanged from the first parse (the parser
    // never saw the frontmatter edit).
    assertSameQueries(*first, *second);
}

void TestParserSession::repeated_identical_source_is_stable()
{
    const QString vault = makeVault(10);
    ParserSession session;
    auto a = session.parse(vault);
    auto b = session.parse(vault);
    assertSameQueries(*a, *b);
    // QSring identity is not required; content is.
    QCOMPARE(a->markdownContent(), b->markdownContent());
}

void TestParserSession::empty_to_nonempty_and_back()
{
    ParserSession session;
    auto empty = session.parse(QString());
    QVERIFY(empty);
    QVERIFY(empty->isEmpty());

    auto one = session.parse(QStringLiteral("# Only heading\n"));
    assertSameQueries(*one, *Document::fromMarkdown(QStringLiteral("# Only heading\n")));

    // Truncating back to empty exercises the deletion side of the diff.
    auto back = session.parse(QString());
    QVERIFY(back);
    QVERIFY(back->headings().isEmpty());
}

void TestParserSession::mid_document_edits_across_all_sections()
{
    // Edits in different regions each round: the minimal-diff re-parse
    // must stay equivalent to a fresh parse for head, middle and tail.
    ParserSession session;
    QString vault = makeVault(20);
    QVERIFY(session.parse(vault));
    for (int i = 0; i < 20; i += 3) {
        const int at = vault.indexOf(QStringLiteral("paragraph %1 with").arg(i));
        QVERIFY(at > 0);
        vault.replace(at, 9, QLatin1String("phrase, edited"));
        auto fromSession = session.parse(vault);
        auto fresh = Document::fromMarkdown(vault);
        assertSameQueries(*fromSession, *fresh);
    }
}

QTEST_APPLESS_MAIN(TestParserSession)
#include "tst_parser_session.moc"
