# Decorated Block Ranges

> **Status: IMPLEMENTED** — DecoratedRange struct and
> MarkdownTextItem::paintDecoratedRanges() shipped. Retained for design
> rationale.

## Problem

Code blocks and callouts use atomic blocks (separate QPainter pixmaps)
that replace the text entirely. This breaks cursor placement, selection,
arrow key navigation, and requires fragile coordinate mapping. Meanwhile,
the highlighter-based approach (used for bold, headings, links, etc.)
provides all text editing affordances for free because the text stays
in the QTextDocument.

## Solution

Replace pixmap-based atomic blocks with "decorated block ranges" —
the text stays in the QTextDocument (formatted by the highlighter),
and `paintEvent` paints visual decorations around it.

## What's a Decorated Range

A decorated range is a contiguous set of QTextBlocks that share visual
chrome: background, border, labels, line numbers, vertical bars. The
text inside is normal QTextDocument text, fully editable.

### Code Block Decoration
- Gray background rect with rounded corners spanning all code lines
- Language label in the top-right corner
- Line numbers in a gutter to the left of the code text
- The text is monospace (from highlighter's `isCodeBlockContent`)
- Fence markers (```) are hidden (from highlighter's delimiter hiding)

### Callout Decoration
- Colored left border (4px, type-specific color)
- Faint type-specific background
- Type label in bold at the top
- The `> [!type]` markers are hidden (delimiter hiding)

### Blockquote Decoration
- Vertical colored line in the indent space
- Already indented (from `applyBlockFormats`)
- Already gray text (from highlighter)

## Data Structure

```cpp
struct DecoratedRange {
    enum Type { CodeBlock, Callout, Blockquote };
    Type type;
    int firstBlock;
    int lastBlock;
    // Code block specific
    QString language;
    // Callout specific
    QString calloutType;
    QString calloutTitle;
};
```

Stored in `Editor::Private` alongside the existing `atomicBlocks` map
(which is kept for images, math, diagrams).

## Paint Path

In `paintEvent`, for each visible block:
1. Check if it belongs to a decorated range
2. If it's the FIRST block of the range, paint the background/border
   spanning the full range height
3. Paint line numbers (for code blocks) in the gutter
4. Paint the language label (for code blocks)
5. Then call `layout->draw()` as normal — the text renders on top

## What Gets Deleted

- `CodeAtomicBlock.h/cpp` — replaced by code block decoration painting
- `CalloutAtomicBlock.h/cpp` — replaced by callout decoration painting
- Code/callout detection in `detectAtomicBlocks()` — moved to
  `detectDecoratedRanges()`

## What Gets Kept

- `AtomicBlock.h/cpp` — base class for true atomic blocks (images, math)
- `MarkoffBlockData` with atomic block pointers — still used for images
- Highlighter formatting (monospace for code, gray for quotes, etc.)
- Block format application (indent for blockquotes)
