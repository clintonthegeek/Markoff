// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/SearchEngine.h>
#include <markoff/core/Session.h>
#include <markoff/vnext/DocumentStorageProvider.h>
#include <memory>
#include <stdexcept>

using namespace Markoff;

namespace {
struct CandidateDocument {
    std::unique_ptr<VNext::DocumentStorageProvider> provider;
    std::unique_ptr<MarkoffDocument> document;
};

CandidateDocument openCandidate(const QString &key)
{
    auto providerResult = VNext::makeEphemeralDocumentStorageProvider();
    if (!providerResult.ok())
        throw std::runtime_error("could not create ephemeral provider");
    CandidateDocument result{std::move(providerResult).value(), {}};
    auto documentResult = MarkoffDocument::open(*result.provider,
        {key, 1}, VNext::StorageMode::ephemeral);
    if (!documentResult.ok())
        throw std::runtime_error("could not open provider-backed document");
    result.document = std::move(documentResult).value();
    return result;
}
}

class TstFoundationSearchEngine : public QObject {
    Q_OBJECT
private Q_SLOTS:
    // clearMatches is a small Session-selection utility independent of the
    // legacy findAll/findNext/findPrevious trio removed 2026-06-10 (queue
    // #11) — populate a SearchMatch selection directly rather than via the
    // deleted legacy-buffer search.
    void clear_matches_removes_search_kind() {
        auto fixture = openCandidate(QStringLiteral("clear-matches"));
        auto &doc = *fixture.document;
        QCOMPARE(doc.importInitialMarkdown(QByteArrayLiteral("abc")).state,
                 VNext::ActionPublicationState::committed);

        Session *sess = doc.createSession();
        Selection x;
        x.kind = Selection::Kind::SearchMatch;
        sess->addSecondarySelection(x);

        SearchEngine s;
        s.clearMatches(sess);
        for (const Selection &y : sess->secondarySelections())
            QVERIFY(y.kind != Selection::Kind::SearchMatch);
    }

    // D2: per-block search finds matches in multiple blocks
    void search_findsMatchAcrossBlocks() {
        auto fixture = openCandidate(QStringLiteral("search-blocks"));
        auto &doc = *fixture.document;
        // Two paragraphs separated by a blank line: block 0 = "foo bar",
        // block 1 = "baz foo".
        QCOMPARE(doc.importInitialMarkdown(QByteArrayLiteral("foo bar\n\nbaz foo\n")).state,
                 VNext::ActionPublicationState::committed);

        const auto hits = SearchEngine::findByBlock(doc, "foo");
        QCOMPARE(hits.size(), 2);

        // The two blocks must be distinct.
        QVERIFY(hits[0].blockId != hits[1].blockId);

        // First block: "foo" at byte offset 0, length 3.
        QCOMPARE(hits[0].matchStart, uint32_t(0));
        QCOMPARE(hits[0].matchLen,   uint32_t(3));

        // Second block: "baz foo" — "foo" starts at byte offset 4, length 3.
        QCOMPARE(hits[1].matchStart, uint32_t(4));
        QCOMPARE(hits[1].matchLen,   uint32_t(3));
    }
    // ASK-M11: the declared-but-unimplemented flags must not silently behave
    // like a literal search. supports() is the query a UI greys out with, and
    // asking for an unsupported flag returns no matches (with a warning).
    void unsupported_flags_are_refused_not_silently_ignored() {
        QVERIFY(SearchEngine::supports(SearchEngine::NoFlags));
        QVERIFY(SearchEngine::supports(SearchEngine::CaseSensitive));
        QVERIFY(!SearchEngine::supports(SearchEngine::WholeWords));
        QVERIFY(!SearchEngine::supports(SearchEngine::Regex));
        QVERIFY(!SearchEngine::supports(SearchEngine::Backwards));

        auto fixture = openCandidate(QStringLiteral("search-flags"));
        auto &doc = *fixture.document;
        QCOMPARE(doc.importInitialMarkdown(QByteArrayLiteral("abc foo")).state,
                 VNext::ActionPublicationState::committed);

        // The literal search still works...
        QCOMPARE(SearchEngine::findByBlock(doc, QStringLiteral("foo"),
                                           SearchEngine::CaseSensitive).size(), 1);
        // ...but a request for an unimplemented flag is not quietly downgraded.
        QVERIFY(SearchEngine::findByBlock(doc, QStringLiteral("foo"),
                                          SearchEngine::Regex).isEmpty());
        QVERIFY(SearchEngine::findByBlock(doc, QStringLiteral("foo"),
                                          SearchEngine::WholeWords).isEmpty());
    }
};

QTEST_APPLESS_MAIN(TstFoundationSearchEngine)
#include "tst_foundation_search_engine.moc"
