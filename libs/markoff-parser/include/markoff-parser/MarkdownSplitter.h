// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_MARKDOWNSPLITTER_H
#define MARKOFF_MARKDOWNSPLITTER_H

#include <QList>
#include <QString>

namespace Markoff {

class TreeSitterParser;

/// A segment of a markdown document — either text or a non-text block.
struct MarkdownSegment {
    enum Type { Text, Table, FencedCodeBlock, Image };
    Type type = Text;
    QString text;       ///< The raw markdown for this segment
    int sourceStart = 0; ///< QString char offset in original document
    int sourceEnd = 0;   ///< QString char offset end
};

/// Splits markdown into segments at block boundaries (tables, code blocks).
/// Text segments contain raw markdown; block segments contain the block's
/// raw markdown (pipe table, fenced code block).
class MarkdownSplitter {
public:
    /// Split markdown text into segments using TreeSitterParser.
    static QList<MarkdownSegment> split(const QString &markdown,
                                        TreeSitterParser &parser);
};

} // namespace Markoff

#endif // MARKOFF_MARKDOWNSPLITTER_H
