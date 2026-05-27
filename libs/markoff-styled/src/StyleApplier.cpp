// SPDX-License-Identifier: GPL-3.0-or-later
#include "StyleApplier.h"

#include <cstring>

#include <QFont>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>

#include <markoff/core/BlockKind.h>
#include <markoff/core/Cmd/D2.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/SourceTextDocumentBinding.h>
#include <markoff/parser/SourceSpan.h>

namespace {

QTextBlockFormat baseBlockFormat() {
    QTextBlockFormat fmt;
    fmt.setTopMargin(0);
    fmt.setBottomMargin(0);
    fmt.setLeftMargin(0);
    fmt.setIndent(0);
    return fmt;
}

void applyHeading(QTextCursor &cursor, int level, qreal fontScale) {
    static constexpr qreal kBaseSize = 11.0;
    static constexpr qreal kRatios[6] = { 2.0, 1.7, 1.4, 1.2, 1.0, 0.9 };
    const int idx = qBound(1, level, 6) - 1;
    QTextBlockFormat bf = baseBlockFormat();
    bf.setTopMargin(8 * fontScale);
    bf.setBottomMargin(4 * fontScale);
    cursor.setBlockFormat(bf);

    QTextCharFormat cf;
    cf.setFontPointSize(kBaseSize * kRatios[idx] * fontScale);
    cf.setFontWeight(QFont::Bold);
    cursor.setBlockCharFormat(cf);
}

void applyParagraph(QTextCursor &cursor, qreal fontScale) {
    QTextBlockFormat bf = baseBlockFormat();
    bf.setTopMargin(2 * fontScale);
    bf.setBottomMargin(2 * fontScale);
    cursor.setBlockFormat(bf);

    QTextCharFormat cf;
    cf.setFontPointSize(11.0 * fontScale);
    cf.setFontWeight(QFont::Normal);
    cursor.setBlockCharFormat(cf);
}

void applyCodeBlock(QTextCursor &cursor, qreal fontScale) {
    QTextBlockFormat bf = baseBlockFormat();
    bf.setLeftMargin(12 * fontScale);
    bf.setTopMargin(2);
    bf.setBottomMargin(2);
    bf.setBackground(QColor(245, 245, 245));  // Theme::CodeBlockBackground
                                              // resolved in Task 9 wiring.
    cursor.setBlockFormat(bf);

    QTextCharFormat cf;
    cf.setFontFamilies({QStringLiteral("monospace")});
    cf.setFontFixedPitch(true);
    cf.setFontPointSize(10.0 * fontScale);
    cursor.setBlockCharFormat(cf);
}

void applyBlockquote(QTextCursor &cursor, int depth, qreal fontScale) {
    QTextBlockFormat bf = baseBlockFormat();
    bf.setLeftMargin(16 * fontScale * qMax(1, depth));
    bf.setTopMargin(2);
    bf.setBottomMargin(2);
    cursor.setBlockFormat(bf);

    QTextCharFormat cf;
    cf.setFontPointSize(11.0 * fontScale);
    cf.setForeground(QColor(100, 100, 100));  // Theme::Quote.
    cursor.setBlockCharFormat(cf);
}

void applyListItem(QTextCursor &cursor, int depth, qreal fontScale) {
    QTextBlockFormat bf = baseBlockFormat();
    bf.setLeftMargin(16 * fontScale * qMax(1, depth + 1));
    bf.setTopMargin(1);
    bf.setBottomMargin(1);
    cursor.setBlockFormat(bf);

    QTextCharFormat cf;
    cf.setFontPointSize(11.0 * fontScale);
    cursor.setBlockCharFormat(cf);
}

void applyHorizontalRule(QTextCursor &cursor, qreal fontScale) {
    QTextBlockFormat bf = baseBlockFormat();
    bf.setTopMargin(6 * fontScale);
    bf.setBottomMargin(6 * fontScale);
    cursor.setBlockFormat(bf);

    QTextCharFormat cf;
    cf.setFontFamilies({QStringLiteral("monospace")});
    cf.setFontFixedPitch(true);
    cf.setForeground(QColor(180, 180, 180));
    cf.setFontPointSize(11.0 * fontScale);
    cursor.setBlockCharFormat(cf);
}

QTextCharFormat charFormatForSpan(const Markoff::SourceSpan &span,
                                  qreal /*fontScale*/) {
    QTextCharFormat fmt;
    if (span.bold)          fmt.setFontWeight(QFont::Bold);
    if (span.italic)        fmt.setFontItalic(true);
    if (span.strikethrough) fmt.setFontStrikeOut(true);
    if (span.code) {
        fmt.setFontFamilies({QStringLiteral("monospace")});
        fmt.setFontFixedPitch(true);
        fmt.setBackground(QColor(245, 245, 245));   // Theme::InlineCodeBackground
    }
    if (span.highlight) {
        fmt.setBackground(QColor(255, 240, 130));   // Theme::Highlight
    }
    if (span.isTag) {
        fmt.setForeground(QColor(70, 130, 180));    // Theme::Tag
    }
    if (span.isFootnoteRef) {
        fmt.setForeground(QColor(150, 90, 150));    // Theme::FootnoteRef
        fmt.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
    }
    if (span.isLink || span.isWikilink) {
        fmt.setAnchor(true);
        // Anchor href is informational (accessibility introspection).
        // Click resolution flows through LinkService (Task 10), NOT this href.
        fmt.setAnchorHref(span.isWikilink ? span.linkTarget.page
                                          : span.linkTarget.url);
        fmt.setFontUnderline(true);
        fmt.setForeground(span.isWikilink
                          ? QColor(120, 80, 200)    // Theme::WikiLink
                          : QColor(40, 100, 200));  // Theme::Link
    }
    return fmt;
}

quint64 computeBlockHash(Markoff::BlockKind kind,
                         const QByteArray &text,
                         const QList<Markoff::SourceSpan> &spans,
                         qreal fontScale) {
    quint64 h = qHash(int(kind));
    h ^= qHash(text);
    h ^= quint64(text.size()) * 0x9E3779B97F4A7C15ULL;
    h ^= quint64(spans.size()) << 32;
    for (const Markoff::SourceSpan &span : spans) {
        h ^= quint64(span.charOffset) * 0xBF58476D1CE4E5B9ULL;
        h ^= quint64(span.charLength) << 16;
        const quint64 flagBits =
            (span.bold          ? 1ULL << 0  : 0) |
            (span.italic        ? 1ULL << 1  : 0) |
            (span.strikethrough ? 1ULL << 2  : 0) |
            (span.code          ? 1ULL << 3  : 0) |
            (span.highlight     ? 1ULL << 4  : 0) |
            (span.isLink        ? 1ULL << 5  : 0) |
            (span.isWikilink    ? 1ULL << 6  : 0) |
            (span.isTag         ? 1ULL << 7  : 0) |
            (span.isFootnoteRef ? 1ULL << 8  : 0);
        h ^= flagBits;
    }
    // Mix in fontScale (cast to quint64 bits for stable hashing).
    quint64 fsBits = 0;
    std::memcpy(&fsBits, &fontScale, sizeof(fsBits));
    h ^= fsBits;
    return h;
}

Markoff::BlockKind inferKindFromPrefix(const QByteArray &text,
                                       Markoff::BlockKind currentKind) {
    if (text.isEmpty()) return Markoff::BlockKind::Paragraph;

    // Heading: 1-6 '#' followed by space, or 1-6 '#' followed by EOF.
    int hashCount = 0;
    while (hashCount < text.size() && hashCount < 7 && text[hashCount] == '#')
        ++hashCount;
    if (hashCount >= 1 && hashCount <= 6) {
        if (hashCount == text.size()
            || text[hashCount] == ' '
            || text[hashCount] == '\n') {
            return Markoff::BlockKind::Heading;
        }
    }

    // BlockQuote: starts with "> " or is exactly ">".
    if (text.startsWith("> ") || text == ">") {
        return Markoff::BlockKind::BlockQuote;
    }

    // ListItem: ^[ \t]{0,3}([-*+]|\d+[.)])\s — same as markoff-live.
    static const QRegularExpression listRe(
        QStringLiteral("^[ \\t]{0,3}([-*+]|\\d+[.)])\\s"));
    if (listRe.match(QString::fromUtf8(text)).hasMatch()) {
        return Markoff::BlockKind::ListItem;
    }

    // CodeBlock and HorizontalRule inference deferred to v0.2
    // (fence-state matching, not pure prefix). Currently we rely on
    // the CRDT load path to set these correctly.
    if (currentKind == Markoff::BlockKind::CodeBlock
        || currentKind == Markoff::BlockKind::HorizontalRule) {
        return currentKind;  // preserve, don't reinfer.
    }

    return Markoff::BlockKind::Paragraph;
}

}  // namespace

namespace Markoff::Styled {

StyleApplier::StyleApplier(QObject *parent) : QObject(parent) {}
StyleApplier::~StyleApplier() = default;

void StyleApplier::setTextDocument(QTextDocument *doc) {
    if (m_textDocument == doc) return;
    m_textDocument = doc;
    rerender();
}

void StyleApplier::setMarkoffDocument(Markoff::MarkoffDocument *doc) {
    if (m_markoffDocument == doc) return;
    if (m_markoffDocument) {
        disconnect(m_markoffDocument, &Markoff::MarkoffDocument::d2DocumentChanged,
                   this, &StyleApplier::onD2Changed);
    }
    m_markoffDocument = doc;
    if (m_markoffDocument) {
        connect(m_markoffDocument, &Markoff::MarkoffDocument::d2DocumentChanged,
                this, &StyleApplier::onD2Changed);
    }
    rerender();
}

void StyleApplier::setTheme(const Markoff::Theme *theme) {
    if (m_theme == theme) return;
    m_theme = theme;
    rerender();
}

void StyleApplier::setFontScale(qreal s) {
    if (qFuzzyCompare(m_fontScale, s)) return;
    m_fontScale = s;
    rerender();
}

void StyleApplier::rerender() {
    if (!m_textDocument || !m_markoffDocument) return;
    m_blockHashes.clear();  // Force every block to apply on next pass.
    applyFormats();
}

void StyleApplier::onD2Changed() { applyFormats(); }

void StyleApplier::applyFormats() {
    if (m_applyingFormats) return;
    if (!m_textDocument || !m_markoffDocument) return;
    m_applyingFormats = true;
    m_hashSkipsLastPass = 0;
    {
        QSignalBlocker block(m_textDocument);
        QTextCursor cursor(m_textDocument);
        cursor.beginEditBlock();

        // The QTextDocument is populated with flatView() (separator-bearing:
        // blocks joined by "\n\n"). Compute block byte positions in that
        // coordinate space by summing block sizes + separator lengths, then
        // convert to Qt UTF-16 char positions.
        const QByteArray flatBytes = m_markoffDocument->flatView();
        const std::vector<Markoff::BlockId> blocks = m_markoffDocument->iterateBlocks();
        static constexpr int kSepLen = 2;  // "\n\n"

        // Set-like map of all IDs present this pass, for stale-entry pruning below.
        QHash<Markoff::BlockId, char> currentIds;
        currentIds.reserve(static_cast<qsizetype>(blocks.size()));

        quint32 bytePos = 0;
        for (size_t i = 0; i < blocks.size(); ++i) {
            const Markoff::BlockId id = blocks[i];
            currentIds.insert(id, 0);

            const QByteArray text = m_markoffDocument->blockText(id);
            const quint32 blockStart = bytePos;
            const quint32 blockEnd   = bytePos + static_cast<quint32>(text.size());

            const Markoff::BlockKind kind = m_markoffDocument->blockKind(id);
            const QList<Markoff::SourceSpan> spans = m_markoffDocument->inlineSpansFor(id);
            const quint64 h = computeBlockHash(kind, text, spans, m_fontScale);

            if (m_blockHashes.value(id, 0) == h) {
                // Block unchanged — skip format reapplication, but still
                // advance the bytePos accumulator so subsequent blocks are
                // computed correctly.
                ++m_hashSkipsLastPass;
                bytePos = blockEnd;
                if (i + 1 < blocks.size()) bytePos += kSepLen;
                continue;
            }
            m_blockHashes[id] = h;

            // Kind transition: if text prefix disagrees with stored kind, queue a
            // Cmd::changeKind for deferred dispatch. The current pass still
            // formats using `kind` (the stored kind) — the next d2 cycle, after
            // changeKind lands, will format using the corrected kind.
            const Markoff::BlockKind inferred = inferKindFromPrefix(text, kind);
            if (inferred != kind) {
                m_pendingKindChanges.push_back({id, inferred});
            }

            const int startQt = Markoff::SourceTextDocumentBinding
                ::byteOffsetToQtPos(flatBytes, blockStart);
            const int endQt = Markoff::SourceTextDocumentBinding
                ::byteOffsetToQtPos(flatBytes, blockEnd);

            cursor.setPosition(startQt);
            QTextBlock qblk = cursor.block();
            while (qblk.isValid() && qblk.position() <= endQt) {
                QTextCursor blkCursor(qblk);
                if (kind == Markoff::BlockKind::Heading) {
                    int level = 0;
                    while (level < text.size() && text[level] == '#') ++level;
                    level = qBound(1, level, 6);
                    applyHeading(blkCursor, level, m_fontScale);
                } else if (kind == Markoff::BlockKind::Paragraph) {
                    applyParagraph(blkCursor, m_fontScale);
                } else if (kind == Markoff::BlockKind::CodeBlock) {
                    applyCodeBlock(blkCursor, m_fontScale);
                } else if (kind == Markoff::BlockKind::BlockQuote) {
                    int depth = 1;
                    if (!text.isEmpty()) {
                        depth = 0;
                        for (int bi = 0; bi < text.size() && text[bi] == '>'; ++bi) ++depth;
                        depth = qMax(1, depth);
                    }
                    applyBlockquote(blkCursor, depth, m_fontScale);
                } else if (kind == Markoff::BlockKind::ListItem) {
                    int depth = 0;
                    while (depth < text.size() && (text[depth] == ' ' || text[depth] == '\t')) ++depth;
                    depth /= 2;  // 2 spaces per indent level — close enough for v0.
                    applyListItem(blkCursor, depth, m_fontScale);
                } else if (kind == Markoff::BlockKind::HorizontalRule) {
                    applyHorizontalRule(blkCursor, m_fontScale);
                } else {
                    applyParagraph(blkCursor, m_fontScale);
                }
                qblk = qblk.next();
            }

            // Apply inline char formats (bold, italic, strikethrough) for this block.
            // docLen - 1: QTextDocument always has a trailing paragraph separator that
            // must not be selected, so cap positions at characterCount() - 1.
            const int docLen = m_textDocument->characterCount() - 1;
            for (const Markoff::SourceSpan &span : spans) {
                if (span.charLength <= 0) continue;
                const int spanStart = startQt + span.charOffset;
                const int spanEnd   = startQt + span.charOffset + span.charLength;
                if (spanStart >= docLen) continue;
                QTextCursor c(m_textDocument);
                c.setPosition(spanStart);
                c.setPosition(qMin(spanEnd, docLen), QTextCursor::KeepAnchor);
                c.mergeCharFormat(charFormatForSpan(span, m_fontScale));
            }

            // Advance past block content + separator ("\n\n") between blocks.
            bytePos = blockEnd;
            if (i + 1 < blocks.size()) bytePos += kSepLen;
        }

        // Prune stale entries (blocks that no longer exist in the document).
        for (auto it = m_blockHashes.begin(); it != m_blockHashes.end(); ) {
            if (!currentIds.contains(it.key())) {
                it = m_blockHashes.erase(it);
            } else {
                ++it;
            }
        }

        cursor.endEditBlock();
    }
    ++m_restyleCount;
    m_applyingFormats = false;

    if (!m_pendingKindChanges.empty()) {
        QTimer::singleShot(0, this, &StyleApplier::applyPendingKindChanges);
    }
}

void StyleApplier::applyPendingKindChanges() {
    if (!m_markoffDocument) {
        m_pendingKindChanges.clear();
        return;
    }
    auto changes = std::move(m_pendingKindChanges);
    m_pendingKindChanges.clear();
    for (const auto &chg : changes) {
        Markoff::Cmd::changeKind(*m_markoffDocument, chg.id, chg.newKind);
    }
}

}  // namespace Markoff::Styled
