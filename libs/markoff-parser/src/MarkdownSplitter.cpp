// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-parser/MarkdownSplitter.h>
#include <markoff-parser/TreeSitterParser.h>

namespace Markoff {

QList<MarkdownSegment> MarkdownSplitter::split(const QString &markdown,
                                                TreeSitterParser &parser)
{
    QList<MarkdownSegment> segments;

    // Trivial fall-through: empty input or parse failure -> one text segment.
    if (markdown.isEmpty()) {
        MarkdownSegment seg;
        seg.type = MarkdownSegment::Text;
        seg.text = markdown;
        seg.sourceStart = 0;
        seg.sourceEnd = 0;
        segments.append(seg);
        return segments;
    }
    if (!parser.parse(markdown)) {
        MarkdownSegment seg;
        seg.type = MarkdownSegment::Text;
        seg.text = markdown;
        seg.sourceStart = 0;
        seg.sourceEnd = markdown.length();
        segments.append(seg);
        return segments;
    }

    // Collect only the boundaries we split on. Tables stay in text; the
    // editor converts them to QTextTable inside a text item.
    QList<TreeSitterParser::BlockBoundary> blockBoundaries;
    for (const auto &b : parser.findBlockBoundaries()) {
        if (b.type == TreeSitterParser::BlockBoundary::Table)
            continue;
        blockBoundaries.append(b);
    }

    if (blockBoundaries.isEmpty()) {
        // Whole document is one text segment.
        MarkdownSegment seg;
        seg.type = MarkdownSegment::Text;
        seg.text = markdown;
        seg.sourceStart = 0;
        seg.sourceEnd = markdown.length();
        segments.append(seg);
        return segments;
    }

    auto emitText = [&](int runStart, int runEnd) {
        MarkdownSegment seg;
        seg.type = MarkdownSegment::Text;
        seg.text = markdown.mid(runStart, runEnd - runStart);
        seg.sourceStart = runStart;
        seg.sourceEnd = runEnd;
        segments.append(seg);
    };

    int cursor = 0;
    for (int k = 0; k < blockBoundaries.size(); ++k) {
        const auto &b = blockBoundaries[k];

        // --- Pre-block region: [runStart, runEnd) in source ---
        // runStart is the previous block's content-end (k>0) or doc start (k=0).
        const int runStart = cursor;
        const int runEnd = b.startChar;
        const int rawLen = runEnd - runStart;

        if (k == 0) {
            // Pre-first-block: strip at most one trailing '\n' (the join
            // separator to the following block). No leading boundary.
            int strippedEnd = runEnd;
            if (strippedEnd > runStart
                && markdown.at(strippedEnd - 1) == QLatin1Char('\n'))
                strippedEnd -= 1;
            const bool contentNonEmpty = (strippedEnd > runStart);
            const bool emitForLeadingNewline = (rawLen >= 1);
            if (contentNonEmpty || emitForLeadingNewline)
                emitText(runStart, strippedEnd);
        } else {
            // Between-blocks: strip at most one leading '\n' AND one trailing
            // '\n'. The leading '\n' is the join separator from the prior
            // block's content-end; the trailing '\n' is the join separator
            // to the following block.
            int strippedStart = runStart;
            int strippedEnd = runEnd;
            if (strippedStart < strippedEnd
                && markdown.at(strippedStart) == QLatin1Char('\n'))
                strippedStart += 1;
            if (strippedStart < strippedEnd
                && markdown.at(strippedEnd - 1) == QLatin1Char('\n'))
                strippedEnd -= 1;
            const bool contentNonEmpty = (strippedEnd > strippedStart);
            const bool emitForBlankLine = (rawLen >= 2);
            if (contentNonEmpty || emitForBlankLine)
                emitText(strippedStart, strippedEnd);
        }

        // --- Block segment ---
        // Normalize: strip a trailing '\n' from the block's content range
        // if tree-sitter included it, so the content-end is unambiguous.
        int blockContentEnd = b.endChar;
        if (blockContentEnd > b.startChar
            && markdown.at(blockContentEnd - 1) == QLatin1Char('\n'))
            blockContentEnd -= 1;

        MarkdownSegment blockSeg;
        switch (b.type) {
        case TreeSitterParser::BlockBoundary::FencedCodeBlock:
            blockSeg.type = MarkdownSegment::FencedCodeBlock;
            break;
        case TreeSitterParser::BlockBoundary::Image:
            blockSeg.type = MarkdownSegment::Image;
            break;
        case TreeSitterParser::BlockBoundary::Table:
            Q_UNREACHABLE();
            break;
        }
        blockSeg.text = markdown.mid(b.startChar, blockContentEnd - b.startChar);
        blockSeg.sourceStart = b.startChar;
        blockSeg.sourceEnd = blockContentEnd;
        segments.append(blockSeg);

        // Cursor advances to the block's CONTENT-END (before any terminating
        // '\n'). The terminating '\n' is the join separator to the next
        // segment, accounted for by the pre-block / trailing-region rules.
        cursor = blockContentEnd;
    }

    // --- Post-last-block region ---
    {
        const int runStart = cursor;
        const int runEnd = markdown.length();
        const int rawLen = runEnd - runStart;
        int strippedStart = runStart;
        if (strippedStart < runEnd
            && markdown.at(strippedStart) == QLatin1Char('\n'))
            strippedStart += 1;
        const bool contentNonEmpty = (runEnd > strippedStart);
        const bool emitForTrailingNewline = (rawLen >= 1);
        if (contentNonEmpty || emitForTrailingNewline)
            emitText(strippedStart, runEnd);
    }

    // Invariant: at least one segment always exists by this point
    // (we returned early for empty / no-boundary inputs).
    Q_ASSERT(!segments.isEmpty());
    return segments;
}

} // namespace Markoff
