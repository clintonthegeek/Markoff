// SPDX-License-Identifier: GPL-3.0-or-later
#include "KindTransition.h"
#include <markoff/live/BlockKind.h>
#include <QRegularExpression>

namespace Markoff::Live {

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
    const int lastNl = text.lastIndexOf(u'\n');
    if (lastNl < 0) return 0;                         // single-line buffer, can't be setext

    // Underline candidate = substring after the last newline.
    const QString tail = text.mid(lastNl + 1);
    static const QRegularExpression underlineRe(
        QStringLiteral("^[ \\t]{0,3}(=+|-+)[ \\t]*$"));
    auto m = underlineRe.match(tail);
    if (!m.hasMatch()) return 0;
    const QChar uchar = m.captured(1).at(0);
    const int level = (uchar == u'=') ? 1 : 2;

    // Find the line directly above the underline; it must be non-blank.
    const int prevNl = text.lastIndexOf(u'\n', lastNl - 1);
    const QString aboveLine = (prevNl < 0)
        ? text.left(lastNl)
        : text.mid(prevNl + 1, lastNl - prevNl - 1);
    if (aboveLine.trimmed().isEmpty()) return 0;

    return level;
}

QString inferBlockKind(const QString &text, bool *displayMode)
{
    if (text.isEmpty())
        return BlockKind::Paragraph;

    // Heading: 1–6 '#' followed by space or EOL
    if (countLeadingHashes(text) > 0)
        return BlockKind::Heading;

    // CodeBlock: starts with ``` or ~~~ (3+ chars)
    if ((text.startsWith(QStringLiteral("```")) && text.size() >= 3) ||
        (text.startsWith(QStringLiteral("~~~")) && text.size() >= 3))
        return BlockKind::CodeBlock;

    // HorizontalRule: trimmed is ---, ***, or ___
    {
        const QString trimmed = text.trimmed();
        if (trimmed == QStringLiteral("---") ||
            trimmed == QStringLiteral("***") ||
            trimmed == QStringLiteral("___"))
            return BlockKind::HorizontalRule;
    }

    // Image: starts with ![
    if (text.startsWith(QStringLiteral("![")))
        return BlockKind::Image;

    // Math: check $$ before $ (longest-match first)
    if (text.startsWith(QStringLiteral("$$"))) {
        if (displayMode) *displayMode = true;
        return BlockKind::Math;
    }
    if (text.startsWith(u'$')) {
        if (displayMode) *displayMode = false;
        return BlockKind::Math;
    }

    // ListItem: [-*+] followed by space, or \d+[.)]\s
    {
        static const QRegularExpression listRe(
            QStringLiteral("^[ \\t]{0,3}([-*+]|\\d+[.)])\\s"));
        if (listRe.match(text).hasMatch())
            return BlockKind::ListItem;
    }

    // Blockquote: starts with "> " or is exactly ">"
    if (text.startsWith(QStringLiteral("> ")) || text == QStringLiteral(">"))
        return BlockKind::Blockquote;

    return BlockKind::Paragraph;
}

}  // namespace Markoff::Live
