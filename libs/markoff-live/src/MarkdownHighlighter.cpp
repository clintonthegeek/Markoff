// SPDX-License-Identifier: GPL-3.0-or-later
#include "MarkdownHighlighter.h"

#include <QFont>
#include <QTextDocument>
#include <QTextBlock>
#include <QRegularExpression>

namespace Markoff {

MarkdownHighlighter::MarkdownHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
    , m_theme(Theme::defaultLight())
{
}

void MarkdownHighlighter::setTheme(const Theme &theme)
{
    m_theme = theme;
    rehighlight();
}

void MarkdownHighlighter::setSpanMap(QList<SourceSpan> spans)
{
    m_spans = std::move(spans);
    // Don't call rehighlight() here — the document change that triggered
    // the reparse will also trigger Qt's automatic rehighlight. Calling
    // rehighlight() explicitly would re-modify the document, triggering
    // another contentsChanged → reparse → setSpanMap → infinite loop.
}

void MarkdownHighlighter::setCursorPosition(int blockNumber, int columnInBlock)
{
    bool blockChanged = (m_cursorBlock != blockNumber);
    bool columnChanged = (m_cursorColumn != columnInBlock);
    if (!blockChanged && !columnChanged) return;

    int oldBlock = m_cursorBlock;
    m_cursorBlock = blockNumber;
    m_cursorColumn = columnInBlock;

    QTextDocument *doc = document();
    if (blockChanged && oldBlock >= 0) {
        QTextBlock b = doc->findBlockByNumber(oldBlock);
        if (b.isValid()) rehighlightBlock(b);
    }
    {
        QTextBlock b = doc->findBlockByNumber(blockNumber);
        if (b.isValid()) rehighlightBlock(b);
    }
}

void MarkdownHighlighter::hideRange(int start, int length)
{
    QTextCharFormat hidden;
    hidden.setForeground(Qt::transparent);
    // PercentageSpacing(1) ≈ 1% of glyph advance → near-zero total width.
    // -100 produces NEGATIVE advance in Qt6, shifting subsequent text left.
    hidden.setFontLetterSpacing(1);
    hidden.setFontPointSize(1);
    setFormat(start, length, hidden);
}

bool MarkdownHighlighter::cursorInRange(int cursorCol, int matchStart, int matchEnd) const
{
    return cursorCol >= matchStart && cursorCol <= matchEnd;
}

void MarkdownHighlighter::applySpanFormat(const SourceSpan &span,
                                           int blockCharStart, int blockCharEnd,
                                           bool shouldHideDelim, int cursorCol)
{
    // Compute the portion of this span that overlaps the current block
    int spanCharStart = span.charOffset;
    int spanCharEnd = span.charOffset + span.charLength;

    // Clip to block range
    int localStart = qMax(spanCharStart, blockCharStart) - blockCharStart;
    int localEnd = qMin(spanCharEnd, blockCharEnd) - blockCharStart;
    if (localStart >= localEnd || localStart < 0)
        return;
    int localLen = localEnd - localStart;

    static const Element headingElements[6] = {
        Element::H1, Element::H2, Element::H3,
        Element::H4, Element::H5, Element::H6
    };

    // Delimiter spans
    if (span.isDelimiter) {
        bool hide = shouldHideDelim;
        // Per-element: on cursor line, only hide if cursor is NOT within
        // the parent formatting element's range (not just adjacent to
        // this specific delimiter character)
        if (!hide && cursorCol >= 0) {
            if (span.parentCharStart >= 0 && span.parentCharEnd >= 0) {
                // Use parent element range: show delimiters when cursor
                // is anywhere between opening and closing delimiters
                int parentLocalStart = span.parentCharStart - blockCharStart;
                int parentLocalEnd = span.parentCharEnd - blockCharStart;
                hide = !cursorInRange(cursorCol, parentLocalStart, parentLocalEnd);
            } else {
                // No parent range (block-level delimiters like ##)
                hide = !cursorInRange(cursorCol, localStart, localEnd);
            }
        }

        if (hide) {
            hideRange(localStart, localLen);
        } else {
            // Source mode or cursor-adjacent: show delimiter with context coloring
            if (span.isHeading && span.headingLevel >= 1 && span.headingLevel <= 6) {
                setFormat(localStart, localLen,
                          m_theme.formats.value(headingElements[span.headingLevel - 1]));
            } else if (span.code) {
                setFormat(localStart, localLen, m_theme.formats.value(Element::InlineCode));
            } else if (span.bold && span.italic) {
                QTextCharFormat fmt;
                fmt.setFontWeight(700);
                fmt.setFontItalic(true);
                setFormat(localStart, localLen, fmt);
            } else if (span.bold) {
                setFormat(localStart, localLen, m_theme.formats.value(Element::Bold));
            } else if (span.italic) {
                setFormat(localStart, localLen, m_theme.formats.value(Element::Italic));
            } else if (span.strikethrough) {
                setFormat(localStart, localLen, m_theme.formats.value(Element::Strikethrough));
            } else if (span.math || span.mathDisplay) {
                setFormat(localStart, localLen, m_theme.formats.value(Element::Math));
            } else if (span.highlight) {
                setFormat(localStart, localLen, m_theme.formats.value(Element::Highlight));
            } else if (span.comment) {
                setFormat(localStart, localLen, m_theme.formats.value(Element::Comment));
            } else if (span.isBlockquoteMarker) {
                setFormat(localStart, localLen, m_theme.formats.value(Element::BlockQuote));
            } else if (span.isWikilink) {
                setFormat(localStart, localLen, m_theme.formats.value(Element::WikiLink));
            } else if (span.isLink) {
                setFormat(localStart, localLen, m_theme.formats.value(Element::Link));
            }
        }
        return;
    }

    // Content spans: apply formatting by merging onto existing format
    // (so nested formatting accumulates: bold + code = bold monospace)
    // Skip spans with no formatting flags — they're plain text and would
    // overwrite formatting already applied by other spans.
    bool hasAnyFormat = span.bold || span.italic || span.strikethrough ||
        span.code || span.math || span.mathDisplay || span.highlight ||
        span.comment || span.isTag || span.isLink || span.isWikilink ||
        span.isImage || span.isHeading || span.isHorizontalRule ||
        span.isListMarker || span.isBlockquoteMarker || span.isFrontmatter ||
        span.isBlockquote || span.isFootnoteRef || span.isCodeBlockContent;
    if (!hasAnyFormat)
        return;

    // Cluster J phase 3: pre-compute the wikilink anchor href once per
    // content span (not per-character) so the per-character loop can
    // stamp it cheaply. A `wikilink://` scheme lets Editor's LinkRenderer
    // bridge distinguish internal wikilinks from standard markdown URLs.
    QString wikilinkHref;
    if (span.isWikilink && !span.isDelimiter) {
        const QString full = currentBlock().text().mid(
            localStart, localEnd - localStart);
        const int pipeIdx = full.indexOf(QLatin1Char('|'));
        const QString target = (pipeIdx >= 0) ? full.left(pipeIdx) : full;
        if (!target.isEmpty())
            wikilinkHref = QStringLiteral("wikilink://") + target;
    }

    for (int i = localStart; i < localEnd; ++i) {
        QTextCharFormat fmt = format(i);

        if (span.isHeading && span.headingLevel >= 1 && span.headingLevel <= 6) {
            const auto &hfmt = m_theme.formats.value(headingElements[span.headingLevel - 1]);
            fmt.setFontWeight(hfmt.fontWeight());
            fmt.setFontPointSize(hfmt.fontPointSize());
            fmt.setForeground(hfmt.foreground());
        }

        if (span.bold)
            fmt.setFontWeight(700);
        if (span.italic)
            fmt.setFontItalic(true);
        if (span.strikethrough)
            fmt.setFontStrikeOut(true);
        if (span.code)
            fmt.merge(m_theme.formats.value(Element::InlineCode));
        if (span.math || span.mathDisplay)
            fmt.setForeground(m_theme.formats.value(Element::Math).foreground());
        if (span.highlight)
            fmt.setBackground(m_theme.formats.value(Element::Highlight).background());
        if (span.comment) {
            fmt.setForeground(m_theme.formats.value(Element::Comment).foreground());
            fmt.setFontItalic(true);
        }
        if (span.isTag)
            fmt.setForeground(m_theme.formats.value(Element::Tag).foreground());
        if (span.isLink)
            fmt.merge(m_theme.formats.value(Element::Link));
        if (span.isWikilink) {
            fmt.merge(m_theme.formats.value(Element::WikiLink));
            if (!wikilinkHref.isEmpty()) {
                fmt.setAnchor(true);
                fmt.setAnchorHref(wikilinkHref);
            }
        }
        if (span.isHorizontalRule) {
            // Make text transparent — decoration painter draws the line
            fmt.setForeground(Qt::transparent);
        }
        if (span.isFootnoteRef) {
            // Hide the ^ character (first char) and superscript the number
            hideRange(localStart, 1); // hide ^
            const QTextCharFormat refFmt = m_theme.formats.value(Element::FootnoteRef);
            const QColor refColor = refFmt.foreground().color();
            for (int j = localStart + 1; j < localEnd; ++j) {
                QTextCharFormat sfmt = format(j);
                sfmt.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
                sfmt.setForeground(refColor);
                setFormat(j, 1, sfmt);
            }
            return;
        }
        if (span.isListMarker && !span.isTaskMarker)
            fmt.setForeground(m_theme.formats.value(Element::ListMarker).foreground());
        if (span.isCodeBlockContent)
            fmt.setFontFamilies(m_theme.codeFont.families());
        if (span.isBlockquote && !span.isHeading && !span.bold && !span.italic)
            fmt.setForeground(m_theme.formats.value(Element::BlockQuote).foreground());

        setFormat(i, 1, fmt);
    }
}

// Helper class for KSyntaxHighlighting integration
class CodeBlockHighlighter : public KSyntaxHighlighting::AbstractHighlighter {
public:
    struct Run { int start, length; QColor color; bool bold, italic; };
    QList<Run> runs;
    QString m_line;

    KSyntaxHighlighting::State processLine(const QString &text,
                                            const KSyntaxHighlighting::State &state) {
        m_line = text;
        runs.clear();
        return highlightLine(text, state);
    }
protected:
    void applyFormat(int offset, int length,
                     const KSyntaxHighlighting::Format &fmt) override {
        Run r;
        r.start = offset;
        r.length = length;
        r.color = fmt.isDefaultTextStyle(theme())
                      ? QColor(0x33, 0x33, 0x33)
                      : fmt.textColor(theme());
        r.bold = fmt.isBold(theme());
        r.italic = fmt.isItalic(theme());
        runs.append(r);
    }
};

void MarkdownHighlighter::highlightCodeBlock(const QString &text,
                                              const DecoratedRange &dr, int blockNum)
{
    if (dr.language.isEmpty())
        return;

    auto def = m_syntaxRepo.definitionForName(dr.language);
    if (!def.isValid())
        def = m_syntaxRepo.definitionForFileName(QStringLiteral("file.") + dr.language);
    if (!def.isValid())
        return;

    // Use block state to carry KSyntaxHighlighting state across lines.
    // We store a state index in the QTextBlock's userState. For simplicity,
    // we re-highlight from the first code line to the current one.
    // This is O(n) per block but code blocks are typically short.
    CodeBlockHighlighter hl;
    hl.setDefinition(def);
    hl.setTheme(m_syntaxRepo.defaultTheme(KSyntaxHighlighting::Repository::LightTheme));

    KSyntaxHighlighting::State state;
    QTextBlock b = document()->findBlockByNumber(dr.firstBlock + 1); // skip opening fence
    while (b.isValid() && b.blockNumber() < blockNum) {
        state = hl.processLine(b.text(), state);
        b = b.next();
    }

    // Now highlight the current line
    state = hl.processLine(text, state);
    for (const auto &run : hl.runs) {
        QTextCharFormat fmt = format(run.start);
        fmt.setForeground(run.color);
        if (run.bold) fmt.setFontWeight(QFont::Bold);
        if (run.italic) fmt.setFontItalic(true);
        setFormat(run.start, run.length, fmt);
    }
}

void MarkdownHighlighter::highlightBlock(const QString &text)
{
    if (text.isEmpty())
        return;



    // Skip table cell blocks — QTextDocumentLayout renders table content
    // natively. Applying markdown span formatting to cell blocks can make
    // text invisible (delimiter hiding) or apply wrong styles.
    {
        const auto frames = currentBlock().document()->rootFrame()->childFrames();
        int pos = currentBlock().position();
        for (auto *f : frames) {
            if (pos >= f->firstPosition() && pos <= f->lastPosition())
                return;
        }
    }

    const int blockNum = currentBlock().blockNumber();
    const int blockPos = currentBlock().position();
    const int blockLen = text.length();

    // Determine cursor-related visibility (always live-preview behavior)
    bool isCursorLine = (blockNum == m_cursorBlock);
    bool hideDelimiters = !isCursorLine;
    int cursorCol = isCursorLine ? m_cursorColumn : -1;


    // Find all spans that overlap this block's character range
    int blockCharStart = blockPos;
    int blockCharEnd = blockPos + blockLen;

    // Track blockquote depth for this block (for indentation)
    int maxBlockquoteDepth = 0;

    for (const SourceSpan &span : m_spans) {
        int spanEnd = span.charOffset + span.charLength;
        if (spanEnd <= blockCharStart)
            continue;
        if (span.charOffset >= blockCharEnd)
            break;  // spans are sorted by offset

        if (span.blockquoteDepth > maxBlockquoteDepth)
            maxBlockquoteDepth = span.blockquoteDepth;

        applySpanFormat(span, blockCharStart, blockCharEnd, hideDelimiters, cursorCol);
    }

    // Make table blocks transparent (embedded QTableWidget renders on top)
    if (!isCursorLine) {
        for (const DecoratedRange &dr : m_decoratedRanges) {
            if (dr.type == DecoratedRange::Table &&
                blockNum >= dr.firstBlock && blockNum <= dr.lastBlock) {
                QTextCharFormat transparentFmt;
                transparentFmt.setForeground(Qt::transparent);
                setFormat(0, text.length(), transparentFmt);
                return; // skip all other formatting for this block
            }
        }
    }

    // Syntax highlighting for code block content
    for (const DecoratedRange &dr : m_decoratedRanges) {
        if (dr.type == DecoratedRange::CodeBlock &&
            blockNum > dr.firstBlock && blockNum < dr.lastBlock) {
            // This is a content line inside a code block
            highlightCodeBlock(text, dr, blockNum);
            break;
        }
    }

    // Callout first line: hide the [!type] marker and style the title.
    // Works like bold — delimiters hidden, content styled. Click on title
    // reveals the [!type] prefix (same shift behavior as ** for bold).
    {
        for (const DecoratedRange &dr : m_decoratedRanges) {
            if (dr.type == DecoratedRange::Callout && blockNum == dr.firstBlock) {
                static const QRegularExpression calloutMarkerRe(
                    QStringLiteral(R"(\[!(\w+)\]([+-])?\s*)"));
                auto match = calloutMarkerRe.match(text);
                if (match.hasMatch()) {
                    int markerStart = match.capturedStart();
                    int markerLen = match.capturedLength();
                    int titleStart = match.capturedEnd();
                    int titleLen = text.length() - titleStart;
                    bool hasCustomTitle = titleLen > 0;

                    if (isCursorLine) {
                        // Cursor on line: show everything raw (like bold when cursor is in it)
                        // Don't hide anything — raw markdown is visible
                    } else if (hasCustomTitle) {
                        // Has custom title: hide [!type] marker, style title
                        hideRange(markerStart, markerLen);
                        QTextCharFormat titleFmt;
                        titleFmt.setForeground(dr.calloutColor);
                        titleFmt.setFontWeight(QFont::Bold);
                        for (int i = titleStart; i < titleStart + titleLen; ++i) {
                            QTextCharFormat fmt = format(i);
                            fmt.merge(titleFmt);
                            setFormat(i, 1, fmt);
                        }
                    } else {
                        // No custom title: hide [! and ], show type name styled
                        // "[!note]" → hide "[!", show "note" styled, hide "]"
                        int typeStart = markerStart + 2; // skip "[!"
                        int typeLen = match.captured(1).length();
                        hideRange(markerStart, 2); // hide "[!"
                        QTextCharFormat typeFmt;
                        typeFmt.setForeground(dr.calloutColor);
                        typeFmt.setFontWeight(QFont::Bold);
                        // Capitalize first letter visually (can't change text,
                        // but the color makes it look intentional)
                        setFormat(typeStart, typeLen, typeFmt);
                        hideRange(typeStart + typeLen,
                                  markerLen - 2 - typeLen); // hide "]" and any +/-/space
                    }
                }
                break;
            }
        }
    }

    // Note: blockquote indentation is handled by Editor::applyBlockFormats()
    // because QSyntaxHighlighter can only set character formats, not block formats.
}

void MarkdownHighlighter::adjustSpanOffsets(int editPos, int charsRemoved, int charsAdded)
{
    int delta = charsAdded - charsRemoved;
    if (delta == 0) return;

    for (auto &span : m_spans) {
        int spanEnd = span.charOffset + span.charLength;

        // Span entirely before edit — unchanged
        if (spanEnd <= editPos)
            continue;

        // Span entirely after the removed region — shift
        if (span.charOffset >= editPos + charsRemoved) {
            span.charOffset += delta;
            if (span.parentCharStart >= editPos + charsRemoved) {
                span.parentCharStart += delta;
                span.parentCharEnd += delta;
            }
            continue;
        }

        // Span overlaps the edit — adjust length
        if (charsRemoved == 0) {
            // Pure insertion inside this span: grow it
            span.charLength += charsAdded;
            if (span.parentCharEnd >= editPos)
                span.parentCharEnd += charsAdded;
        } else {
            // Deletion/replacement overlaps this span
            int overlapStart = qMax(span.charOffset, editPos);
            int overlapEnd = qMin(spanEnd, editPos + charsRemoved);
            int removed = overlapEnd - overlapStart;
            span.charLength = qMax(0, span.charLength - removed + (span.charOffset <= editPos ? charsAdded : 0));
            if (span.charOffset > editPos)
                span.charOffset = editPos + charsAdded;
        }
    }
}

} // namespace Markoff
