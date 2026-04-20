// libs/markoff/include/markoff/Theme.h
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_THEME_H
#define MARKOFF_THEME_H

#include <QColor>
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

/// Paint-time colors used by graphics-item painters and `ExtraSelection`
/// highlights. These cover surfaces that cannot be expressed as
/// `QTextCharFormat` (custom-drawn checkbox glyphs, rounded code-block
/// backdrops, callout accent bars, search match ExtraSelection
/// overlays, etc.). `QTextCharFormat`-paintable elements live in
/// `Theme::formats` and are applied by `MarkdownHighlighter`.
struct PaintColors {
    // Code block backdrop (painted by MarkdownTextItem::paintDecoratedRanges
    // behind the code text). The text color itself is driven by the
    // CodeBlock / Code* elements in `formats`.
    QColor codeBlockBg;
    QColor codeBlockBorder;
    QColor codeBlockLanguageLabel;

    // Search ExtraSelection backgrounds (Editor::highlightAllMatches).
    // The alpha is typically <255 so it blends with the underlying text
    // formats.
    QColor searchMatchBg;
    QColor searchCurrentMatchBg;

    // Custom-drawn checkbox glyph (CheckboxTextObject::drawObject). The
    // corresponding `CheckboxChecked` / `CheckboxUnchecked` elements in
    // `formats` colour any *text* on the same line — these three fields
    // style the glyph itself.
    QColor checkboxCheckedFill;
    QColor checkboxCheckMark;
    QColor checkboxUncheckedOutline;

    // Image block placeholder drawn by ImageBlockItem when the referenced
    // image cannot be resolved.
    QColor imagePlaceholderBg;
    QColor imagePlaceholderBorder;
    QColor imagePlaceholderText;

    // BlockItem "fully selected" overlay (selection spans this whole item).
    // Typically a semi-transparent accent color.
    QColor blockSelectionOverlay;

    // Callout accent color keyed by lowercased type name ("note", "warning",
    // ...). `calloutDefault` is used as a fallback when the typed name is
    // not present. Obsidian-compatible names (note/info/todo, abstract/
    // summary/tldr, ...) are provided by `defaultLight`/`defaultDark`.
    QHash<QString, QColor> calloutAccents;
    QColor calloutDefault;
};

struct Theme {
    QHash<Element, QTextCharFormat> formats;

    QFont textFont;     // base proportional font
    QFont codeFont;     // base monospace font

    /// Non-format paint colors. See `PaintColors` doc comment for scope.
    PaintColors paint;

    /// Resolve a callout's accent color from `paint.calloutAccents`,
    /// lower-casing `type` and falling back to `paint.calloutDefault`.
    QColor calloutColor(const QString &type) const;

    static Theme defaultLight();
    static Theme defaultDark();
    static Theme fromSchemeFile(const QString &path);
};

} // namespace Markoff

#endif // MARKOFF_THEME_H
