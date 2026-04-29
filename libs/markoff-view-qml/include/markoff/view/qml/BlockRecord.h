// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

namespace Markoff::View::Qml {

/// Kind-tagged record for a single AST block, carrying both identity-bearing
/// data (kind + raw source) and rendering data (kind-specific fields). The
/// `source` field is the raw markdown text for this block exactly as it
/// appears in the document; `text` holds the renderable string (may equal
/// source, or strip frontmatter markers, etc., per kind).
struct BlockRecord {
    QString kind;          ///< One of BlockKind::*
    QString source;        ///< Raw markdown source for this block (identity)
    QString text;          ///< Renderable text (used by paragraph/heading delegates)

    int     headingLevel = 0;   ///< 1..6 if kind==Heading; else 0
    QString imageSrc;           ///< URL/path if kind==Image
    QString imageAlt;           ///< Alt text if kind==Image
    QString imageTitle;         ///< Optional title if kind==Image
    QString codeLanguage;       ///< Fence info-string if kind==CodeBlock
    QString codeText;           ///< Body without fences if kind==CodeBlock

    bool operator==(const BlockRecord &o) const noexcept {
        return kind == o.kind && source == o.source && text == o.text
            && headingLevel == o.headingLevel
            && imageSrc == o.imageSrc && imageAlt == o.imageAlt && imageTitle == o.imageTitle
            && codeLanguage == o.codeLanguage && codeText == o.codeText;
    }
    bool operator!=(const BlockRecord &o) const noexcept { return !(*this == o); }
};

/// Minimal identity key used by AstBlockDiff. Two blocks with the same kind
/// and source bytes are "the same block" from the diff's perspective.
struct BlockKey {
    QString kind;
    QString source;
    bool operator==(const BlockKey &o) const noexcept {
        return kind == o.kind && source == o.source;
    }
    bool operator!=(const BlockKey &o) const noexcept { return !(*this == o); }
};

}  // namespace Markoff::View::Qml
