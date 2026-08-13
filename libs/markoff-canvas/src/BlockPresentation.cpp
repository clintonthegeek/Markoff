// SPDX-License-Identifier: GPL-3.0-or-later
#include "BlockPresentation.h"

#include <QFontMetricsF>

#include <markoff/core/AttrNames.h>
#include <markoff/core/BlockKind.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Theme.h>

namespace Markoff::Canvas {

namespace {

/// Indent step for nested list items, in units of the body font's height.
constexpr qreal kIndentSteps = 1.6;

/// Heading level → Theme slot. The Theme owns the sizes and colors; this
/// function owns nothing but the mapping.
Theme::Slot headingSlot(int level)
{
    switch (level) {
    case 1:  return Theme::Slot::Heading1;
    case 2:  return Theme::Slot::Heading2;
    case 3:  return Theme::Slot::Heading3;
    case 4:  return Theme::Slot::Heading4;
    case 5:  return Theme::Slot::Heading5;
    default: return Theme::Slot::Heading6;
    }
}

/// Build a QFont from a Theme slot's family/size/weight/slant. Uses pixel
/// sizing because pixelSizeFor() has already folded in the slot's size
/// multiplier — going back through point sizes would drop it.
QFont fontForSlot(const Theme &theme, Theme::Slot slot)
{
    QFont f(theme.familyFor(slot));
    f.setPixelSize(qMax(1, qRound(theme.pixelSizeFor(slot))));
    f.setBold(theme.isBold(slot));
    f.setItalic(theme.isItalic(slot));
    return f;
}

/// Theme::color() falls back to TextDefault for any slot the theme does
/// not define. That is survivable for a foreground, but for a BACKGROUND
/// slot it hands back the text colour — painting a black slab under black
/// text. Neither defaultLight() nor defaultDark() defines QuoteBackground
/// today, so this is the live case, not a hypothetical.
///
/// Treat "resolved to exactly TextDefault" as "not defined" and paint no
/// background. A theme that genuinely wants its quote background to equal
/// its body text colour loses nothing worth having.
QColor backgroundOrNone(const Theme &theme, Theme::Slot slot)
{
    const QColor c = theme.color(slot);
    return c == theme.color(Theme::Slot::TextDefault) ? QColor() : c;
}

int intAttr(const MarkoffDocument &doc, BlockId id, const AttrName &name,
            int fallback)
{
    const auto attrs = doc.blockAttrs(id);
    const auto it = attrs.constFind(name);
    if (it == attrs.cend())
        return fallback;
    if (const int *p = std::get_if<int>(&it.value()))
        return *p;
    return fallback;
}

}  // namespace

BlockStyle presentationFor(const MarkoffDocument &doc, BlockId id,
                           const Theme &theme)
{
    BlockStyle s;
    s.font       = fontForSlot(theme, Theme::Slot::TextDefault);
    s.foreground = theme.color(Theme::Slot::TextDefault);

    const qreal em = QFontMetricsF(s.font).height();
    s.topMargin    = em * 0.35;
    s.bottomMargin = em * 0.35;

    switch (doc.blockKind(id)) {
    case BlockKind::Paragraph:
        break;

    case BlockKind::Heading: {
        const int level = qBound(1, intAttr(doc, id, AttrNames::Level, 1), 6);
        const Theme::Slot slot = headingSlot(level);
        s.font       = fontForSlot(theme, slot);
        s.foreground = theme.color(slot);
        const qreal hem = QFontMetricsF(s.font).height();
        s.topMargin    = hem * 0.6;
        s.bottomMargin = hem * 0.3;
        break;
    }

    case BlockKind::CodeBlock:
        s.font       = fontForSlot(theme, Theme::Slot::CodeBlock);
        s.foreground = theme.color(Theme::Slot::CodeBlock);
        s.background = backgroundOrNone(theme, Theme::Slot::CodeBlockBackground);
        s.fullWidthBackground = true;
        s.leftIndent = em * 0.5;
        break;

    case BlockKind::ListItem: {
        const int indent = qMax(0, intAttr(doc, id, AttrNames::IndentLevel, 0));
        s.leftIndent = em * kIndentSteps * (indent + 1);
        // Single source of truth for the marker text (queue #8.3) — do not
        // re-derive it from markerStyle/markerNumber attrs.
        s.marker = QString::fromUtf8(doc.listItemDisplayMarker(id)).trimmed();
        s.topMargin    = em * 0.15;
        s.bottomMargin = em * 0.15;
        break;
    }

    case BlockKind::BlockQuote: {
        const int depth = qMax(1, intAttr(doc, id, AttrNames::BlockQuoteDepth, 1));
        s.font       = fontForSlot(theme, Theme::Slot::Quote);
        s.foreground = theme.color(Theme::Slot::Quote);
        s.background = backgroundOrNone(theme, Theme::Slot::QuoteBackground);
        s.fullWidthBackground = true;
        s.hasQuoteBar = true;
        s.leftIndent  = em * 1.0 * depth;
        break;
    }

    case BlockKind::HorizontalRule:
        s.isRule       = true;
        s.topMargin    = em * 0.5;
        s.bottomMargin = em * 0.5;
        break;

    case BlockKind::Math:
        s.foreground = theme.color(Theme::Slot::Math);
        break;

    // Out of T1 scope, rendered as their source text in the body font so
    // the document stays navigable: Table gets a real grid in T9, and
    // Image/Mermaid/HtmlBlock are out of spike scope entirely (spec §5).
    case BlockKind::Table:
    case BlockKind::Image:
    case BlockKind::Mermaid:
    case BlockKind::HtmlBlock:
        break;
    }

    return s;
}

}  // namespace Markoff::Canvas
