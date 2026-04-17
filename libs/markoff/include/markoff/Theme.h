// libs/markoff/include/markoff/Theme.h
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_THEME_H
#define MARKOFF_THEME_H

#include <QFont>
#include <QHash>
#include <QString>
#include <QTextCharFormat>

namespace Markoff {

enum class Element {
    // Base
    Text,
    CurrentLineBackground,

    // Headings
    H1, H2, H3, H4, H5, H6,

    // Inline formatting
    Bold, Italic, Strikethrough, InlineCode,
    Math,               // $...$ or $$...$$
    Highlight,          // ==text==
    Comment,            // %%text%%
    Tag,                // #tag

    // Footnotes
    FootnoteRef,        // ^[1] superscript ref

    // Links
    Link,               // [text](url)
    WikiLink,           // [[note]]
    BrokenLink,         // unresolvable link
    Image,              // ![alt](src)

    // Block-level
    CodeBlock,
    BlockQuote,
    HorizontalRule,
    ListMarker,
    Table,
    FrontmatterBlock,
    Callout,

    // Task checkboxes
    CheckboxUnchecked,
    CheckboxChecked,

    // Syntax highlighting within code blocks
    CodeKeyword, CodeString, CodeComment, CodeType,
    CodeNumLiteral, CodeBuiltIn, CodeOther,

    // Misc
    MaskedSyntax,       // hidden delimiters in live preview
    TrailingSpace,
};

struct Theme {
    QHash<Element, QTextCharFormat> formats;

    QFont textFont;     // base proportional font
    QFont codeFont;     // base monospace font

    static Theme defaultLight();
    static Theme defaultDark();
    static Theme fromSchemeFile(const QString &path);
};

} // namespace Markoff

#endif // MARKOFF_THEME_H
