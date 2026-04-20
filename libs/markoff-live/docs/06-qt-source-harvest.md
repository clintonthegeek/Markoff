# Markoff: Qt Source Harvest Plan

## The GPL Insight

Corbomite is GPLv3. Qt is available under GPL. We can cherry-pick Qt's own
source code — the same battle-tested text editing infrastructure used by
every Qt application — and embed it directly in our codebase, modified to
our needs.

This eliminates the central objection to Option C (Custom Widget): the claim
that cursor handling, selection, IME, clipboard, undo/redo, scrolling, and
accessibility must be "implemented from scratch." They don't. They're already
implemented. We take them.

---

## What This Changes

The original tradeoff analysis for Option C:

| Concern | Original Assessment | With Qt Source Harvest |
|---------|--------------------|-----------------------|
| Text input + IME | Must implement (~months) | **Already implemented** — fork QWidgetTextControl |
| Cursor + navigation | Must implement (~weeks) | **Already implemented** — fork QTextCursor integration |
| Selection | Must implement (~weeks) | **Already implemented** — fork QWidgetTextControl |
| Clipboard | Must implement (~days) | **Already implemented** — fork QWidgetTextControl |
| Undo/redo | Must implement (~weeks) | **Already implemented** — fork QTextDocument undo stack |
| Scrolling | Must implement (~days) | **Already implemented** — fork QPlainTextEdit + QAbstractScrollArea |
| Accessibility | Must implement (~weeks) | **Already implemented** — fork QPlainTextEdit accessibility |
| Find/replace | Must implement (~days) | **Already implemented** — fork QWidgetTextControl |
| Drag-and-drop | Must implement (~days) | **Already implemented** — fork QWidgetTextControl |

**Revised time estimate for Option C: comparable to Option A.** The "from
scratch" penalty is eliminated. What remains is the same core work: parsing,
AST, rendering, and the markdown-specific modifications.

---

## Source Files to Harvest

### Tier 1: Must Fork (Core Widget)

These files form the editing surface. We fork them into `libs/markoff/` under
our own class names.

| Qt File | Lines | Fork As | Purpose |
|---------|-------|---------|---------|
| `qplaintextedit.h` | 294 | `MarkoffEditor.h` | Widget public API |
| `qplaintextedit.cpp` | 3,231 | `MarkoffEditor.cpp` | Widget implementation |
| `qplaintextedit_p.h` | 159 | `MarkoffEditor_p.h` | Private implementation |
| `qwidgettextcontrol_p.h` | 289 | `MarkoffTextControl.h` | Text control API |
| `qwidgettextcontrol_p_p.h` | 216 | `MarkoffTextControl_p.h` | Text control private |
| `qwidgettextcontrol.cpp` | 3,577 | `MarkoffTextControl.cpp` | Text control implementation |

**Total: ~7,766 lines.** This gives us the complete editing engine: input
handling, cursor management, selection, clipboard, drag-drop, IME, undo/redo
integration, link handling, and rendering support.

**What we modify:**
- Remove rich text mode (we handle formatting through the AST, not QTextCharFormat)
- Add per-block rendering mode (raw vs. rendered)
- Add block height management (rendered blocks can be taller)
- Add cursor-proximity-aware paint decisions
- Add markdown-aware input handling (auto-pairing of `**`, `[[`, etc.)
- Add markdown-aware clipboard (copy as markdown, paste with format detection)

### Tier 2: Use As-Is (Link Against Qt)

These files are part of Qt's public API and we link against them normally. No
need to fork — they work as-is through the public interface.

| Qt Class | Purpose | Markoff Usage |
|----------|---------|---------------|
| `QTextDocument` | Document model | Store raw markdown text |
| `QTextCursor` | Cursor operations | Navigate and modify text |
| `QTextBlock` | Block abstraction | Per-paragraph access |
| `QTextBlockUserData` | Block metadata | AST node pointer, render cache |
| `QTextLayout` | Line-level layout | Glyph shaping, line breaking |
| `QSyntaxHighlighter` | Syntax coloring | Markdown source highlighting |
| `QAbstractScrollArea` | Scroll management | Viewport + scrollbars |

### Tier 3: Fork If Needed (Layout Engine)

The layout engine may need forking if `QPlainTextDocumentLayout` can't handle
variable block heights. But since we're already inside the widget internals,
we can start by subclassing and only fork if needed.

| Qt File | Lines | When to Fork |
|---------|-------|-------------|
| `QPlainTextDocumentLayout` (in qplaintextedit.cpp) | ~200 | If block height tricks don't work via subclassing |
| `qtextdocumentlayout.cpp` | 4,217 | If we need full rich text layout (tables, inline objects) as a starting point |

---

## Architecture: Forked Widget Internals

```
┌──────────────────────────────────────────────────────────┐
│                 MarkoffEditor                             │
│          (forked from QPlainTextEdit)                     │
│                                                           │
│  ┌────────────────────────────────────────────────┐      │
│  │          MarkoffTextControl                     │      │
│  │       (forked from QWidgetTextControl)           │      │
│  │                                                  │      │
│  │  Inherited (unchanged):                          │      │
│  │  • Key event handling + IME                      │      │
│  │  • Mouse events + cursor positioning             │      │
│  │  • Selection (word, line, block, all)             │      │
│  │  • Clipboard (cut/copy/paste)                    │      │
│  │  • Drag-and-drop                                 │      │
│  │  • Undo/redo coordination                        │      │
│  │  • Cursor blinking                               │      │
│  │  • Find operations                               │      │
│  │                                                  │      │
│  │  Modified:                                       │      │
│  │  • paintContents() — per-block raw/rendered      │      │
│  │  • keyPressEvent() — markdown commands            │      │
│  │  • insertFromMimeData() — markdown paste          │      │
│  │  • Block height queries — variable heights        │      │
│  └────────────────────────────────────────────────┘      │
│                                                           │
│  ┌────────────────────────────────────────────────┐      │
│  │  QTextDocument (unmodified, linked from Qt)      │      │
│  │  + MarkoffBlockData (QTextBlockUserData)         │      │
│  │    - AST node pointer                            │      │
│  │    - Rendered height                             │      │
│  │    - Render cache (QPixmap)                      │      │
│  │    - Block rendering mode (raw/rendered)         │      │
│  └────────────────────────────────────────────────┘      │
│                                                           │
│  ┌────────────────────────────────────────────────┐      │
│  │  MarkoffDocumentLayout                           │      │
│  │  (subclass or fork of QPlainTextDocumentLayout)  │      │
│  │  - Variable block heights based on render mode   │      │
│  │  - Reports rendered height for non-cursor blocks │      │
│  │  - Reports text height for cursor-adjacent blocks│      │
│  └────────────────────────────────────────────────┘      │
│                                                           │
│  ┌────────────────────────────────────────────────┐      │
│  │  MarkoffDocument (our AST — NOT QTextDocument)   │      │
│  │  - Variant-based AST with source positions       │      │
│  │  - Built by MarkoffDocumentBuilder (MD4C)        │      │
│  │  - Parallel to QTextDocument, kept in sync       │      │
│  └────────────────────────────────────────────────┘      │
│                                                           │
│  ┌────────────────────────────────────────────────┐      │
│  │  MarkoffRenderer                                 │      │
│  │  - AST → QPainter for rendered blocks            │      │
│  │  - Hybrid: QTextDocument for simple, custom for  │      │
│  │    callouts/math/diagrams                        │      │
│  └────────────────────────────────────────────────┘      │
└──────────────────────────────────────────────────────────┘
```

---

## The Fork Process

### Step 1: Copy and Rename

Copy the Tier 1 files into `libs/markoff/src/editor/`:
```
src/editor/
├── MarkoffEditor.h          (from qplaintextedit.h)
├── MarkoffEditor.cpp         (from qplaintextedit.cpp)
├── MarkoffEditor_p.h         (from qplaintextedit_p.h)
├── MarkoffTextControl.h      (from qwidgettextcontrol_p.h)
├── MarkoffTextControl_p.h    (from qwidgettextcontrol_p_p.h)
└── MarkoffTextControl.cpp    (from qwidgettextcontrol.cpp)
```

### Step 2: Rename Classes

- `QPlainTextEdit` → `MarkoffEditor`
- `QPlainTextEditPrivate` → `MarkoffEditorPrivate`
- `QPlainTextEditControl` → `MarkoffEditorControl`
- `QWidgetTextControl` → `MarkoffTextControl`
- `QWidgetTextControlPrivate` → `MarkoffTextControlPrivate`
- `QPlainTextDocumentLayout` → `MarkoffDocumentLayout`

### Step 3: Decouple from Qt Private API

The forked code uses Qt's private headers (`_p.h` files). We need to:
- Replace `Q_D()` / `Q_Q()` macros with direct member access (or keep
  the pimpl pattern with our own private classes)
- Replace `QWidgetPrivate` base class usage with our own base
- Remove `Q_DECLARE_PRIVATE` / `Q_DECLARE_PUBLIC` or redefine them
- Replace internal Qt type references with public equivalents where possible

This is the most tedious part of the fork but it's mechanical, not creative.

### Step 4: Strip Unnecessary Code

Remove from the fork:
- Rich text toggle logic (we're always in markdown mode)
- HTML paste handling (we paste as markdown)
- `setAcceptRichText()` and related API
- Tab handling for QTextTable navigation (we'll add our own table handling)
- Auto-bullet formatting (we'll handle list formatting in the AST)

### Step 5: Add Markdown Machinery

This is where the real work begins — adding the markdown-specific features:

**In MarkoffTextControl:**
- `paintContents()`: for each visible block, check `MarkoffBlockData::isRendered`.
  If rendered, call `MarkoffRenderer::paintBlock()`. If raw, call the original
  text painting code.
- `keyPressEvent()`: intercept markdown commands (Ctrl+B → toggle `**`, etc.)
- `cursorPositionChanged()`: update block rendering modes based on cursor
  proximity. Blocks near cursor → raw mode. Blocks far → rendered mode.
  Trigger re-paint for blocks that changed mode. This recalculation must
  happen on EVERY cursor movement — not deferred (see "Cursor-Aware
  Decoration Timing" below).
- `insertFromMimeData()`: smart paste handling (see "Paste Subsystem" below).

**In MarkoffDocumentLayout:**
- `blockBoundingRect()`: return the rendered height for rendered blocks,
  text height for raw blocks.
- `documentChanged()`: when blocks change, re-parse affected blocks, update
  AST, invalidate render cache, re-compute heights.

### Step 6: Zero-Width Space Cursor Stability

A critical technique borrowed from Obsidian's Live Preview implementation:
when markdown syntax characters are hidden in rendered mode, they must NOT
be removed from the text or skipped during layout. Instead, they are rendered
as **zero-width spaces** — invisible but still present in the text stream.

This matters because `QTextCursor` positions are byte offsets into the
`QTextDocument`. If we skip or delete syntax characters when rendering, every
cursor position after the hidden text shifts, breaking:
- Cursor placement (click on rendered text → wrong source position)
- Selection ranges (off by N characters where N = hidden syntax length)
- Undo/redo (recorded positions no longer match)
- Source position mapping (AST offsets become invalid)

**Implementation in our fork:**

In `paintContents()`, when rendering a block in live preview mode:
- The underlying text remains unchanged (raw markdown with all syntax chars)
- The paint code uses `QTextLayout` with custom format ranges that set
  hidden syntax characters to zero-width:
  ```cpp
  QTextCharFormat hiddenFormat;
  hiddenFormat.setFontLetterSpacing(0);
  hiddenFormat.setFontWordSpacing(0);
  hiddenFormat.setFontStretch(0);   // zero-width rendering
  hiddenFormat.setForeground(Qt::transparent);
  ```
- The cursor continues to navigate through all characters (including hidden
  ones), but hidden characters are treated as atomic ranges — arrow keys
  skip over them as a unit rather than stepping through each hidden character.

This is the same approach Obsidian uses with CodeMirror 6's AtomicRanges
extension. In our forked `MarkoffTextControl`, we implement equivalent
behavior by overriding cursor movement to skip hidden ranges.

### Cursor-Aware Decoration Timing

Obsidian's Live Preview recalculates which ranges to hide/show on EVERY
cursor movement and selection change — not on a deferred timer. This is
essential for the responsive feel of the editor.

In our fork, the sequence is:
1. Cursor moves (key press, mouse click, or selection change)
2. `cursorPositionChanged()` fires
3. Immediately determine which blocks/inline ranges should transition
   between raw and rendered mode
4. Update `MarkoffBlockData::isRendered` for affected blocks
5. If any block's rendered state changed, update its height in
   `MarkoffDocumentLayout` and trigger a viewport repaint

This must be synchronous — no `QTimer::singleShot()` deferral. The user
must see the mode transition on the same frame as the cursor movement.

### Paste Subsystem

Obsidian's paste handling is substantially richer than plain text paste.
Our forked `insertFromMimeData()` must handle several scenarios:

**HTML paste → markdown conversion:**
When the clipboard contains HTML (e.g., copying from a web page), convert
it to markdown rather than inserting raw HTML. This requires an HTML-to-
markdown converter (similar to Turndown.js, but in C++). The conversion
should handle:
- `<b>`/`<strong>` → `**text**`
- `<i>`/`<em>` → `*text*`
- `<a href>` → `[text](url)`
- `<img>` → `![alt](src)`
- `<h1>`-`<h6>` → `#` headings
- `<ul>`/`<ol>` → markdown lists
- `<table>` → pipe tables
- `<code>`/`<pre>` → backtick code
- `<blockquote>` → `>` blockquotes

**Image paste → save and embed:**
When the clipboard contains image data (e.g., screenshot), save the image
to the vault's attachment directory and insert `![[image-name.png]]` at the
cursor. The filename should be auto-generated (timestamp or content hash).
This requires a callback to the application layer for file I/O (the editor
library doesn't know about vaults).

**URL paste over selection:**
When the clipboard contains a URL and text is selected, wrap the selection
as a markdown link: `[selected text](pasted-url)`. If no text is selected,
insert the bare URL or `[url](url)`.

**URL paste for media:**
When pasting a URL that looks like an image/video/audio URL (by extension),
insert as an embed: `![](url)` or appropriate media embed syntax.

**Modifier key behavior:**
- Plain paste: smart paste (HTML conversion, URL detection)
- Ctrl+Shift+V (or equivalent): paste as plain text, no conversion

This subsystem is a distinct component — `MarkoffPasteHandler` — injected
into the text control, not inlined in `insertFromMimeData()`.

---

## Advantages of This Approach

1. **We start with 7,766 lines of proven code** — cursor, selection, IME,
   clipboard, undo, scrolling, accessibility all work on day one.

2. **We modify internals, not fight the API** — block heights, paint decisions,
   and input handling are changed where they're implemented, not wrapped from
   outside.

3. **Source of truth is always markdown** — the `QTextDocument` holds raw
   markdown text. No bidirectional sync problem. The AST is a parallel
   structure built from the text.

4. **Three modes fall out naturally:**
   - Source mode: all blocks in raw mode, syntax highlighter active
   - Live preview: cursor-adjacent blocks raw, others rendered
   - Reading mode: all blocks rendered, input disabled

5. **Canvas cards work too** — the `MarkoffRenderer` that paints blocks in the
   editor also implements the `MarkdownRenderEngine` interface for canvas cards.

6. **Incremental by design** — `QTextDocument` already emits `contentsChanged`
   with affected block ranges. We re-parse only those blocks.

7. **We own the code** — no upstream dependency on qmarkdowntextedit. No
   submodule to maintain. No API limitations to work around. GPL allows it.

---

## Risks and Mitigations

### Risk: Qt Private API Decoupling

The forked code depends on Qt private headers. When we copy it out, those
references break.

**Mitigation:** The private API usage is mostly the pimpl pattern
(`Q_D()`/`Q_Q()`) and some internal type casts. We can:
1. Replace the pimpl pattern with our own (same pattern, our class names)
2. Use public Qt API equivalents where they exist
3. Keep minimal private API references where necessary (with `QT_BEGIN_NAMESPACE`
   + forward declarations), accepting the coupling

### Risk: Qt Version Drift

If Qt changes the internal structure of `QTextDocument` or `QTextLayout`, our
forked code might need updates.

**Mitigation:** We depend on Qt's PUBLIC API for `QTextDocument`, `QTextCursor`,
`QTextLayout`, `QTextBlock`. The forked code (widget + text control) only needs
to call these public APIs. Internal Qt changes to the document model don't
affect us because we use the public interface.

### Risk: Maintaining the Fork

~7,766 lines of forked code is a maintenance burden.

**Mitigation:** This is a one-time fork, not an ongoing sync. We're not
tracking Qt upstream — we're taking a snapshot and evolving it independently.
The forked code becomes OUR code. Over time it will diverge significantly
from Qt's version as we add markdown-specific features. That's the point.

---

## Updated Option C Assessment

| Criterion | Original Option C | Option C with Qt Harvest |
|-----------|-------------------|-------------------------|
| **Time to MVP** | ~6-12 months | **~2-3 months** |
| **Time to polished** | ~18+ months | **~6 months** |
| **Text editing** | Must implement | **Already implemented** |
| **Risk level** | Very High | **Medium** |
| **Rendering flexibility** | Maximum | Maximum |
| **Long-term maintenance** | Very High | **Medium** (we own the code) |

Option C is now the recommended approach.
