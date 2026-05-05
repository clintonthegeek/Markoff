// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-foundation/CompletionDetector.h>

#include <markoff-foundation/MarkoffDocument.h>

namespace Markoff {

namespace {
bool isIdentChar(QChar c) {
    return c.isLetterOrNumber() || c == '_' || c == '-';
}

bool insideFencedCodeBlock(const QString &text, int u16cur) {
    int fences = 0;
    int i = 0;
    while (i + 2 < u16cur) {
        if (text.at(i) == '`' && text.at(i + 1) == '`' && text.at(i + 2) == '`'
            && (i == 0 || text.at(i - 1) == '\n'))
            ++fences;
        ++i;
    }
    return fences % 2 == 1;
}

bool isEscaped(const QString &text, int idx) {
    int n = 0;
    for (int j = idx - 1; j >= 0 && text.at(j) == '\\'; --j) ++n;
    return n % 2 == 1;
}
}

CompletionContext
CompletionDetector::detect(const MarkoffDocument *doc,
                            TextAnchor cursor)
{
    CompletionContext ctx;
    if (!doc) return ctx;

    const quint32 cursorByteOffset = doc->resolveTextAnchor(cursor);

    // Populate cursorBlock + cursorByteOffset from the flat anchor.
    {
        const TextAnchor ta = doc->textAnchorAt(cursorByteOffset, /*rightBias*/ false);
        if (const auto blockId = doc->blockAt(ta)) {
            ctx.cursorBlock      = *blockId;
            ctx.cursorByteOffset = static_cast<uint32_t>(
                doc->offsetInBlock(*blockId, ta));
        }
    }

    const QString text = doc->toMarkdown();
    const QByteArray prefixBytes = doc->toMarkdownUtf8().left(cursorByteOffset);
    const int u16cur = QString::fromUtf8(prefixBytes).size();

    if (insideFencedCodeBlock(text, u16cur)) return ctx;

    int i = u16cur;
    while (i > 0 && isIdentChar(text.at(i - 1))) --i;
    const QString prefix = text.mid(i, u16cur - i);
    if (i == 0) return ctx;
    const QChar trig = text.at(i - 1);

    // Compute trigger flat byte offset and map to block-local position.
    const quint32 triggerFlatByte = static_cast<quint32>(
        text.left(i - 1).toUtf8().size());
    {
        const TextAnchor ta = doc->textAnchorAt(triggerFlatByte, /*rightBias*/ false);
        if (const auto blockId = doc->blockAt(ta)) {
            ctx.triggerBlock      = *blockId;
            ctx.triggerByteOffset = static_cast<uint32_t>(
                doc->offsetInBlock(*blockId, ta));
        }
    }

    if (trig == ':') {
        ctx.trigger = CompletionTrigger::Emoji;
        ctx.prefix  = prefix;
    } else if (trig == '[' && i >= 2 && text.at(i - 2) == '['
               && !isEscaped(text, i - 2))
    {
        ctx.trigger = CompletionTrigger::WikiLink;
        ctx.prefix  = prefix;
    } else if (trig == '^' && i >= 2 && text.at(i - 2) == '['
               && !isEscaped(text, i - 2))
    {
        ctx.trigger = CompletionTrigger::Footnote;
        ctx.prefix  = prefix;
    } else if (trig == '#') {
        const bool atLineStart = (i - 1 == 0) || text.at(i - 2) == '\n';
        if (!atLineStart) {
            ctx.trigger = CompletionTrigger::Tag;
            ctx.prefix  = prefix;
        }
    }
    return ctx;
}

}  // namespace Markoff
