// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-foundation/CompletionDetector.h>

#include <markoff-foundation/MarkoffDocument.h>

namespace Markoff {

namespace {
bool isIdentChar(QChar c) {
    return c.isLetterOrNumber() || c == '_' || c == '-';
}
}

CompletionContext
CompletionDetector::detect(const MarkoffDocument *doc,
                            const CollabText::Crdt::Anchor &cursor)
{
    CompletionContext ctx;
    ctx.cursorAnchor = cursor;
    if (!doc) return ctx;

    const QString text = doc->toMarkdown();
    const quint32 byteOff = doc->resolveAnchor(cursor);
    // Convert byte offset to UTF-16 index.
    const QByteArray prefixBytes = doc->toMarkdownUtf8().left(byteOff);
    const int u16cur = QString::fromUtf8(prefixBytes).size();

    // Scan back from u16cur to a trigger char or whitespace.
    int i = u16cur;
    while (i > 0 && isIdentChar(text.at(i - 1))) --i;
    const QString prefix = text.mid(i, u16cur - i);
    if (i == 0) return ctx;
    const QChar trig = text.at(i - 1);

    if (trig == ':') {
        ctx.trigger = CompletionTrigger::Emoji;
        ctx.prefix  = prefix;
    } else if (trig == '[' && i >= 2 && text.at(i - 2) == '[') {
        ctx.trigger = CompletionTrigger::WikiLink;
        ctx.prefix  = prefix;
    } else if (trig == '#') {
        // Heading marker if at line start; tag otherwise.
        bool atLineStart = (i - 1 == 0) || text.at(i - 2) == '\n';
        if (!atLineStart) {
            ctx.trigger = CompletionTrigger::Tag;
            ctx.prefix  = prefix;
        }
    }
    return ctx;
}

}  // namespace Markoff
