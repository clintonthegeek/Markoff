// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff/core/Kf6SyntaxHighlightService.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/vnext/DocumentStorageProvider.h>
#include <memory>
#include <stdexcept>

using namespace Markoff;

namespace {
struct CandidateDocument {
    std::unique_ptr<VNext::DocumentStorageProvider> provider;
    std::unique_ptr<MarkoffDocument> document;
};

CandidateDocument openCandidate(const QString& key, const QByteArray& source)
{
    auto providerResult = VNext::makeEphemeralDocumentStorageProvider();
    if (!providerResult.ok()) throw std::runtime_error("provider creation failed");
    CandidateDocument result{std::move(providerResult).value(), {}};
    auto opened = MarkoffDocument::open(*result.provider, {key, 1}, VNext::StorageMode::ephemeral);
    if (!opened.ok()) throw std::runtime_error("document open failed");
    result.document = std::move(opened).value();
    if (result.document->importInitialMarkdown(source).state
        != VNext::ActionPublicationState::committed)
        throw std::runtime_error("initial Markdown import failed");
    return result;
}
}

class TstFoundationSyntaxHighlightService : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void available_languages_includes_cpp() {
        Kf6SyntaxHighlightService s;
        const QStringList langs = s.availableLanguages();
        bool found = false;
        for (const QString &l : langs)
            if (l.compare("c++", Qt::CaseInsensitive) == 0
                || l.compare("cpp", Qt::CaseInsensitive) == 0)
            { found = true; break; }
        QVERIFY(found);
    }

    void supports_language_returns_true_for_known() {
        Kf6SyntaxHighlightService s;
        QVERIFY(s.supportsLanguage("c++") || s.supportsLanguage("cpp"));
    }

    void highlight_yields_some_spans() {
        Kf6SyntaxHighlightService s;
        const QString lang = s.supportsLanguage("c++") ? "c++" : "cpp";
        const auto spans = s.highlight(lang,
            QByteArray("int main() { return 0; }"));
        QVERIFY(!spans.isEmpty());
    }

    void highlight_offsets_are_utf8_bytes_for_non_ascii() {
        Kf6SyntaxHighlightService s;
        const QString lang = s.supportsLanguage("c++") ? "c++" : "cpp";
        // "// é\n" = 2+1+2+1 = 6 UTF-8 bytes; second line starts at byte offset 6.
        // "int" on the second line must produce a span with offset >= 6.
        const QByteArray src = QString::fromUtf8("// é\nint main() { return 0; }").toUtf8();
        const auto spans = s.highlight(lang, src);
        QVERIFY(!spans.isEmpty());
        bool found_second_line = false;
        for (const auto &sp : spans)
            if (sp.offset >= 6) { found_second_line = true; break; }
        QVERIFY(found_second_line);
    }

    // D2: verify that doc.inlineSpansFor(blockId) is accessible from view
    // code and returns a result without crashing (even if empty for plain text).
    void inline_spans_for_block_does_not_crash() {
        auto fixture = openCandidate(QStringLiteral("inline-spans"),
            QByteArrayLiteral("# Heading\n\nSome `code` here.\n"));
        auto& doc = *fixture.document;

        const auto blocks = doc.iterateBlocks();
        QVERIFY(!blocks.empty());

        // inlineSpansFor must not crash for any block. The result may be empty
        // for blocks that have no inline markup.
        for (const BlockId id : blocks) {
            const auto spans = doc.inlineSpansFor(id);
            // No crash = pass. Result count is not asserted because the inline
            // parse cache is allowed to return an empty list for plain-text blocks.
            Q_UNUSED(spans)
        }

        // A block containing inline code ("Some `code` here.") should return
        // at least one span — verify accessibility of the non-empty case.
        // The second block (index 1, after the heading) is the paragraph.
        if (blocks.size() >= 2) {
            const auto spans = doc.inlineSpansFor(blocks[1]);
            // SyntaxHighlightService is independent; inlineSpansFor provides
            // markdown-parser inline spans (bold, code, links, etc.).
            // We only assert non-crash here; content is tested in
            // tst_d2_inline_parse_cache.
            Q_UNUSED(spans)
        }
    }
};

QTEST_APPLESS_MAIN(TstFoundationSyntaxHighlightService)
#include "tst_foundation_syntax_highlight_service.moc"
