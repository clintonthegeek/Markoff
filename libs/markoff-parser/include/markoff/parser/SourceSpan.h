// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_SOURCESPAN_H
#define MARKOFF_SOURCESPAN_H

#include <QList>
#include <QString>

namespace Markoff {

/// A flat representation of a styled range in the source text.
/// Built from the parsed AST. Used by the highlighter to apply
/// formatting without regex.
struct SourceSpan {
    int utf8Offset = 0;     // byte offset in UTF-8 source
    int utf8Length = 0;      // byte length

    // These are computed from utf8 offsets by the offset mapper
    int charOffset = 0;      // QString character offset (UTF-16)
    int charLength = 0;      // QString character count

    // Formatting flags (from the AST's InlineRun)
    bool bold = false;
    bool italic = false;
    bool strikethrough = false;
    bool code = false;
    bool math = false;
    bool mathDisplay = false;
    bool highlight = false;
    bool comment = false;
    bool isTag = false;
    bool isLink = false;
    bool isWikilink = false;
    bool isImage = false;
    bool isFootnoteRef = false;  // [^1] footnote reference — render superscript

    // Block-level info
    bool isHeading = false;
    int headingLevel = 0;
    bool isBlockquoteMarker = false;
    bool isListMarker = false;
    bool isCodeBlockFence = false;
    bool isCodeBlockContent = false;
    bool isFrontmatter = false;
    bool isHorizontalRule = false;
    bool isBlockquote = false;
    int blockquoteDepth = 0;
    bool isCalloutMarker = false;
    bool isTaskMarker = false;       // task_list_marker_checked/unchecked

    // True for syntax delimiters (**, *, `, [[, ]], ==, %%, ~~, $)
    // that should be hidden in live preview when cursor is not adjacent
    bool isDelimiter = false;

    // For delimiter spans: the char range of the parent formatting element
    // (e.g., for ** in **bold**, this is the range of the entire **bold**).
    // Used to decide delimiter visibility: show delimiters when cursor is
    // anywhere within the parent range, not just adjacent to the delimiter.
    int parentCharStart = -1;
    int parentCharEnd = -1;
};

/// Build a UTF-8 byte offset → QString char offset mapping table.
/// Returns a vector where index = UTF-8 byte offset, value = QString char offset.
QList<int> buildUtf8ToCharMap(const QByteArray &utf8);

} // namespace Markoff

#endif // MARKOFF_SOURCESPAN_H
