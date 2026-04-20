# Obsidian Editor Internals: Technical Reference

Deep technical analysis of how Obsidian's editor works under the hood.
Intended as an implementation reference for building a native C++/Qt
alternative (Corbomite). Complements obsidian-editor-ux-reference.md which
covers user-facing behavior.

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [CodeMirror 6 Integration](#2-codemirror-6-integration)
3. [Markdown Parsing: Lezer](#3-markdown-parsing-lezer)
4. [Live Preview Implementation](#4-live-preview-implementation)
5. [Cursor and Selection Behavior](#5-cursor-and-selection-behavior)
6. [Decoration System](#6-decoration-system)
7. [Rendering Pipelines: Live Preview vs Reading View](#7-rendering-pipelines-live-preview-vs-reading-view)
8. [Paste Behavior](#8-paste-behavior)
9. [Auto-Complete and Suggesters](#9-auto-complete-and-suggesters)
10. [Drag and Drop](#10-drag-and-drop)
11. [Auto-Pairing and List Continuation](#11-auto-pairing-and-list-continuation)
12. [Undo/Redo](#12-undoredo)
13. [Formatting Hotkeys](#13-formatting-hotkeys)
14. [Performance and Large Files](#14-performance-and-large-files)
15. [Mobile vs Desktop](#15-mobile-vs-desktop)
16. [Plugin API: What It Reveals About Architecture](#16-plugin-api-what-it-reveals-about-architecture)
17. [HyperMD Heritage](#17-hypermd-heritage)
18. [Implications for a Native Qt Implementation](#18-implications-for-a-native-qt-implementation)

---

## 1. Architecture Overview

Obsidian is an Electron app. The editor is a web-based CodeMirror 6 instance
running inside a Chromium renderer process. The architecture follows a
hub-and-spoke pattern centered on an `App` class that acts as a service
locator, providing access to: `workspace`, `vault`, `metadataCache`,
`fileManager`, `keymap`, `scope`, and `renderContext`.

Key technology stack for the editor:
- **CodeMirror 6** -- the editor engine (both source mode and live preview)
- **Lezer** -- incremental parser for markdown (CM6's parser framework)
- **lezer-markdown-obsidian** -- Obsidian-specific Lezer extensions
- **remark / unified** -- markdown processing for reading view
- **MathJax** -- LaTeX math rendering
- **Mermaid** -- diagram rendering
- **Prism** -- syntax highlighting in reading view
- **DOMPurify** -- HTML sanitization

The `Editor` interface abstracts over CM6, providing a high-level API for
content manipulation, cursor/selection management, and transactions. Plugins
can also access the raw CM6 `EditorView` via the `cm6` property when they
need lower-level control.

### Platform Abstraction

File system access is abstracted through a `DataAdapter` interface:
- Desktop: `FileSystemAdapter`
- Mobile: `CapacitorAdapter`

This allows identical editor code to run on both platforms.

---

## 2. CodeMirror 6 Integration

### Why CM6

Obsidian originally used CodeMirror 5 for the desktop editor and CodeMirror 6
for mobile (since CM6 is one of the few code editors that works well on
mobile). With the Live Preview feature (v0.13), desktop was migrated to CM6 as
well. The legacy CM5 editor remained available as "Legacy Editor" in settings
to support older plugins, but has since been removed.

### Extension Architecture

CM6 is a minimal core with a composable extension system. Obsidian layers its
functionality as CM6 extensions:

- **State Fields** -- manage custom editor state that survives document
  changes and updates through transactions. Used when decorations affect
  vertical layout (block widgets, line decorations).
- **View Plugins** -- respond to viewport changes and the visible editor
  region. Used for performance-sensitive decorations that only need to track
  what's visible.
- **Facets** -- CM6's dependency injection / configuration system.
  Decorations are provided through `EditorView.decorations` facet.
- **Transactions** -- all state changes go through transactions, enabling
  atomic multi-operation edits.

### Critical Version Constraint

Obsidian re-exports CM6 modules. Plugins MUST use the exact same CM6 classes
that Obsidian uses. Importing a different version of `@codemirror/state` or
`@codemirror/view` causes silent failures because JavaScript `instanceof`
checks fail across module duplicates. This is an important architectural
detail: CM6 is a singleton within the Obsidian process.

### Exposed StateFields

Obsidian exports specific StateField instances for plugin use:
- `editorEditorField: StateField<EditorView>` -- access to the CM6 EditorView
- `editorInfoField: StateField<MarkdownFileInfo>` -- file and editor context
- `editorLivePreviewField: StateField<boolean>` -- whether Live Preview is active

These allow plugins (and Obsidian's own code) to read editor context and
conditionally branch behavior.

---

## 3. Markdown Parsing: Lezer

Obsidian uses Lezer, CodeMirror 6's incremental parser framework (inspired by
tree-sitter but designed for the web). The `lezer-markdown-obsidian` package
extends `@lezer/markdown` with Obsidian-specific syntax:

### Obsidian Lezer Extensions

| Extension | Syntax | Notes |
|-----------|--------|-------|
| **Comment** | `%%comment%%` | Block comments at line start span to `%%` or EOF; inline must close on same line |
| **Footnote** | `[^1]` references, `[^1]: text` definitions | Definitions can span multiple lines |
| **Hashtag** | `#tag`, `#nested/tag` | Excludes certain special characters |
| **InternalLink** | `[[File#heading\|display]]` | Supports embeds `![[...]]`, headings `#heading`, block IDs `#^blockid` |
| **Mark** | `==highlighted==` | Highlight syntax |
| **TaskList** | `- [ ]`, `- [x]` | Any character in brackets, not just `x` |
| **Tex** | `$inline$`, `$$block$$` | LaTeX math |
| **YAMLFrontmatter** | `---` delimited YAML at document start | |

The parser produces a syntax tree that CM6 decorations and Live Preview
consult to determine what to render and what to show as source.

### Incremental Parsing

Lezer's key advantage is incremental re-parsing. When the document changes,
only the affected region of the syntax tree is re-parsed, not the entire
document. This is critical for performance in large files.

---

## 4. Live Preview Implementation

Live Preview is the core innovation of Obsidian's editor. It provides a hybrid
WYSIWYG/source editing experience where markdown syntax is hidden until the
cursor enters the relevant region.

### Core Mechanism

Live Preview works by applying CM6 **decorations** that hide or replace
markdown syntax tokens when the cursor is NOT in/near them. When the cursor
enters a decorated region, the decorations are removed and raw source is
revealed.

The implementation uses the Lezer syntax tree to identify markdown tokens,
then generates decoration sets that conditionally hide formatting characters.

### Rendering Granularity

Live Preview uses **mixed granularity** depending on element type:

#### Inline Elements (bold, italic, links, code spans, highlights)
- **Granularity: per-element / per-syntax-token**
- The formatting characters (e.g., `**`, `*`, `` ` ``, `[[`, `]]`) are hidden
  via decorations
- When cursor enters the decorated range, those specific decorations are
  removed, revealing the syntax characters
- The rendered text remains visible; only the syntax delimiters toggle
- Example: `**bold**` shows as **bold** normally. Cursor entering shows
  `**bold**` with the asterisks visible

#### Block Elements (tables, callouts, code blocks, math blocks, embeds)
- **Granularity: per-block**
- The entire block is replaced with a rendered widget (block decoration)
- When the cursor enters the block, the widget disappears and the full source
  markdown is revealed for editing
- Tables: the entire table renders as a formatted HTML table; cursor entry
  reveals all the pipe-delimited source lines
- Code blocks: rendered with syntax highlighting; cursor entry shows the
  fenced code block source
- Math blocks: rendered via MathJax; cursor entry shows the `$$...$$` source

#### Images
- Rendered per-image-link (not per-block)
- Image displays alongside source markdown when cursor is on the image link
  line, creating a hybrid view
- The image continues rendering even when its link syntax is visible

### Detection: How Decorations Know About the Cursor

The decoration generation process:
1. Walk the Lezer syntax tree for the viewport
2. For each markdown token that should be hidden/rendered:
   - Check if the current selection ranges overlap with the token's range
   - If overlap: skip the decoration (show raw source)
   - If no overlap: apply hide/replace decoration
3. Regenerate decorations on every selection change (cursor movement)

The selection check typically uses:
```
for (const range of state.selection.ranges) {
  // Filter out decorations where from/to overlap with cursor range
  if (to < range.from || from > range.to) {
    // No overlap -- keep the decoration (hide the syntax)
  } else {
    // Overlap -- remove the decoration (show the syntax)
  }
}
```

### Zero-Width Space Technique

Inactive markdown tokens (syntax characters that are being hidden) are
replaced with zero-width spaces rather than being removed entirely. This
maintains layout consistency and prevents cursor positioning issues.

---

## 5. Cursor and Selection Behavior

### Cursor Near Hidden Syntax

When syntax characters are hidden via decorations, the cursor must still
behave correctly. Obsidian uses several CM6 mechanisms:

- **`EditorView.atomicRanges`** -- makes a range behave as an indivisible
  unit. The cursor skips over it during arrow-key navigation. Used for syntax
  tokens that are completely hidden (e.g., the `**` in bold text when cursor
  is elsewhere). Side effect: backspace deletes the entire atomic range at
  once.

- **`drawSelection`** -- CM6's custom cursor/selection rendering. Essential
  when hiding syntax with CSS, because the native browser cursor can become
  misaligned or invisible when content is hidden via CSS transforms. Obsidian
  uses `drawSelection` to render its own cursor, avoiding browser quirks.

- **CSS-based hiding** -- Some token hiding is done via CSS rather than
  replace decorations. The recommended technique (from the CM6 community) is:
  ```css
  font-family: monospace;
  font-size: 1px !important;
  letter-spacing: -1ch;
  color: transparent;
  ```
  Using `display: none` breaks cursor placement. The CSS approach shrinks
  tokens to near-zero width while keeping them in the DOM flow.

### Cursor Height

CM6 determines cursor height in two modes:
- **Line-max mode**: cursor height equals the tallest element on the line
  (used when inline widgets like rendered math or images are present)
- **Adjacent-character mode**: cursor height based on the adjacent character's
  height (normal text editing)

### Selection Across Rendered/Source Boundaries

- You can select text that spans rendered and source regions
- Copy from Live Preview copies the raw markdown source, not rendered HTML
- Known issue: pasting a URL on selected text in Live Preview does NOT
  auto-create a markdown link (it replaces the text with the URL). The legacy
  CM5 editor did create links. This behavior requires a plugin to restore.

---

## 6. Decoration System

CM6 decorations are the core mechanism for Live Preview rendering. There are
four types:

### Mark Decorations
- Add CSS classes to ranges of text
- Used for: inline formatting styles (bold, italic, strikethrough, code)
- Do not alter document structure
- Example: adding `cm-strong` class to bold text range

### Replace Decorations
- Hide a range of text or replace it with a DOM node
- Used for: hiding syntax delimiters, code folding, replacing content with
  widgets
- `Decoration.replace({ widget: myWidget })` replaces a range with a widget
- Critical: replace decorations that span line breaks CANNOT be provided via
  View Plugins -- they must come from State Fields (because they affect
  vertical layout)

### Widget Decorations
- Insert a custom DOM element at a position in the editor
- Used for: checkboxes in task lists, rendered math, image previews, fold
  indicators
- `Decoration.widget({ widget: myWidgetInstance })`
- Can be inline or block-level

### Line Decorations
- Apply styling to entire lines
- Used for: heading styling, blockquote indentation, list styling

### State Fields vs View Plugins for Decorations

| | State Field | View Plugin |
|---|---|---|
| **Layout impact** | CAN affect vertical layout | CANNOT affect vertical layout |
| **Block widgets** | Yes | No |
| **Line-break-spanning replacements** | Yes | No |
| **Performance** | Recomputes on every transaction | Only recomputes for viewport changes |
| **Use case** | Block elements, tables, code blocks | Inline formatting, syntax hiding |

---

## 7. Rendering Pipelines: Live Preview vs Reading View

Obsidian has two completely different rendering pipelines:

### Live Preview Pipeline (Editing View)
1. **Parser**: Lezer incremental markdown parser (via CM6)
2. **Syntax tree**: CM6 syntax tree (available via `syntaxTree(state)`)
3. **Rendering**: CM6 decorations (mark, replace, widget, line)
4. **Post-processing**: `registerEditorExtension()` -- plugins add CM6
   extensions
5. **DOM**: CM6's virtual DOM within `.cm-editor` container

### Reading View Pipeline
1. **Parser**: remark/unified markdown processing pipeline
2. **Output**: HTML
3. **Post-processing**: `registerMarkdownPostProcessor()` -- plugins modify
   the HTML DOM after rendering
4. **Sanitization**: DOMPurify
5. **Syntax highlighting**: Prism.js (not CM6's Lezer)
6. **DOM**: Standard HTML within `.markdown-preview-view` container

### Key Differences
- `registerMarkdownPostProcessor()` is NOT called in Live Preview mode
- Frontmatter context available in reading view but returns `undefined` in
  Live Preview
- Reading view renders the entire document; Live Preview only renders the
  viewport (plus buffer)
- Different CSS class structure: `.CodeMirror-line` (CM5 legacy) vs
  `.cm-line` (CM6) vs standard HTML elements (reading view)
- The `.is-live-preview` CSS class on `div.markdown-source-view` enables
  mode-specific styling

---

## 8. Paste Behavior

### HTML Paste
- Built-in "Auto convert HTML" setting (Editor settings)
- When enabled: pasting HTML from clipboard converts it to markdown using a
  Turndown-style HTML-to-markdown converter
- Formats preserved: bold, italic, links, headings, lists, tables, code
  blocks, images
- Known issues: conversion is sometimes wrong, especially with complex HTML
- Shift+Ctrl+V (Shift+Cmd+V on macOS): "Paste as plain text" -- bypasses
  HTML conversion

### URL Paste
- Pasting a bare URL: inserts the URL as plain text (no auto-linking)
- Pasting a URL with text selected: in the current editor, this REPLACES the
  selected text with the URL (does NOT auto-create `[text](url)`)
- The auto-link-on-paste behavior requires a plugin ("Paste Link" or "URL
  into Selection")
- Ctrl+K with URL in clipboard and text selected: creates a markdown link
  (built-in)

### Image Paste
- Pasting an image from clipboard: saves the image as a file in the
  configured attachment directory and inserts an embed link `![[image.png]]`
- The attachment directory is configurable in Settings > Files & Links
- File naming: typically `Pasted image YYYYMMDDHHMMSS.png`

### Markdown Paste
- Pasting markdown text: inserted as-is (raw markdown)
- No special markdown-to-markdown conversion

---

## 9. Auto-Complete and Suggesters

### Architecture: EditorSuggest

Obsidian's suggestion system is built on the `EditorSuggest` abstract class
(extends `PopoverSuggest`). The API reveals the architecture:

```typescript
abstract class EditorSuggest<T> extends PopoverSuggest<T> {
  context: EditorSuggestContext | null;

  // Called on every keypress. Return trigger info if suggester should
  // activate, null otherwise. MUST be fast (runs on every keystroke).
  abstract onTrigger(
    cursor: EditorPosition,
    editor: Editor,
    file: TFile
  ): EditorSuggestTriggerInfo | null;

  // Generate suggestions. Can be async but sync preferred for responsiveness.
  abstract getSuggestions(
    context: EditorSuggestContext
  ): T[] | Promise<T[]>;

  // Render a suggestion item in the popover.
  abstract renderSuggestion(value: T, el: HTMLElement): void;

  // Handle selection of a suggestion.
  abstract selectSuggestion(
    value: T,
    evt: MouseEvent | KeyboardEvent
  ): void;
}
```

The `onTrigger` method typically runs a regex on the current line text before
the cursor to detect trigger patterns.

### Built-in Suggesters

#### Link Suggester (`[[`)
- Triggered when user types `[[`
- Shows a popover with matching file names from the vault
- Fuzzy matching against file names, aliases, and paths
- Typing `|` after the file name switches to display text entry
- Typing `#` shows heading completions within the linked file
- Typing `#^` shows block ID completions
- Enter or click: inserts the completed link and closes with `]]`
- Creates new notes if the linked file doesn't exist (on follow)

#### Tag Suggester (`#`)
- Triggered when user types `#`
- Shows existing tags from the vault
- Supports nested tags (`#parent/child`)
- Fuzzy matching

#### Slash Commands (`/`)
- Triggered when user types `/` at the start of a line or after whitespace
- Shows available commands (same pool as command palette)
- Fuzzy matching
- Acts as an in-editor command palette

### Popover Positioning
- The suggestion popover is positioned relative to the trigger text in the
  editor
- Follows the cursor position
- Handles viewport boundaries (flips above if near bottom)

---

## 10. Drag and Drop

### From File Explorer to Editor
- **Default behavior**: creates an embed `![[filename]]`
- File is NOT copied; a link to the existing vault file is created
- The link format follows the vault's "New link format" setting (shortest
  path, relative, absolute)

### From Outside Obsidian to Editor
- **Images/attachments**: file is COPIED into the attachment directory, and an
  embed link is inserted `![[copied-file.png]]`
- **Other files**: copied to attachment directory with a link

### Modifier Keys
- **Ctrl (held during drop)**: creates a `file:///` absolute path link
  instead of copying the file
- **No modifier**: default embed behavior

### Link Format
- Determined by Settings > Files & Links > "New link format"
- Options: "Shortest path when possible", "Relative path", "Absolute path"
- Known issue: drag-and-drop from file explorer may ignore the "New link
  format" setting in some cases

### Between Panes
- Dragging a file tab to another pane moves it
- Dragging a file from the file explorer to a pane opens it there

---

## 11. Auto-Pairing and List Continuation

### Bracket/Quote Auto-Pairing
- Controlled by Settings > Editor > "Auto pair brackets" and "Auto pair
  Markdown syntax"
- Paired characters: `()`, `[]`, `{}`, `""`, `''`, ` `` `, `**`, `__`, `~~`,
  `==`, `$$`
- When typing an opening character: closing character is auto-inserted and
  cursor placed between them
- When typing a closing character that matches the auto-inserted one: cursor
  moves past it (no duplicate)
- Selection + typing opening character: wraps the selection
- Known limitation: quotes don't auto-pair inside parentheses/brackets
- Tab does NOT exit auto-paired brackets (unlike some IDEs)

### List Continuation
- Pressing Enter at the end of a list item: creates a new list item with the
  next marker
- Ordered lists: increments the number
- Unordered lists: repeats the marker (`-`, `*`, `+`)
- Task lists: creates a new unchecked task `- [ ]`
- Pressing Enter on an empty list item: exits the list (removes the marker)
- Tab: indents the list item (increases nesting level)
- Shift+Tab: outdents the list item

### Blockquote Continuation
- Pressing Enter inside a blockquote: continues the `>` prefix
- Pressing Enter on an empty quoted line: exits the blockquote

---

## 12. Undo/Redo

### CodeMirror 6 History

CM6 provides the `history()` extension which manages undo/redo state. Key
behaviors:

- Undo/redo operate on the CM6 transaction history
- CM6 groups changes that occur close together in time into single undo steps
  (configurable `newGroupDelay`, default ~500ms)
- Auto-formatting (list continuation, bracket pairing) creates separate
  transactions that may or may not merge with the preceding edit depending on
  timing

### Known Issues
- Undo sometimes deletes more than expected (e.g., an entire line instead of
  the last few characters). This is a known CM6 behavior related to
  transaction grouping.
- Table editing has reported undo/redo bugs
- On mobile, undo/redo requires toolbar buttons (no keyboard shortcut by
  default)

### Keyboard Shortcuts
- Undo: Ctrl+Z (Cmd+Z on macOS)
- Redo: Ctrl+Y or Ctrl+Shift+Z (Cmd+Shift+Z on macOS)

---

## 13. Formatting Hotkeys

### Built-in Formatting Commands
- **Ctrl+B**: Toggle bold (`**text**`) -- wraps selection or inserts empty
  bold markers
- **Ctrl+I**: Toggle italic (`*text*`) -- always uses asterisks, not
  underscores
- **Ctrl+K**: Insert link -- if URL in clipboard and text selected, creates
  `[text](url)`
- **Ctrl+Shift+K**: Insert code block
- **Ctrl+`**: Toggle inline code

### Formatting Behavior Details
- Only asterisk-based notation is used for bold/italic (never underscores)
- Multi-paragraph selection + bold hotkey: inserts `**` at start of first
  paragraph and end of last paragraph (wraps the whole selection, does NOT
  format each paragraph individually). This differs from word processors.
- No text selected + Ctrl+B: inserts `****` and places cursor in the middle
- The "Format Hotkeys" community plugin provides Google Docs-style behavior
  (per-line formatting, better empty-selection handling)

---

## 14. Performance and Large Files

### Virtual Viewport Rendering

CM6's core performance feature: it only renders DOM for the visible viewport
plus a configurable buffer. This enables handling documents with millions of
lines in theory.

Key mechanisms:
- Only visible lines have DOM nodes
- Scrolling adds/removes DOM nodes as needed
- Line heights are estimated for off-screen content (with correction when
  scrolled into view)
- Decorations from View Plugins only need to cover the viewport

### Known Performance Issues
- **Large files (40k+ lines)**: UI can freeze for seconds when opening,
  jittery scrolling
- **Embedded images (Data URI)**: severe degradation with many large inline
  images
- **File explorer interaction**: large vaults with expanded file lists cause
  editor lag
- **Live Preview overhead**: rendering decorations, widgets, and
  cursor-aware show/hide adds overhead vs. source mode

### Optimization Strategies Used
- Incremental Lezer parsing (only re-parse changed regions)
- Viewport-only decoration generation (View Plugins)
- Lazy loading of syntax highlighting for code blocks
- Document sections are cached for reading view

---

## 15. Mobile vs Desktop

### Historical Context
- Mobile launched with CM6 from the start
- Desktop originally used CM5, migrated to CM6 with Live Preview (v0.13)
- The legacy CM5 editor was kept as "Legacy Editor" for desktop plugin
  compatibility

### Current State (Both CM6)
- Desktop CM6 Editor (Source) is equivalent to Mobile CM6 Editor (Source)
- Desktop CM6 Editor (Live Preview) is equivalent to Mobile CM6 Editor (Live
  Preview)
- Desktop Preview (Reading View) is equivalent to Mobile Preview
- Copy/paste behavior is now identical across platforms

### Mobile-Specific Differences
- No keyboard shortcuts (touch interface)
- Mobile toolbar provides formatting buttons
- Undo/redo via toolbar buttons
- Tap to position cursor (no hover events)
- Suggestion popovers may position differently due to virtual keyboard
- Some community plugins don't support mobile due to CM5 legacy code

---

## 16. Plugin API: What It Reveals About Architecture

The plugin API exposes architectural details about how the editor is built:

### Editor Interface Abstraction

The `Editor` interface is deliberately designed as a drop-in replacement for
CM5's editor object. This means:
- `getValue()` / `setValue()` -- whole-document access
- `getLine()` / `setLine()` -- line-level access
- `getRange()` / `replaceRange()` -- range-based edits
- `getCursor()` / `setCursor()` -- cursor positioning
- `getSelection()` / `replaceSelection()` -- selection manipulation
- `listSelections()` -- multi-cursor support
- `somethingSelected()` -- selection check
- `EditorTransaction` -- atomic multi-operation edits
- Direct CM6 access via `.cm6` property (for advanced use)

### Event-Driven Architecture

All major subsystems extend an `Events` base class:
- `Vault` emits: `create`, `modify`, `delete`, `rename`
- `Workspace` emits: workspace change events
- `MetadataCache` emits: cache update events
- Publish-subscribe pattern with automatic cleanup on plugin unload

### MetadataCache: The Link Graph

The `MetadataCache` maintains indexed, parsed markdown structures:
- Links, embeds, headings, tags, blocks
- `resolvedLinks`: map of valid link destinations
- `unresolvedLinks`: map of broken references
- Frontmatter (parsed YAML properties)
- `getFirstLinkpathDest()`: resolves link paths considering aliases and
  relative paths

This is what powers the link suggester's fuzzy search -- it searches against
the metadata cache, not the file system directly.

### Markdown Post-Processing

Two separate plugin hooks for the two pipelines:
- `registerMarkdownPostProcessor()` -- Reading View only. Receives rendered
  HTML DOM elements and can modify them. Called per section as content scrolls
  into view.
- `registerMarkdownCodeBlockProcessor(language, handler)` -- intercepts
  specific fenced code blocks for custom rendering (e.g., dataview, mermaid)
- `registerEditorExtension()` -- Live Preview / editor. Registers a CM6
  extension that runs in the editor.

### Bases System (v1.10.0+)

A structured data layer providing database-like querying:
- `QueryController` for executing queries against vault files
- Property types: `note` (frontmatter), `formula` (computed), `file`
  (metadata)
- Custom views via `registerBasesView()`

---

## 17. HyperMD Heritage

Obsidian's Live Preview draws heavily from the open-source HyperMD project
(by laobubu), which pioneered WYSIWYG markdown editing on top of CodeMirror.

### HyperMD Architecture
- A set of CodeMirror add-ons, modes, themes, commands, and keymaps
- Modular: features can be loaded independently
- Custom "hypermd" CodeMirror mode extending the standard markdown mode
- "Fold" mechanism: markdown elements visually transform while remaining
  editable source

### HyperMD Key Features (inherited by Obsidian)
- Real-time rendering of bold, italic, strikethrough, links, images
- Clickable links and task list checkboxes
- Footnote hover previews
- Code block syntax highlighting (120+ languages)
- LaTeX formula rendering via MathJax/KaTeX
- Smart paste: HTML-to-markdown conversion
- Image drag-and-drop with upload support
- Table cell navigation (Tab/Shift+Tab)

### HyperMD PowerPacks Pattern
HyperMD uses "PowerPacks" for third-party integrations:
- `fold-math-with-mathjax`: TeX rendering
- `paste-with-turndown`: HTML-to-markdown paste conversion

Obsidian adopted this modular integration pattern, though with its own
implementations for CM6.

---

## 18. Implications for a Native Qt Implementation

### What Must Be Replicated (Core)

1. **Incremental markdown parsing** -- Lezer's role must be filled. Options:
   - Tree-sitter with a markdown grammar (what Neovim/Helix use)
   - A custom incremental parser
   - KSyntaxHighlighting's existing markdown support (limited but available)

2. **Cursor-aware decoration toggling** -- The core Live Preview behavior:
   - Track cursor position against the syntax tree
   - Generate/remove decorations based on cursor proximity
   - Mixed granularity: per-token for inline, per-block for block elements

3. **Two rendering pipelines** -- Live Preview (in-editor decorations) and
   Reading View (full HTML render) are architecturally distinct. They must be
   implemented separately.

4. **Suggestion popover system** -- Triggered by `[[`, `#`, `/` with fuzzy
   matching against the metadata cache.

5. **Paste intelligence** -- HTML-to-markdown conversion, image saving,
   clipboard inspection.

### What Qt Provides Natively

- **QTextDocument/QTextEdit** -- rich text editing with cursor management
- **QSyntaxHighlighter** -- highlight rules, but no decoration
  hiding/replacing
- **QCompleter** -- autocomplete popover (simpler than EditorSuggest)
- **QTextCursor** -- cursor positioning and selection
- **Drag and drop** -- QMimeData, QDropEvent

### Key Architecture Decisions for Corbomite

1. **No CM6 equivalent exists for Qt.** The closest approach is building
   Live Preview as a custom layer on top of QPlainTextEdit or QTextEdit
   using:
   - QSyntaxHighlighter for syntax coloring
   - QTextCharFormat with zero-width font tricks for hiding syntax
   - QTextObjectInterface or custom QTextLayout manipulation for widgets
   - Block-level widgets via QTextDocument's object replacement character

2. **Cursor-aware toggling** must be implemented in the syntax highlighter
   or a parallel decoration system that checks cursor position on every
   cursor movement signal.

3. **The metadata cache** (resolvedLinks, unresolvedLinks) is the data
   structure that powers link suggestions, backlinks, and graph view. It must
   be built and maintained incrementally as files change.

4. **Transaction-based editing** (CM6's model) maps loosely to
   QTextDocument's undo framework, but atomic multi-operation edits need
   explicit `beginEditBlock()` / `endEditBlock()` grouping.

5. **Virtual viewport rendering** is NOT available in QTextEdit by default.
   QPlainTextEdit does some lazy rendering but not to CM6's degree. For very
   large files, a custom viewport-aware text widget may be needed.

---

## Sources

- [Obsidian Developer Docs: Editor Extensions](https://docs.obsidian.md/Plugins/Editor/Editor+extensions)
- [Obsidian Blog: CM6 Migration Guide](https://obsidian.md/blog/codemirror-6-migration-guide/)
- [Obsidian API (DeepWiki)](https://deepwiki.com/obsidianmd/obsidian-api)
- [Obsidian Plugin Development (DeepWiki)](https://deepwiki.com/obsidianmd/obsidian-api/3-plugin-development)
- [Community Guide: How to Update Plugins for Live Preview](https://raw.githubusercontent.com/obsidian-community/obsidian-hub/main/04%20-%20Guides,%20Workflows,%20&%20Courses/Guides/How%20to%20update%20your%20plugins%20and%20CSS%20for%20live%20preview.md)
- [lezer-markdown-obsidian (GitHub)](https://github.com/erykwalder/lezer-markdown-obsidian)
- [HyperMD (GitHub)](https://github.com/laobubu/HyperMD)
- [codemirror-rich-markdoc (GitHub)](https://github.com/segphault/codemirror-rich-markdoc)
- [codemirror-live-markdown Roadmap](https://github.com/blueberrycongee/codemirror-live-markdown/blob/main/ROADMAP.md)
- [CM6 Forum: Hide Markdown Syntax](https://discuss.codemirror.net/t/hide-markdown-syntax/7602)
- [CM6 Forum: WYSIWYG Markdown in CodeMirror](https://discuss.codemirror.net/t/implementing-wysiwyg-markdown-editor-in-codemirror/2403)
- [CM6 Forum: Cursor Movement with Replace Decorations](https://discuss.codemirror.net/t/cursor-movement-with-replacing-decorations/9491)
- [CM6 Forum: Decoration.replace and Cursor Motion](https://discuss.codemirror.net/t/decorations-replace-and-cursor-motion/9330)
- [CM6 Forum: Cursor Misalignment Hiding Symbols](https://discuss.codemirror.net/t/regarding-the-issue-of-cursor-misalignment-when-using-decoration-mark-to-hide-symbols/9354)
- [Obsidian Forum: CM View Plugin vs State Field](https://forum.obsidian.md/t/codemirror-view-plugin-vs-state-field-for-inline-replacements/78108)
- [Obsidian Forum: Custom CM Block Widget](https://forum.obsidian.md/t/how-to-create-custom-codemirror-block-widget/36132)
- [Obsidian Forum: EditorSuggest API](https://docs.obsidian.md/Reference/TypeScript+API/EditorSuggest)
- [Obsidian Forum: Live Preview Image Rendering Granularity](https://forum.obsidian.md/t/live-preview-improve-rendering-of-images-similar-to-tables-and-callouts-render-per-block-rather-than-per-image-link/37557)
- [Obsidian Forum: Paste Behavior](https://forum.obsidian.md/t/make-optional-turn-on-off-paste-that-converts-html-content-to-markdown-links/10096)
- [Obsidian Forum: Auto-Pair Brackets](https://forum.obsidian.md/t/auto-pairing-brackets-how-to-continue/39066)
- [Obsidian Forum: Table Editing in Live Preview](https://forum.obsidian.md/t/how-to-make-markdown-syntax-show-when-editing-tables-in-live-preview/57775)
- [Obsidian EditorSuggest Docs (marcusolsson)](https://marcusolsson.github.io/obsidian-plugin-docs/reference/typescript/classes/EditorSuggest)
- [Obsidian Help: Live Preview Update](https://help.obsidian.md/Live+preview+update)
- [Obsidian Help: Drag and Drop](https://help.obsidian.md/drag-and-drop)
- [CM6 Decoration Example](https://codemirror.net/examples/decoration/)
