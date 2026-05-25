// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_SOURCESPAN_H
#define MARKOFF_SOURCESPAN_H

#include <QList>
#include <QMetaType>
#include <QString>
#include <markoff/parser/LinkTarget.h>

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

    // Structured link payload — populated when isLink or isWikilink.
    // Empty (default-constructed) for all other spans. See LinkTarget.h.
    LinkTarget linkTarget;

    bool operator==(const SourceSpan &o) const noexcept {
        return utf8Offset == o.utf8Offset && utf8Length == o.utf8Length
            && charOffset == o.charOffset && charLength == o.charLength
            && bold == o.bold && italic == o.italic
            && strikethrough == o.strikethrough && code == o.code
            && math == o.math && mathDisplay == o.mathDisplay
            && highlight == o.highlight && comment == o.comment
            && isTag == o.isTag && isLink == o.isLink
            && isWikilink == o.isWikilink && isImage == o.isImage
            && isFootnoteRef == o.isFootnoteRef
            && isHeading == o.isHeading && headingLevel == o.headingLevel
            && isBlockquoteMarker == o.isBlockquoteMarker
            && isListMarker == o.isListMarker
            && isCodeBlockFence == o.isCodeBlockFence
            && isCodeBlockContent == o.isCodeBlockContent
            && isFrontmatter == o.isFrontmatter
            && isHorizontalRule == o.isHorizontalRule
            && isBlockquote == o.isBlockquote
            && blockquoteDepth == o.blockquoteDepth
            && isCalloutMarker == o.isCalloutMarker
            && isTaskMarker == o.isTaskMarker
            && isDelimiter == o.isDelimiter
            && parentCharStart == o.parentCharStart
            && parentCharEnd == o.parentCharEnd
            && linkTarget == o.linkTarget;
    }
    bool operator!=(const SourceSpan &o) const noexcept { return !(*this == o); }
};

/// Build a UTF-8 byte offset → QString char offset mapping table.
/// Returns a vector where index = UTF-8 byte offset, value = QString char offset.
QList<int> buildUtf8ToCharMap(const QByteArray &utf8);

} // namespace Markoff

Q_DECLARE_METATYPE(Markoff::SourceSpan)
Q_DECLARE_METATYPE(QList<Markoff::SourceSpan>)

#endif // MARKOFF_SOURCESPAN_H
