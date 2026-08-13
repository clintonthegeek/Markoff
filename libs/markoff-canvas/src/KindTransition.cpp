// SPDX-License-Identifier: GPL-3.0-or-later
#include "KindTransition.h"

#include <QRegularExpression>

namespace Markoff::Canvas::Detail {

int countLeadingHashes(const QString &text)
{
    int n = 0;
    while (n < text.size() && n < 6 && text[n] == u'#') ++n;
    if (n == 0) return 0;
    // Must be followed by space or end of string
    if (n < text.size() && text[n] != u' ' && text[n] != u'\n') return 0;
    return n;
}

int matchesSetextShape(const QString &text)
{
    // Strip any trailing newlines first: a soft-break-then-underline-then-
    // soft-break sequence produces e.g. "Heading\n=\n". Without trimming,
    // lastIndexOf('\n') would point at the trailing newline and the tail
    // would be empty, missing the underline. Trim only the trailing run of
    // newlines so internal structure (the content line(s) and the
    // underline) is preserved.
    int endIdx = text.size();
    while (endIdx > 0 && text[endIdx - 1] == u'\n') --endIdx;
    const QString trimmed = text.left(endIdx);

    const int lastNl = trimmed.lastIndexOf(u'\n');
    if (lastNl < 0) return 0;                         // single-line buffer, can't be setext
    if (lastNl == 0) return 0;                        // nothing before the newline; no content line

    // Underline candidate = substring after the last newline.
    const QString tail = trimmed.mid(lastNl + 1);
    static const QRegularExpression underlineRe(
        QStringLiteral("^[ \\t]{0,3}(=+|-+)[ \\t]*$"));
    auto m = underlineRe.match(tail);
    if (!m.hasMatch()) return 0;
    const QChar uchar = m.captured(1).at(0);
    const int level = (uchar == u'=') ? 1 : 2;

    // Find the line directly above the underline; it must be non-blank.
    // Search strictly before lastNl (from lastNl-1) to avoid finding lastNl itself.
    const int prevNl = (lastNl >= 2) ? trimmed.lastIndexOf(u'\n', lastNl - 1) : -1;
    const QString aboveLine = (prevNl < 0)
        ? trimmed.left(lastNl)
        : trimmed.mid(prevNl + 1, lastNl - prevNl - 1);
    if (aboveLine.trimmed().isEmpty()) return 0;

    return level;
}

Markoff::BlockKind inferBlockKind(const QString &text)
{
    if (text.isEmpty())
        return Markoff::BlockKind::Paragraph;

    // Heading: 1-6 '#' followed by space or EOL
    if (countLeadingHashes(text) > 0)
        return Markoff::BlockKind::Heading;

    // CodeBlock: starts with ``` or ~~~ (3+ chars)
    if ((text.startsWith(QStringLiteral("```")) && text.size() >= 3) ||
        (text.startsWith(QStringLiteral("~~~")) && text.size() >= 3))
        return Markoff::BlockKind::CodeBlock;

    // Setext heading: text + \n + underline. Checked before bare-`---`-HR
    // so `Heading\n---` wins over `---` alone.
    if (matchesSetextShape(text) > 0)
        return Markoff::BlockKind::Heading;

    // HorizontalRule: trimmed is ---, ***, or ___ (single-line only).
    // Only applied when the buffer has no newline, to avoid misclassifying
    // `\n---` (blank-preceded underline) as HR.
    if (!text.contains(u'\n')) {
        const QString trimmed = text.trimmed();
        if (trimmed == QStringLiteral("---") ||
            trimmed == QStringLiteral("***") ||
            trimmed == QStringLiteral("___"))
            return Markoff::BlockKind::HorizontalRule;
    }

    // Image: starts with ![
    if (text.startsWith(QStringLiteral("![")))
        return Markoff::BlockKind::Image;

    // Math: $$ or $ prefix (longest-match first). Display-mode attr not
    // wired here — see header comment.
    if (text.startsWith(QStringLiteral("$$")) || text.startsWith(u'$'))
        return Markoff::BlockKind::Math;

    // ListItem: [-*+] followed by space, or \d+[.)]\s
    {
        static const QRegularExpression listRe(
            QStringLiteral("^[ \\t]{0,3}([-*+]|\\d+[.)])\\s"));
        if (listRe.match(text).hasMatch())
            return Markoff::BlockKind::ListItem;
    }

    // Blockquote: starts with "> " or is exactly ">"
    if (text.startsWith(QStringLiteral("> ")) || text == QStringLiteral(">"))
        return Markoff::BlockKind::BlockQuote;

    return Markoff::BlockKind::Paragraph;
}

}  // namespace Markoff::Canvas::Detail
