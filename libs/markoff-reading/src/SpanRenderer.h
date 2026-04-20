// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#ifndef CORBOMITE_READINGVIEW_SPANRENDERER_H
#define CORBOMITE_READINGVIEW_SPANRENDERER_H

#include <QList>
#include <QString>
#include <QVariantMap>

class QTextCursor;
class QTextCharFormat;

namespace Corbomite::ReadingView {

class StyleManager;

/// Lightweight inline-span walker. Replaces Phase 3a's HTML-building
/// `inlineToHtml` pass with a CharacterStyle-driven tokenization that
/// writes directly into a `QTextCursor` via
/// `cursor.insertText(text, charFormat)` — the same pattern
/// Markoff::MarkdownTextItem uses. No HTML round-trip.
///
/// Recognised inline spans (precedence in listed order):
///   `code`                 → CharacterStyle "InlineCode"
///   $inline math$          → emits QChar::ObjectReplacementCharacter with
///                            MathSpanKind = Inline. SectionLayout picks
///                            these up to decide between inline-QTextObject
///                            or split-pixmap placement.
///   ![alt](path)           → emits an image placeholder entry.
///   [[target|display]]     → CharacterStyle "WikiLink", with target
///                            stored in the fragment's user data under
///                            WikiLinkTargetProperty.
///   [text](url)            → CharacterStyle "Link" + setAnchor / AnchorHref.
///   **strong** / __strong__→ CharacterStyle "Strong"
///   ~~strike~~             → CharacterStyle "Strikethrough"
///   ==highlight==          → CharacterStyle "Highlight"
///   *emph* / _emph_        → CharacterStyle "Emphasis"
///
/// Anything else is inserted as plain text with the current base format.
class SpanRenderer
{
public:
    /// Custom QTextCharFormat property slot carrying the wiki-link target.
    /// 0x100001 stays above QTextFormat::UserProperty (0x100000) and avoids
    /// collisions with Markoff's math/checkbox property assignments.
    static constexpr int WikiLinkTargetProperty = 0x100001;

    /// Slot carrying the original markdown source of an inline-math span
    /// (without surrounding `$`). Used by SectionLayout's math routing.
    static constexpr int InlineMathSourceProperty = 0x100002;

    /// Slot carrying image embed path (as written in the markdown).
    static constexpr int InlineImagePathProperty = 0x100003;

    /// Slot carrying image alt text.
    static constexpr int InlineImageAltProperty = 0x100004;

    /// `charFormat.objectType()` marker for an inline-math run emitted by
    /// the span walker. Consumed by SectionLayout to decide placement.
    static constexpr int MathObjectMarker = 0x100010;

    /// Object-type marker for an inline-image placeholder.
    static constexpr int ImageObjectMarker = 0x100011;

    SpanRenderer() = default;

    /// Walk `text`, emitting characters into `cursor` with the appropriate
    /// CharacterStyle applied. `baseFormat` is the paragraph's base char
    /// format (font/color). `styles` supplies the character-style table.
    ///
    /// Returns true if at least one inline-math or inline-image run was
    /// emitted via `QChar::ObjectReplacementCharacter` — SectionLayout uses
    /// this to decide whether the paragraph needs a post-pass to attach
    /// math/image object handlers.
    bool renderInline(QTextCursor &cursor,
                      const QString &text,
                      const QTextCharFormat &baseFormat,
                      StyleManager &styles) const;
};

} // namespace Corbomite::ReadingView

#endif // CORBOMITE_READINGVIEW_SPANRENDERER_H
