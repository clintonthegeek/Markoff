// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/view/qml/InlineFormatHighlighter.h>

#include <markoff/view/qml/LiveProjectionLayer.h>
#include <markoff/view/qml/ProjectionItem.h>

#include <markoff-parser/TreeSitterParser.h>

#include <QBitArray>
#include <QColor>
#include <QFont>
#include <QQuickTextDocument>
#include <QTextBlock>
#include <QTextCharFormat>

#include <algorithm>

namespace Markoff::View::Qml {

InlineFormatHighlighter::InlineFormatHighlighter(QObject *parent)
    : QSyntaxHighlighter(parent)
{
}

QQuickTextDocument *InlineFormatHighlighter::quickDocument() const
{
    return m_quickDoc;
}

void InlineFormatHighlighter::setQuickDocument(QQuickTextDocument *doc)
{
    if (m_quickDoc == doc) return;
    m_quickDoc = doc;
    if (m_quickDoc) {
        setDocument(m_quickDoc->textDocument());
        rehighlight();  // apply pre-populated spans immediately
    } else
        setDocument(nullptr);
    Q_EMIT quickDocumentChanged();
}

QString InlineFormatHighlighter::source() const
{
    return m_source;
}

void InlineFormatHighlighter::setSource(const QString &src)
{
    if (m_source == src) return;
    m_source = src;
    Q_EMIT sourceChanged();
    rebuildSpans();
    publishInlinePredictions(m_source);
    rehighlight();
}

LiveProjectionLayer *InlineFormatHighlighter::projectionLayer() const
{
    return m_layer;
}

void InlineFormatHighlighter::setProjectionLayer(LiveProjectionLayer *layer)
{
    if (m_layer == layer) return;
    if (m_layer) {
        m_layer->clearInlinePredictionsForRow(m_blockIndex);
    }
    m_layer = layer;
    // Re-publish current predictions into the new layer so the registry
    // matches the highlighter's view of the world.
    if (m_layer) {
        for (const InlinePrediction &p : m_fallbackPredictions) {
            m_layer->createInlinePrediction(p);
        }
    }
    Q_EMIT projectionLayerChanged();
}

int InlineFormatHighlighter::blockIndex() const
{
    return m_blockIndex;
}

void InlineFormatHighlighter::setBlockIndex(int idx)
{
    if (m_blockIndex == idx) return;
    if (m_layer && m_blockIndex >= 0) {
        m_layer->clearInlinePredictionsForRow(m_blockIndex);
    }
    m_blockIndex = idx;
    // Re-publish predictions under the new row key so the layer's per-row
    // index reflects this delegate's current model index.
    if (m_layer) {
        for (InlinePrediction p : m_fallbackPredictions) {
            p.row = m_blockIndex;
            m_layer->createInlinePrediction(p);
        }
    }
    Q_EMIT blockIndexChanged();
}

void InlineFormatHighlighter::rebuildSpans()
{
    m_spans.clear();
    if (m_source.isEmpty()) return;

    Markoff::TreeSitterParser parser;
    if (!parser.parse(m_source))
        return;

    m_spans = parser.buildSpanMap();
}

void InlineFormatHighlighter::publishInlinePredictions(const QString &source)
{
    // Producer logic: scan for unclosed inline delimiters and turn each into
    // an `InlinePrediction`. The layer is the registry; this method is the
    // sole producer for inline predictions emitted by the highlighter.

    // Drop any previously-registered predictions for this row before
    // republishing — the source has just changed, so prior predictions are
    // stale.
    m_fallbackPredictions.clear();
    if (m_layer) {
        m_layer->clearInlinePredictionsForRow(m_blockIndex);
    }

    if (source.isEmpty()) return;

    // Collect confirmed code-span ranges to avoid speculating inside code.
    QList<QPair<int,int>> codeRanges;
    for (const SourceSpan &s : m_spans) {
        if (s.code && !s.isDelimiter && s.charLength > 0)
            codeRanges.append({s.charOffset, s.charOffset + s.charLength});
    }
    auto inCodeRange = [&](int pos) {
        for (const auto &r : codeRanges)
            if (pos >= r.first && pos < r.second) return true;
        return false;
    };

    // Process delimiters longest-first. Track consumed positions.
    const int len = source.length();
    QBitArray consumed(len, false);

    struct DelimSpec { QString d; bool bold, italic, code, strike, highlight; };
    const DelimSpec specs[] = {
        { QStringLiteral("**"), true,  false, false, false, false },
        { QStringLiteral("__"), true,  false, false, false, false },
        { QStringLiteral("~~"), false, false, false, true,  false },
        { QStringLiteral("=="), false, false, false, false, true  },
        { QStringLiteral("*"),  false, true,  false, false, false },
        { QStringLiteral("_"),  false, true,  false, false, false },
        { QStringLiteral("`"),  false, false, true,  false, false },
    };

    for (const DelimSpec &spec : specs) {
        const int dlen = spec.d.length();
        // Find all non-consumed, non-code-range occurrences.
        QList<int> positions;
        for (int i = 0; i <= len - dlen; ++i) {
            if (consumed[i]) continue;
            if (inCodeRange(i)) continue;
            if (QStringView(source).mid(i, dlen) == spec.d) {
                // Mark as consumed.
                for (int j = 0; j < dlen; ++j) consumed[i + j] = true;
                positions.append(i);
            }
        }
        if (positions.isEmpty() || positions.size() % 2 == 0) continue;
        // Odd count → unclosed opener. The last position is the unclosed one.
        const int openerStart = positions.last();
        const int contentStart = openerStart + dlen;
        if (contentStart >= len) continue;  // opener at very end, nothing to style

        QTextCharFormat fmt;
        if (spec.bold)      fmt.setFontWeight(QFont::Bold);
        if (spec.italic)    fmt.setFontItalic(true);
        if (spec.code)    { fmt.setFontFamilies({ QStringLiteral("Monospace") });
                            fmt.setBackground(QColor(0xf0, 0xf0, 0xf0)); }
        if (spec.strike)    fmt.setFontStrikeOut(true);
        if (spec.highlight) fmt.setBackground(QColor(0xff, 0xff, 0x00));

        InlinePrediction pred;
        pred.row       = m_blockIndex;
        pred.charStart = contentStart;
        pred.charEnd   = len;
        pred.format    = fmt;

        m_fallbackPredictions.append(pred);
        if (m_layer) {
            m_layer->createInlinePrediction(pred);
        }
    }
}

QList<InlinePrediction> InlineFormatHighlighter::currentPredictions() const
{
    if (m_layer && m_blockIndex >= 0) {
        const QList<InlinePrediction> fromLayer =
            m_layer->predictionsForRow(m_blockIndex);
        if (!fromLayer.isEmpty()) return fromLayer;
    }
    return m_fallbackPredictions;
}

void InlineFormatHighlighter::highlightBlock(const QString &text)
{
    // QSyntaxHighlighter::highlightBlock is invoked once per QTextBlock
    // (per visual line in our multi-line paragraph delegates, since
    // Stage C-2/C-3 made BlockRecord.text source-faithful so a paragraph
    // with internal '\n's becomes multiple QTextBlocks). Predictions and
    // spans are computed against whole-block source positions, so each
    // setFormat call must be translated by the current QTextBlock's
    // start position before the call (setFormat's `start` is relative to
    // the QTextBlock, not the QTextDocument).
    const int blockStart = currentBlock().position();
    const int textLen    = static_cast<int>(text.length());
    const int blockEnd   = blockStart + textLen;

    auto applyRange = [&](int absStart, int absEnd, const QTextCharFormat &fmt) {
        if (absEnd <= blockStart || absStart >= blockEnd) return;
        const int localStart = std::max(0, absStart - blockStart);
        const int localEnd   = std::min(textLen, absEnd - blockStart);
        if (localEnd <= localStart) return;
        setFormat(localStart, localEnd - localStart, fmt);
    };

    // Apply speculative formats first so confirmed spans can overwrite them.
    // Predictions live in the projection layer (Stage 3); we consume them
    // back rather than scanning here. The fallback path (no layer wired)
    // uses the internal `m_fallbackPredictions` list — same data, same
    // shape.
    for (const InlinePrediction &p : currentPredictions()) {
        applyRange(p.charStart, p.charEnd, p.format);
    }

    if (m_spans.isEmpty()) return;

    // QSyntaxHighlighter is called once per QTextBlock. For single-block
    // delegates (paragraph, heading) there is only one block, so all spans
    // apply here. charOffset in SourceSpan is relative to the full parsed
    // source; since source == the delegate's text for paragraphs, and for
    // headings the source may contain a `# ` prefix that is NOT in the
    // displayed text — but we pass the displayed text as `source`, so the
    // offsets are always relative to what is displayed.
    for (const Markoff::SourceSpan &s : m_spans) {
        if (s.isDelimiter) continue;  // delimiters are hidden in live preview
        // All format branches below can assume !isDelimiter.
        if (s.charLength <= 0) continue;

        QTextCharFormat fmt;
        bool hasFormat = false;

        if (s.bold) {
            fmt.setFontWeight(QFont::Bold);
            hasFormat = true;
        }
        if (s.italic) {
            fmt.setFontItalic(true);
            hasFormat = true;
        }
        if (s.strikethrough) {
            fmt.setFontStrikeOut(true);
            hasFormat = true;
        }
        if (s.code) {
            fmt.setFontFamilies({ QStringLiteral("Monospace") });
            fmt.setBackground(QColor(0xf0, 0xf0, 0xf0));
            hasFormat = true;
        }
        if (s.highlight) {
            fmt.setBackground(QColor(0xff, 0xff, 0x00));
            hasFormat = true;
        }
        if (s.isLink) {
            fmt.setFontUnderline(true);
            fmt.setForeground(QColor(0x00, 0x66, 0xcc));
            hasFormat = true;
        }

        if (hasFormat)
            applyRange(s.charOffset, s.charOffset + s.charLength, fmt);
    }
}

}  // namespace Markoff::View::Qml
