// SPDX-License-Identifier: GPL-3.0-or-later
#include "StyleApplier.h"

#include <cstring>

#include <QFont>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextList>
#include <QTextListFormat>
#include <QTimer>

#include <markoff/core/AttrNames.h>
#include <markoff/core/BlockAttrsMap.h>
#include <markoff/core/BlockKind.h>
#include <markoff/core/Cmd/D2.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/SourceTextDocumentBinding.h>
#include <markoff/parser/SourceSpan.h>

namespace {

// Base body font size. Every per-kind cf.setFontPointSize() uses this
// (scaled) value; spacing helpers below derive from it so margins and
// indents stay font-relative under zoom.
constexpr qreal kBaseBodyPt = 11.0;

inline qreal emPt(qreal fontScale) { return kBaseBodyPt * fontScale; }

// Per-kind block margins, expressed as multiples of the current em.
// Adjacent QTextBlockFormat margins do NOT collapse (unlike CSS), so the
// effective inter-block gap is bottom-of-prev + top-of-next.
//
// Targets: ~1em between consecutive paragraphs (body-text rhythm),
// tighter for list items (marker already groups them), and headings
// announce themselves with a slightly larger top gap.
inline qreal paragraphMarginPt(qreal s) { return emPt(s) * 0.45; }
inline qreal listItemMarginPt(qreal s)  { return emPt(s) * 0.18; }
inline qreal codeBlockMarginPt(qreal s) { return emPt(s) * 0.30; }
inline qreal blockquoteMarginPt(qreal s){ return emPt(s) * 0.30; }
inline qreal headingTopMarginPt(qreal s){ return emPt(s) * 0.80; }
inline qreal headingBotMarginPt(qreal s){ return emPt(s) * 0.35; }
inline qreal hruleMarginPt(qreal s)     { return emPt(s) * 0.60; }

// QTextDocument::indentWidth — the per-indent-unit horizontal step used
// by QTextList. Qt's default is the width of "0000" in the body font,
// which produces oversized markers-to-text gaps for our visual rhythm.
// ~1.5em keeps the bullet comfortably separated from text while letting
// nested lists step in by visible-but-modest amounts.
inline qreal docIndentWidthPx(qreal s) { return emPt(s) * 1.5; }

QTextBlockFormat baseBlockFormat() {
    QTextBlockFormat fmt;
    fmt.setLeftMargin(0);
    fmt.setIndent(0);
    return fmt;
}

// Apply a block-level char format both as the block default (for newly
// typed text + test introspection via QTextBlock::charFormat()) AND to the
// block's existing characters via a selection. The latter is load-bearing:
// the inline-span pass that runs after the block-format pass uses
// mergeCharFormat over span ranges, and merge operates on each character's
// *explicit* format. Without an explicit per-character baseline here, the
// merge starts from an empty format and the block's font size/weight is
// lost (headings rendered at body size — dogfood bug 2026-05-27). Applying
// to the selection establishes the baseline so inline emphasis stacks on
// top without erasing size.
void applyBlockCharFormat(QTextCursor &cursor, const QTextCharFormat &cf) {
    cursor.setBlockCharFormat(cf);
    QTextCursor sel(cursor);
    sel.movePosition(QTextCursor::StartOfBlock);
    sel.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    sel.setCharFormat(cf);
}

void applyHeading(QTextCursor &cursor, int level, qreal fontScale) {
    static constexpr qreal kRatios[6] = { 2.0, 1.7, 1.4, 1.2, 1.0, 0.9 };
    const int idx = qBound(1, level, 6) - 1;
    QTextBlockFormat bf = baseBlockFormat();
    bf.setTopMargin(headingTopMarginPt(fontScale));
    bf.setBottomMargin(headingBotMarginPt(fontScale));
    cursor.setBlockFormat(bf);

    QTextCharFormat cf;
    cf.setFontPointSize(kBaseBodyPt * kRatios[idx] * fontScale);
    cf.setFontWeight(QFont::Bold);
    applyBlockCharFormat(cursor, cf);
}

void applyParagraph(QTextCursor &cursor, qreal fontScale) {
    QTextBlockFormat bf = baseBlockFormat();
    bf.setTopMargin(paragraphMarginPt(fontScale));
    bf.setBottomMargin(paragraphMarginPt(fontScale));
    cursor.setBlockFormat(bf);

    QTextCharFormat cf;
    cf.setFontPointSize(emPt(fontScale));
    cf.setFontWeight(QFont::Normal);
    applyBlockCharFormat(cursor, cf);
}

void applyCodeBlock(QTextCursor &cursor, qreal fontScale) {
    QTextBlockFormat bf = baseBlockFormat();
    bf.setLeftMargin(emPt(fontScale) * 1.0);
    bf.setTopMargin(codeBlockMarginPt(fontScale));
    bf.setBottomMargin(codeBlockMarginPt(fontScale));
    bf.setBackground(QColor(245, 245, 245));  // Theme::CodeBlockBackground
                                              // resolved in Task 9 wiring.
    cursor.setBlockFormat(bf);

    QTextCharFormat cf;
    cf.setFontFamilies({QStringLiteral("monospace")});
    cf.setFontFixedPitch(true);
    cf.setFontPointSize(emPt(fontScale) * (10.0 / kBaseBodyPt));
    applyBlockCharFormat(cursor, cf);
}

void applyBlockquote(QTextCursor &cursor, int depth, qreal fontScale) {
    QTextBlockFormat bf = baseBlockFormat();
    bf.setLeftMargin(emPt(fontScale) * qMax(1, depth));
    bf.setTopMargin(blockquoteMarginPt(fontScale));
    bf.setBottomMargin(blockquoteMarginPt(fontScale));
    cursor.setBlockFormat(bf);

    QTextCharFormat cf;
    cf.setFontPointSize(emPt(fontScale));
    cf.setForeground(QColor(100, 100, 100));  // Theme::Quote.
    applyBlockCharFormat(cursor, cf);
}

void applyListItem(QTextCursor &cursor, int depth,
                   const QString &markerStyle, bool checked,
                   qreal fontScale) {
    QTextBlockFormat bf = baseBlockFormat();
    bf.setTopMargin(listItemMarginPt(fontScale));
    bf.setBottomMargin(listItemMarginPt(fontScale));

    // Task-list checkboxes are the only marker type QTextBlockFormat can
    // render natively. Set them on the block format; clear any prior marker
    // when the kind isn't task.
    if (markerStyle == QStringLiteral("task")) {
        bf.setMarker(checked ? QTextBlockFormat::MarkerType::Checked
                             : QTextBlockFormat::MarkerType::Unchecked);
    } else {
        bf.setMarker(QTextBlockFormat::MarkerType::NoMarker);
    }
    cursor.setBlockFormat(bf);

    QTextCharFormat cf;
    cf.setFontPointSize(emPt(fontScale));
    applyBlockCharFormat(cursor, cf);

    // QTextList membership (bullet / numeral rendering) is handled by
    // manageListMembership in the walk so consecutive same-style items
    // share one list (continuous numbering for ordered items). The walk
    // runs OUTSIDE the hash gate so this format-only function isn't
    // responsible for cross-block continuity. Spec:
    // docs/specs/2026-05-29-styled-ordered-list-continuous-numbering-design.md.
    Q_UNUSED(depth);
}

void applyHorizontalRule(QTextCursor &cursor, qreal fontScale) {
    QTextBlockFormat bf = baseBlockFormat();
    bf.setTopMargin(hruleMarginPt(fontScale));
    bf.setBottomMargin(hruleMarginPt(fontScale));
    cursor.setBlockFormat(bf);

    QTextCharFormat cf;
    cf.setFontFamilies({QStringLiteral("monospace")});
    cf.setFontFixedPitch(true);
    cf.setForeground(QColor(180, 180, 180));
    cf.setFontPointSize(emPt(fontScale));
    applyBlockCharFormat(cursor, cf);
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
                         const QHash<Markoff::AttrName, Markoff::AttrValue> &attrs,
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
    // Attrs: XOR-combine per-entry (order-insensitive; sidesteps QHash's
    // non-deterministic iteration order across Qt versions). AttrValue is
    // std::variant<int, QString, bool>; an unhandled alternative wedges a
    // static_assert at compile time.
    // Spec: docs/specs/2026-05-29-styled-hash-gate-over-attrs-design.md.
    for (auto it = attrs.cbegin(); it != attrs.cend(); ++it) {
        quint64 entry = qHash(it.key());
        entry *= 0x9E3779B97F4A7C15ULL;
        std::visit([&](const auto &v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, int>) {
                entry ^= quint64(v) * 0xBF58476D1CE4E5B9ULL;
            } else if constexpr (std::is_same_v<T, bool>) {
                entry ^= v ? 1ULL : 2ULL;
            } else if constexpr (std::is_same_v<T, QString>) {
                entry ^= qHash(v);
            } else {
                static_assert(sizeof(T) == 0,
                              "Unhandled AttrValue alternative");
            }
        }, it.value());
        h ^= entry;
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

    // Conservative: never DEMOTE a parsed block to Paragraph based on
    // missing markers. The parser strips structural markers from blockText
    // for many kinds (ListItem stores "text" not "- text"; Table stores
    // "| ... |" but our infer rules don't recognize it; etc.). Returning
    // Paragraph here on a non-Paragraph block would destroy loaded
    // structure. Only PROMOTE Paragraph -> X when a positive rule above
    // fires. The "user deletes `##` from a heading" demotion-on-typing
    // case is acknowledged as a v0.2 concern; not worth corrupting loaded
    // documents for.
    return currentKind;
}

struct ListStackEntry {
    int depth = -1;
    QString markerStyle;
    QTextList *list = nullptr;
};

// Reconcile a model block's QTextList membership against the
// neighbour-aware list stack. Consecutive same-(markerStyle, depth)
// ListItems share one QTextList (continuous numbering for ordered
// items); nested-list transitions resume the outer list per CommonMark
// via depth-stack pops; non-ListItem blocks and task ListItems break
// the chain.
//
// Runs OUTSIDE the hash gate so a structural change (e.g. paragraph
// inserted between two formerly-adjacent items) breaks the prior
// shared list even when neither item's content hash changed.
//
// Spec: docs/specs/2026-05-29-styled-ordered-list-continuous-numbering-design.md.
void manageListMembership(
    QTextBlock qblk,
    Markoff::BlockKind kind,
    const QHash<Markoff::AttrName, Markoff::AttrValue> &attrs,
    std::vector<ListStackEntry> &listStack)
{
    auto removeFromAnyList = [&](QTextBlock b) {
        if (QTextList *lst = b.textList())
            lst->remove(b);
    };

    if (kind != Markoff::BlockKind::ListItem) {
        // Any non-list block ends the enclosing list per CommonMark.
        listStack.clear();
        removeFromAnyList(qblk);
        return;
    }

    int depth = 0;
    if (auto it = attrs.find(Markoff::AttrNames::IndentLevel);
        it != attrs.end() && std::holds_alternative<int>(*it))
        depth = std::get<int>(*it);
    QString markerStyle;
    if (auto it = attrs.find(Markoff::AttrNames::MarkerStyle);
        it != attrs.end() && std::holds_alternative<QString>(*it))
        markerStyle = std::get<QString>(*it);

    if (markerStyle == QStringLiteral("task")) {
        // Task items render their checkbox via QTextBlockFormat::Marker,
        // not via QTextList. They interrupt the same-depth list of any
        // marker style.
        while (!listStack.empty() && listStack.back().depth > depth)
            listStack.pop_back();
        if (!listStack.empty() && listStack.back().depth == depth)
            listStack.pop_back();
        removeFromAnyList(qblk);
        return;
    }

    // Non-task ListItem: depth-stack reconciliation.
    while (!listStack.empty() && listStack.back().depth > depth)
        listStack.pop_back();

    if (!listStack.empty() && listStack.back().depth == depth) {
        if (listStack.back().markerStyle == markerStyle) {
            listStack.back().list->add(qblk);
            return;
        }
        // Same depth, different marker style: end the running list,
        // fall through to create a fresh one at this depth.
        listStack.pop_back();
    }

    // Empty stack or top is shallower than this item — new list.
    QTextListFormat lf;
    if (markerStyle == QStringLiteral("dot")
        || markerStyle == QStringLiteral("paren"))
        lf.setStyle(QTextListFormat::ListDecimal);
    else  // minus / plus / star / unknown → disc
        lf.setStyle(QTextListFormat::ListDisc);
    lf.setIndent(depth + 1);
    QTextCursor c(qblk);
    QTextList *fresh = c.createList(lf);
    listStack.push_back({depth, markerStyle, fresh});
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

void StyleApplier::setTextEdit(QTextEdit *edit) {
    m_textEdit = edit;
}

void StyleApplier::captureScrollBeforeEdit() {
    if (!m_textEdit || !m_textEdit->verticalScrollBar()) return;
    m_pendingScrollCapture = m_textEdit->verticalScrollBar()->value();
}

void StyleApplier::rerender() {
    if (!m_textDocument || !m_markoffDocument) return;
    m_blockHashes.clear();  // Force every block to apply on next pass.
    applyFormats();
}

void StyleApplier::onD2Changed() {
    applyFormats();
}

void StyleApplier::applyFormats() {
    if (m_applyingFormats) return;
    if (!m_textDocument || !m_markoffDocument) return;
    m_applyingFormats = true;

    // Snapshot scroll + previous block IDs for in-place-edit detection.
    // Prefer the pre-captured value from captureScrollBeforeEdit() (which
    // fires before SourceTextDocumentBinding's setPlainText resets the bar),
    // falling back to an in-place snapshot when no early capture is available
    // (e.g. rerender() calls, direct onD2Changed without Editor wiring).
    // The restore is deferred (see QTimer::singleShot below) because Qt's
    // layout signals fire after endEditBlock and would override a synchronous
    // setValue.
    const int savedScroll = (m_pendingScrollCapture >= 0)
        ? m_pendingScrollCapture
        : ((m_textEdit && m_textEdit->verticalScrollBar())
           ? m_textEdit->verticalScrollBar()->value() : -1);
    m_pendingScrollCapture = -1;  // consume; next pass needs a fresh capture.

    QHash<Markoff::BlockId, char> previousBlockIds;
    for (auto it = m_blockHashes.constBegin();
         it != m_blockHashes.constEnd(); ++it) {
        previousBlockIds.insert(it.key(), 0);
    }

    m_hashSkipsLastPass = 0;
    // Declared outside the QSignalBlocker scope so it's visible to the
    // structural-change check below.
    QHash<Markoff::BlockId, char> currentIds;
    {
        QSignalBlocker block(m_textDocument);
        QTextCursor cursor(m_textDocument);
        cursor.beginEditBlock();

        // QTextDocument::indentWidth governs the per-indent-step horizontal
        // distance used by QTextList — the bullet-to-text gap and the
        // depth-N indent. Set it font-relative here so it tracks fontScale
        // changes (zoom).
        m_textDocument->setIndentWidth(docIndentWidthPx(m_fontScale));

        // The QTextDocument is populated with widgetFlatView() (separator-bearing:
        // blocks joined by single "\n" — WP unification, 2026-05-28). Compute
        // block byte positions in that coordinate space by summing block sizes
        // + separator lengths, then convert to Qt UTF-16 char positions.
        const QByteArray flatBytes = m_markoffDocument->widgetFlatView();
        const std::vector<Markoff::BlockId> blocks = m_markoffDocument->iterateBlocks();
        static constexpr int kSepLen = 1;  // single "\n"

        // Set-like map of all IDs present this pass, for stale-entry pruning below.
        currentIds.reserve(static_cast<qsizetype>(blocks.size()));

        // List-membership stack for continuous numbering across the walk.
        // Reset each cascade; manageListMembership runs OUTSIDE the hash gate
        // so a paragraph inserted between two formerly-adjacent items breaks
        // the prior shared list even when neither item's hash changed.
        std::vector<ListStackEntry> listStack;

        quint32 bytePos = 0;
        for (size_t i = 0; i < blocks.size(); ++i) {
            const Markoff::BlockId id = blocks[i];
            currentIds.insert(id, 0);

            const QByteArray text = m_markoffDocument->blockText(id);
            const quint32 blockStart = bytePos;
            const quint32 blockEnd   = bytePos + static_cast<quint32>(text.size());

            const Markoff::BlockKind kind = m_markoffDocument->blockKind(id);
            const QList<Markoff::SourceSpan> spans = m_markoffDocument->inlineSpansFor(id);
            const auto attrs = m_markoffDocument->blockAttrs(id);
            const quint64 h = computeBlockHash(kind, text, spans, attrs, m_fontScale);

            // Resolve the QTextBlock for this model block once — used by
            // both the format pass (when not hash-skipped) and the list-
            // membership call below (always runs).
            const int startQt = Markoff::SourceTextDocumentBinding
                ::byteOffsetToQtPos(flatBytes, blockStart);

            const bool hashSkipped = (m_blockHashes.value(id, 0) == h);
            if (hashSkipped) {
                ++m_hashSkipsLastPass;
            } else {
            m_blockHashes[id] = h;

            // Kind transition: if text prefix disagrees with stored kind, queue a
            // Cmd::changeKind for deferred dispatch. The current pass still
            // formats using `kind` (the stored kind) — the next d2 cycle, after
            // changeKind lands, will format using the corrected kind.
            const Markoff::BlockKind inferred = inferKindFromPrefix(text, kind);
            if (inferred != kind) {
                m_pendingKindChanges.push_back({id, inferred});
            }

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
                    // Depth from BlockQuoteDepth attr (queue #8.1).
                    // Buffer no longer carries '>' markers post-load
                    // canonicalisation, so the previous peel-from-text
                    // path doesn't apply. Spec
                    // docs/specs/2026-05-29-blockquote-multi-paragraph-
                    // split-design.md §7.
                    int depth = 1;
                    if (auto it = attrs.find(Markoff::AttrNames::BlockQuoteDepth);
                        it != attrs.end()
                        && std::holds_alternative<int>(*it))
                        depth = qMax(1, std::get<int>(*it));
                    applyBlockquote(blkCursor, depth, m_fontScale);
                } else if (kind == Markoff::BlockKind::ListItem) {
                    // Read structural attrs from the model rather than guessing
                    // from buffer text — the harvested ListItem buffer is
                    // post-marker content with no marker syntax to recover.
                    // `attrs` is the outer lookup also consumed by computeBlockHash.
                    int depth = 0;
                    if (auto it = attrs.find(Markoff::AttrNames::IndentLevel);
                        it != attrs.end()
                        && std::holds_alternative<int>(*it))
                        depth = std::get<int>(*it);
                    QString markerStyle;
                    if (auto it = attrs.find(Markoff::AttrNames::MarkerStyle);
                        it != attrs.end()
                        && std::holds_alternative<QString>(*it))
                        markerStyle = std::get<QString>(*it);
                    bool checked = false;
                    if (auto it = attrs.find(Markoff::AttrNames::Checked);
                        it != attrs.end()
                        && std::holds_alternative<bool>(*it))
                        checked = std::get<bool>(*it);
                    applyListItem(blkCursor, depth, markerStyle, checked, m_fontScale);
                } else if (kind == Markoff::BlockKind::HorizontalRule) {
                    applyHorizontalRule(blkCursor, m_fontScale);
                } else {
                    applyParagraph(blkCursor, m_fontScale);
                }

                // BlockQuote depth overlay (queue #8.1): non-BlockQuote
                // inner kinds (Heading, CodeBlock, ListItem, ...) inside
                // a quote get an additive left-margin so the user reads
                // them as quoted regardless of native styling. Matches
                // applyBlockquote's emPt(fontScale) x depth shape. Spec
                // docs/specs/2026-05-29-blockquote-multi-paragraph-split-design.md §7.
                if (kind != Markoff::BlockKind::BlockQuote) {
                    int overlayDepth = 0;
                    if (auto it = attrs.find(Markoff::AttrNames::BlockQuoteDepth);
                        it != attrs.end()
                        && std::holds_alternative<int>(*it))
                        overlayDepth = std::get<int>(*it);
                    if (overlayDepth > 0) {
                        QTextBlockFormat bf = blkCursor.blockFormat();
                        bf.setLeftMargin(bf.leftMargin()
                                         + emPt(m_fontScale) * overlayDepth);
                        blkCursor.setBlockFormat(bf);
                    }
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
            }  // end !hashSkipped format pass

            // List-membership reconciliation runs on EVERY block (hash-skipped
            // or not). Cross-block continuity depends on neighbour state, not
            // per-block content hashes — see spec
            // docs/specs/2026-05-29-styled-ordered-list-continuous-numbering-design.md.
            const QTextBlock listBlk = m_textDocument->findBlock(startQt);
            manageListMembership(listBlk, kind, attrs, listStack);

            // Advance past block content + single-"\n" separator between blocks.
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

    // Detect structural change: did any block ID disappear or appear?
    bool structural = previousBlockIds.size() != currentIds.size();
    if (!structural && !previousBlockIds.isEmpty()) {
        for (auto it = currentIds.constBegin();
             it != currentIds.constEnd(); ++it) {
            if (!previousBlockIds.contains(it.key())) {
                structural = true;
                break;
            }
        }
    }
    // Deferred scroll restore: synchronous setValue would be overridden by
    // Qt's post-endEditBlock layout signals (ensureCursorVisible etc.).
    // Defer one event-loop tick so we land after Qt settles.
    //
    // Skip restore on: structural changes (block set changed — natural scroll
    // is correct); first pass (previousBlockIds empty — cursor at start,
    // scroll at top is correct); no scroll handle available.
    if (!structural && !previousBlockIds.isEmpty() && savedScroll >= 0
        && m_textEdit) {
        QPointer<QTextEdit> editPtr = m_textEdit;
        QTimer::singleShot(0, this, [editPtr, savedScroll]() {
            if (editPtr && editPtr->verticalScrollBar()) {
                editPtr->verticalScrollBar()->setValue(savedScroll);
            }
        });
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
