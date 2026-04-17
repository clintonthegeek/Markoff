// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-parser/MarkdownSplitter.h>
#include <markoff-parser/TreeSitterParser.h>

namespace Markoff {

QList<MarkdownSegment> MarkdownSplitter::split(const QString &markdown,
                                                TreeSitterParser &parser)
{
    QList<MarkdownSegment> segments;

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
        // Parse failed — treat entire document as text
        MarkdownSegment seg;
        seg.type = MarkdownSegment::Text;
        seg.text = markdown;
        seg.sourceStart = 0;
        seg.sourceEnd = markdown.length();
        segments.append(seg);
        return segments;
    }

    auto boundaries = parser.findBlockBoundaries();

    if (boundaries.isEmpty()) {
        // No block boundaries — entire document is text
        MarkdownSegment seg;
        seg.type = MarkdownSegment::Text;
        seg.text = markdown;
        seg.sourceStart = 0;
        seg.sourceEnd = markdown.length();
        segments.append(seg);
        return segments;
    }

    int pos = 0; // current char position in markdown
    // Track where the last EMITTED segment's content ended in source.
    // leadSeparator of the next segment = markdown[prevContentEnd ..
    // thisContentStart], capturing the exact blank-line count from source.
    int prevContentEnd = 0;

    for (const auto &boundary : boundaries) {
        // Tables stay in text segments — they'll be converted to
        // QTextTable by TableConverter in the editor.
        if (boundary.type == TreeSitterParser::BlockBoundary::Table)
            continue;

        // Text before this block
        if (boundary.startChar > pos) {
            MarkdownSegment textSeg;
            textSeg.type = MarkdownSegment::Text;
            textSeg.text = markdown.mid(pos, boundary.startChar - pos);
            // Trim trailing newline from text segment (it belongs to the block boundary)
            if (textSeg.text.endsWith(QLatin1Char('\n')))
                textSeg.text.chop(1);
            const int contentStart = pos;
            const int contentEnd = contentStart + textSeg.text.length();
            textSeg.sourceStart = contentStart;
            textSeg.sourceEnd = contentEnd;
            textSeg.leadSeparator = markdown.mid(prevContentEnd,
                                                 contentStart - prevContentEnd);
            if (!textSeg.text.isEmpty()) {
                segments.append(textSeg);
                prevContentEnd = contentEnd;
            }
        }

        // The block itself
        MarkdownSegment blockSeg;
        switch (boundary.type) {
        case TreeSitterParser::BlockBoundary::Table:
            Q_UNREACHABLE(); // Tables are skipped above
            break;
        case TreeSitterParser::BlockBoundary::FencedCodeBlock:
            blockSeg.type = MarkdownSegment::FencedCodeBlock;
            break;
        case TreeSitterParser::BlockBoundary::Image:
            blockSeg.type = MarkdownSegment::Image;
            break;
        }
        blockSeg.text = markdown.mid(boundary.startChar,
                                      boundary.endChar - boundary.startChar);
        // Trim trailing newline
        if (blockSeg.text.endsWith(QLatin1Char('\n')))
            blockSeg.text.chop(1);
        const int blockContentStart = boundary.startChar;
        const int blockContentEnd = blockContentStart + blockSeg.text.length();
        blockSeg.sourceStart = blockContentStart;
        blockSeg.sourceEnd = blockContentEnd;
        blockSeg.leadSeparator = markdown.mid(prevContentEnd,
                                               blockContentStart - prevContentEnd);
        segments.append(blockSeg);
        prevContentEnd = blockContentEnd;

        pos = boundary.endChar;
    }

    // Text after last block
    if (pos < markdown.length()) {
        MarkdownSegment textSeg;
        textSeg.type = MarkdownSegment::Text;
        textSeg.text = markdown.mid(pos);
        int contentStart = pos;
        // Trim leading newline (it belonged to the previous block)
        if (textSeg.text.startsWith(QLatin1Char('\n'))) {
            textSeg.text.remove(0, 1);
            contentStart += 1;
        }
        const int contentEnd = contentStart + textSeg.text.length();
        textSeg.sourceStart = contentStart;
        textSeg.sourceEnd = contentEnd;
        textSeg.leadSeparator = markdown.mid(prevContentEnd,
                                              contentStart - prevContentEnd);
        if (!textSeg.text.isEmpty()) {
            segments.append(textSeg);
            prevContentEnd = contentEnd;
        }
    }

    // Ensure at least one text segment exists (for cursor placement)
    if (segments.isEmpty()) {
        MarkdownSegment seg;
        seg.type = MarkdownSegment::Text;
        seg.sourceStart = 0;
        seg.sourceEnd = 0;
        segments.append(seg);
    }

    return segments;
}

} // namespace Markoff
