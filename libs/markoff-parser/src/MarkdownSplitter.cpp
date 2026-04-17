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
            textSeg.sourceStart = pos;
            textSeg.sourceEnd = boundary.startChar;
            // Trim trailing newline from text segment (it belongs to the block boundary)
            if (textSeg.text.endsWith(QLatin1Char('\n')))
                textSeg.text.chop(1);
            if (!textSeg.text.isEmpty())
                segments.append(textSeg);
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
        blockSeg.sourceStart = boundary.startChar;
        blockSeg.sourceEnd = boundary.endChar;
        segments.append(blockSeg);

        pos = boundary.endChar;
    }

    // Text after last block
    if (pos < markdown.length()) {
        MarkdownSegment textSeg;
        textSeg.type = MarkdownSegment::Text;
        textSeg.text = markdown.mid(pos);
        textSeg.sourceStart = pos;
        textSeg.sourceEnd = markdown.length();
        // Trim leading newline (it belonged to the previous block)
        if (textSeg.text.startsWith(QLatin1Char('\n')))
            textSeg.text.remove(0, 1);
        if (!textSeg.text.isEmpty())
            segments.append(textSeg);
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
