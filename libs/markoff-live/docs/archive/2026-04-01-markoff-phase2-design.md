# Markoff Phase 2 — Live Preview, Syntax Highlighting, Obsidian Extensions

## Overview

Phase 2 transforms Markoff's editor from a plain text box into a live preview
markdown editor. Three sub-phases, built in this order:

1. **Syntax highlighting** — markdown-aware `QSyntaxHighlighter` for source mode
2. **Live preview foundation** — cursor-aware block rendering (raw near cursor,
   rendered far from cursor) with variable block heights
3. **Obsidian extension rendering** — callouts, highlights, comments, tags in
   both reading view and live preview

## Sub-Phase 2a: Syntax Highlighting

### MarkdownHighlighter

A `QSyntaxHighlighter` subclass that colors markdown syntax in the editor.
Operates on the `QTextDocument` block-by-block via `highlightBlock()`.

**Syntax elements to highlight:**

| Element | Format |
|---------|--------|
| `# Heading` (H1-H6) | Bold, scaled font size, heading color |
| `**bold**` | Bold weight |
| `*italic*` | Italic |
| `~~strikethrough~~` | Strikethrough |
| `` `inline code` `` | Monospace font, background tint |
| `[link](url)` | Link color (blue), underline the text portion |
| `[[wikilink]]` | Link color, distinct from standard links |
| `> blockquote` | Muted color, left indicator |
| `- list item` / `1. item` | Bullet/number in accent color |
| ` ``` ` fenced code | Background tint for entire block |
| `---` horizontal rule | Muted color |
| `$math$` / `$$math$$` | Math color |
| `==highlight==` | Yellow background |
| `%%comment%%` | Muted/dimmed |
| `#tag` | Tag color |
| `> [!callout]` | Callout type color |
| YAML frontmatter `---` | Muted, distinct background |

**State tracking:** Use `setCurrentBlockState()` for multi-line constructs
(fenced code blocks, frontmatter, block comments). States:

| State | Meaning |
|-------|---------|
| 0 | Normal |
| 1 | Inside fenced code block |
| 2 | Inside YAML frontmatter |
| 3 | Inside block comment (`%%`) |

**Integration:** The Editor creates the highlighter in its constructor,
attached to its `QTextDocument`. The highlighter is always active in source
mode. In live preview mode, it's active for blocks near the cursor (which
show raw text).

### Files

- Create: `libs/markoff/src/MarkdownHighlighter.h`
- Create: `libs/markoff/src/MarkdownHighlighter.cpp`
- Modify: `libs/markoff/src/Editor.cpp` — create highlighter in constructor

## Sub-Phase 2b: Live Preview

### Architecture

The editor operates in two modes:
- **Source mode:** all blocks show raw text with syntax highlighting (Phase 2a)
- **Live preview mode:** blocks near the cursor show raw text (with highlighting),
  blocks far from the cursor show rendered output

The mode is a property on the Editor: `setMode(Mode::Source)` or
`setMode(Mode::LivePreview)`.

### How It Works

1. The Editor holds a `Markoff::Document` (parsed AST) alongside the
   `QTextDocument` (raw text). On every `contentsChanged`, re-parse the
   changed blocks.

2. Each `QTextBlock` has `MarkoffBlockData` (via `QTextBlockUserData`)
   storing: the AST node type, whether the block is currently in "raw" or
   "rendered" display mode, cached rendered height, and a cached `QPixmap`
   of the rendered output.

3. On cursor position change, determine which blocks are "near" the cursor
   (same block + adjacent blocks) and which are "far." Update display mode
   for blocks that changed.

4. In `paintEvent()`, for each visible block:
   - If raw mode: paint normally (QTextLayout with syntax highlighting)
   - If rendered mode: paint the cached QPixmap (rendered output)

5. The `DocumentLayout` (forked `QPlainTextDocumentLayout`) reports variable
   block heights: raw-mode blocks report their text height, rendered-mode
   blocks report their rendered height (which may be larger for images,
   math, etc.).

### Block Data

```cpp
struct MarkoffBlockData : public QTextBlockUserData {
    enum DisplayMode { Raw, Rendered };
    DisplayMode displayMode = Raw;
    int renderedHeight = -1;      // -1 = not computed
    QPixmap renderedCache;         // cached rendered output
    // AST info (opaque for now — just block type)
    int blockType = -1;            // MD_BLOCKTYPE or -1 if not parsed
};
```

### Cursor Proximity

"Near the cursor" means: the block containing the cursor, plus one block
above and one block below. This gives the user editing context. All other
visible blocks render in preview mode.

For atomic blocks (tables, code blocks spanning multiple text blocks), the
entire atomic block transitions together — if the cursor is in any line of
a code block, all lines show raw.

### Re-Parsing Strategy

On `contentsChanged(position, charsRemoved, charsAdded)`:
1. Find the affected text blocks (the block at `position` plus any blocks
   whose count changed)
2. Re-parse the FULL document with MD4C (Phase 2 — no incremental parsing yet)
3. Compare old and new block structure — only invalidate render caches for
   blocks that actually changed
4. Update block heights for changed blocks

Full re-parse is acceptable for Phase 2. Documents under 10K lines parse in
< 50ms with MD4C. Incremental parsing (tree-sitter) comes in Phase 3.

### Editor API Additions

```cpp
namespace Markoff {
class Editor : public QAbstractScrollArea {
    // ... existing ...
    enum class Mode { Source, LivePreview };
    void setMode(Mode mode);
    Mode mode() const;
};
}
```

### Files

- Create: `libs/markoff/src/MarkoffBlockData.h`
- Modify: `libs/markoff/include/markoff/Editor.h` — add Mode enum, setMode/mode
- Modify: `libs/markoff/src/Editor.cpp` — live preview paint path, re-parse on change
- Modify: `libs/markoff/src/Editor_p.h` — add Document, mode state, block data management
- Modify: `libs/markoff/app/MainWindow.cpp` — add mode toggle button

## Sub-Phase 2c: Obsidian Extension Rendering

### Two-Layer Parsing

The `DocumentBuilder` (Layer 1, MD4C) handles CommonMark + GFM + wikilinks +
math. Layer 2 post-processes the AST to recognize Obsidian extensions:

| Extension | Layer 1 Output | Layer 2 Transformation |
|-----------|---------------|----------------------|
| Callouts `> [!type]` | Blockquote | Inspect first inline for `[!type]` pattern → Callout node |
| Highlights `==text==` | Plain text | Scan inlines for `==...==` → Highlight inline |
| Comments `%%text%%` | Plain text | Scan inlines for `%%...%%` → Comment inline |
| Tags `#tag` | Plain text | Scan inlines for `#word` → Tag inline |
| Task states `[/]` `[-]` | Task list item | Inspect task_mark character |

### AST Additions

Add new inline/block types to the AST:

```cpp
// In DocumentBuilder_p.h
struct InlineRun {
    // ... existing fields ...
    bool highlight = false;     // ==text==
    bool comment = false;       // %%text%%
    bool isTag = false;         // #tag
};

struct Block {
    // ... existing fields ...
    // Callout info (set by Layer 2 if this is a callout)
    bool isCallout = false;
    QString calloutType;        // "note", "warning", "tip", etc.
    QString calloutTitle;       // custom title or empty
    bool calloutFoldable = false;
    bool calloutCollapsed = false;
};
```

### Renderer Updates

The `Renderer` handles new types:

- **Callouts:** Colored box with left border, icon, type label. Background
  color and icon determined by callout type (13 built-in types from doc 07).
- **Highlights:** `<mark>` tag with yellow background.
- **Comments:** Hidden in rendered output (not emitted in HTML).
- **Tags:** Styled span with distinct color.

### Highlighter Updates

The `MarkdownHighlighter` already handles `==`, `%%`, `#tag` in Phase 2a.
No changes needed.

### Files

- Modify: `libs/markoff/src/DocumentBuilder_p.h` — add new fields
- Modify: `libs/markoff/src/DocumentBuilder.cpp` — add Layer 2 post-processing
- Modify: `libs/markoff/src/Renderer.cpp` — render new types
- Modify: `libs/markoff/src/MarkdownHighlighter.cpp` — if any highlighting gaps

## Testing

### Unit Tests

- `tst_highlighter.cpp`: verify heading, bold, italic, code, link highlighting
- `tst_document.cpp` additions: callout parsing, highlight parsing, comment parsing
- `tst_renderer.cpp` additions: callout rendering, highlight rendering, comment hiding

### Manual Testing

Test app exercises live preview with mode toggle button. Load real markdown
files from test vaults.

## What Phase 2 Does NOT Include

- Atomic blocks (tables/code blocks as interactive widgets)
- Incremental parsing (tree-sitter)
- Images, embeds, mermaid rendering
- Multiple cursors, vim mode, folding
- Paste subsystem
- Zero-width space technique (deferred — Phase 2 uses block-level toggling,
  not inline character hiding)
