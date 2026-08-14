// SPDX-License-Identifier: GPL-3.0-or-later
#include "CodeHighlighting.h"

#include <QTextCharFormat>

#include <markoff/core/CodeSpan.h>
#include <markoff/core/SyntaxHighlightService.h>
#include <markoff/core/Theme.h>

namespace Markoff::Canvas::Detail {

CodeFenceInfo parseCodeFence(const QByteArray &blockText)
{
    CodeFenceInfo info;

    const int n = blockText.size();
    if (n < 3)
        return info;

    const char fenceChar = blockText.at(0);
    if (fenceChar != '`' && fenceChar != '~')
        return info;

    int fenceLen = 0;
    while (fenceLen < n && blockText.at(fenceLen) == fenceChar)
        ++fenceLen;
    if (fenceLen < 3)
        return info;  // CommonMark fences are >= 3 chars.

    const int firstLineEnd = blockText.indexOf('\n', fenceLen);
    if (firstLineEnd < 0)
        return info;  // No newline at all: not a real fenced block yet.

    const QByteArray infoString = blockText.mid(fenceLen, firstLineEnd - fenceLen).trimmed();
    const int spaceIdx = infoString.indexOf(' ');
    const QByteArray languageToken = (spaceIdx < 0) ? infoString : infoString.left(spaceIdx);
    info.language = QString::fromUtf8(languageToken);

    info.contentStart = firstLineEnd + 1;
    info.contentEnd   = info.contentStart;  // default: no content (fence not yet closed/empty)

    // Closing fence: the buffer's last line, if it is >= 3 repeats of the
    // same fence character (trailing whitespace tolerated, CommonMark
    // allows an indented/blank-trailing close). If the last line isn't a
    // valid close (still-typing case — caret is inside an unterminated
    // fence), treat everything after the opening fence as content so
    // highlighting still applies to what is there.
    const int lastNl = blockText.lastIndexOf('\n');
    const int closingLineStart = (lastNl < 0) ? 0 : lastNl + 1;
    bool closingOk = false;
    if (closingLineStart >= info.contentStart) {
        const QByteArray lastLine = blockText.mid(closingLineStart).trimmed();
        if (lastLine.size() >= 3) {
            closingOk = true;
            for (const char c : lastLine) {
                if (c != fenceChar) { closingOk = false; break; }
            }
        }
    }

    if (closingOk)
        info.contentEnd = lastNl;  // up to (not including) the '\n' before the close
    else
        info.contentEnd = n;  // unterminated: content runs to the end of the buffer

    if (info.contentEnd < info.contentStart)
        info.contentEnd = info.contentStart;

    return info;
}

QList<QTextLayout::FormatRange> codeTokenFormatRanges(
    const QByteArray &blockText, const Markoff::SyntaxHighlightService &service,
    const Markoff::Theme &theme, const ProjectionMap &projection)
{
    QList<QTextLayout::FormatRange> ranges;

    const CodeFenceInfo fence = parseCodeFence(blockText);
    if (fence.language.isEmpty() || fence.contentEnd <= fence.contentStart)
        return ranges;

    const QByteArray content =
        blockText.mid(fence.contentStart, fence.contentEnd - fence.contentStart);

    // Service miss (unrecognized language key): highlight() returns an
    // empty list for this leaf's own def-lookup misses (see
    // Kf6SyntaxHighlightService::highlight — an invalid Definition returns
    // {} before doing any tokenizing), so the loop below simply produces no
    // ranges and the block keeps its plain BlockPresentation monospace
    // color, per spec's "a service miss renders plain monospace".
    const QList<Markoff::CodeSpan> spans = service.highlight(fence.language, content);
    ranges.reserve(spans.size());

    for (const Markoff::CodeSpan &sp : spans) {
        if (sp.length == 0 || sp.kind == Markoff::CodeTokenKind::Default)
            continue;

        const QColor color = theme.colorForCodeToken(sp.kind);
        if (!color.isValid())
            continue;  // Theme has no color for this token slot — leave monospace as-is.

        const int absStart = fence.contentStart + int(sp.offset);
        const int absEnd   = absStart + int(sp.length);
        const int layoutStart = projection.byteToLayoutQChar(absStart);
        const int layoutEnd   = projection.byteToLayoutQChar(absEnd);
        if (layoutEnd <= layoutStart)
            continue;

        QTextCharFormat fmt;
        fmt.setForeground(color);
        ranges.push_back({layoutStart, layoutEnd - layoutStart, fmt});
    }

    return ranges;
}

}  // namespace Markoff::Canvas::Detail
