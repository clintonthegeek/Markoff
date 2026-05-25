// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff/parser/Document.h>
#include <markoff/parser/SourceSpan.h>

using Markoff::Document;
using Markoff::LinkTarget;
using Markoff::SourceSpan;

namespace {

QList<SourceSpan> spansForFirstBlock(const Document &doc) {
    const auto blocks = doc.topLevelBlocks();
    if (blocks.isEmpty()) return {};
    return blocks.first().inlineSpans;
}

bool hasWikilinkSpan(const QList<SourceSpan> &spans, const LinkTarget &want) {
    for (const auto &s : spans)
        if (s.isWikilink && s.linkTarget == want) return true;
    return false;
}

}  // namespace

class TestInlineSpansLinkTarget : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void wikilink_plain_page_target() {
        auto d = Document::fromMarkdown(QStringLiteral("See [[Page]]."));
        QVERIFY(d);
        LinkTarget want;
        want.page = QStringLiteral("Page");
        QVERIFY(hasWikilinkSpan(spansForFirstBlock(*d), want));
    }

    void wikilink_alias_target() {
        auto d = Document::fromMarkdown(QStringLiteral("See [[Page|Alias]]."));
        QVERIFY(d);
        LinkTarget want;
        want.page = QStringLiteral("Page");
        want.alias = QStringLiteral("Alias");
        QVERIFY(hasWikilinkSpan(spansForFirstBlock(*d), want));
    }

    void wikilink_section_target() {
        auto d = Document::fromMarkdown(QStringLiteral("See [[Page#Sec]]."));
        QVERIFY(d);
        LinkTarget want;
        want.page = QStringLiteral("Page");
        want.section = QStringLiteral("Sec");
        QVERIFY(hasWikilinkSpan(spansForFirstBlock(*d), want));
    }

    void wikilink_block_ref_target() {
        auto d = Document::fromMarkdown(QStringLiteral("See [[Page#^abc]]."));
        QVERIFY(d);
        LinkTarget want;
        want.page = QStringLiteral("Page");
        want.blockRef = QStringLiteral("abc");
        QVERIFY(hasWikilinkSpan(spansForFirstBlock(*d), want));
    }

    void standard_link_url() {
        auto d = Document::fromMarkdown(QStringLiteral("See [text](https://x.y)."));
        QVERIFY(d);
        LinkTarget want;
        want.url = QStringLiteral("https://x.y");
        const auto spans = spansForFirstBlock(*d);
        bool found = false;
        for (const auto &s : spans)
            if (s.isLink && !s.isWikilink && s.linkTarget == want) { found = true; break; }
        QVERIFY(found);
    }

    void non_link_span_has_empty_target() {
        auto d = Document::fromMarkdown(QStringLiteral("Plain text."));
        QVERIFY(d);
        for (const auto &s : spansForFirstBlock(*d))
            QVERIFY(s.linkTarget.isEmpty());
    }
};

QTEST_APPLESS_MAIN(TestInlineSpansLinkTarget)
#include "tst_inline_spans_link_target.moc"
