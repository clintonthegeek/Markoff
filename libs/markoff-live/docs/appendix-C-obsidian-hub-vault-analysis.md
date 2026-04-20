# Appendix C: Obsidian Hub Vault Analysis

Agent report: exhaustive analysis of the obsidian-hub vault for editor UX details.

---

## 1. Editor Modes

Obsidian has three distinct editing/viewing modes:

### Live Preview (WYSIWYG)
- Introduced in Obsidian v0.13.0 (November 2021) as an "experimental" feature for Insiders. Built on **CodeMirror 6** (CM6), replacing the old CM5-based editor.
- Hides markdown formatting syntax when the cursor is not on the formatted element.
- Renders embedded notes, images, audio, video, PDFs inline using wikilink syntax.
- Renders external images in markdown syntax `![](url)`.
- Renders block `$$` LaTeX (inline LaTeX added later).
- Replaces checkbox `- [ ]` with clickable checkbox widgets. Checkboxes have a `data-task` attribute.
- Renders `---` horizontal rules as visual separators.
- Headings always show the `#` characters when the cursor is on that line.
- Adds external link icon decoration to external URLs.
- Empty headings keep the `#` characters visible so users can see it is an empty heading.
- Internal links starting with `#` (heading sections) hide the `#` when not selected to avoid confusion with tags.
- CSS class `.is-live-preview` on `div.markdown-source-view` differentiates Live Preview from Source mode.
- DOM: inactive markdown tokens are replaced with a zero-width space. Embedded content renders new DOM elements similar to Reading mode counterparts.
- Basic markdown tables render inline (added in v0.14.1).
- Indentation guides (relationship lines) available (added in v0.14.0).

### Source Mode
- Bare-bones CM6 editor with Live Preview disabled.
- Shows all raw markdown syntax at all times.
- `Desktop CM6: Editor (Source)` is identical to `Mobile CM6: Editor (Source)`.

### Reading Mode (formerly "Preview Mode")
- Renamed from "Preview mode" to "Reading view" in v0.13.8 to avoid confusion.
- Fully rendered, non-editable view of the markdown.
- `Desktop: Preview` is identical to `Mobile: Preview`.
- Page preview hover works: hover internal link to see content; in editor mode, hold Ctrl/Cmd while hovering.

### Legacy Editor
- The old CM5 editor was deprecated into a "legacy" mode. CM5 will eventually be dropped.

### Default Mode Setting
- Users can set the default editing mode to Live Preview or Source Mode (added v0.13.8).

---

## 2. Keyboard Shortcuts and Hotkeys

### Core Obsidian Shortcuts
- **Ctrl/Cmd+P**: Open Command Palette (Command Palette core plugin).
- **Ctrl/Cmd+O**: Open Quick Switcher (Quick Switcher core plugin).
- All commands are rebindable through Settings > Hotkeys.

### Formatting Hotkeys
- Bold, italic, strikethrough, highlight, inline code, etc. are applied via commands that wrap selected text.
- When formatting commands are applied, whitespace characters at the beginning or ending of the selection are ignored (added v0.13.9).

### Community Plugin Hotkeys Ecosystem

| Plugin | Purpose |
|--------|---------|
| **Hotkeys++** (`hotkeysplus-obsidian`) | Additional hotkeys for common operations |
| **Format Hotkeys** (`format-hotkeys-obsidian`) | Google Docs style formatting hotkeys |
| **Code Editor Shortcuts** (`obsidian-editor-shortcuts`) | VS Code / Sublime Text style shortcuts |
| **Smarter Markdown Hotkeys** (`obsidian-smarter-md-hotkeys`) | Smart word/line selection before markup, multiple cursor support |
| **Sequence Hotkeys** / **Chorded Hotkeys** | Multi-key sequences (vim leader key style) |
| **Leader Hotkeys** (`leader-hotkeys-obsidian`) | Leader hotkey support (like tmux/vim) |
| **Hotkey Helper** (`hotkey-helper`) | See plugin settings/hotkey assignments and conflicts |

### Slash Commands
- Core plugin `slash-commands`: Typing `/` opens a command menu in the editor.

---

## 3. Markdown Syntax Supported

### Standard Markdown (CommonMark-based)
- Based on **CommonMark** specification.
- Headings: ATX (`#`, `##`, etc.) and **setext** (underline with `---` or `===`).
- Bold: `**text**` or `__text__`
- Italic: `*text*` or `_text_`
- Strikethrough: `~~text~~`
- Code: backtick inline `` `code` `` and fenced code blocks.
- Blockquotes: `>`
- Ordered and unordered lists.
- Links: standard `[text](url)` format.
- Images: `![alt](url)` format.
- Horizontal rules: `---`
- Tables: pipe-delimited markdown tables.

### Obsidian Extensions to Markdown
- **Highlights**: `==highlighted text==`
- **Comments**: `%% text %%` — not rendered, invisible in reading mode.
- **Tags**: `#tagname` — recognized as interactive elements; clicking opens a search.
- **Internal Links (Wikilinks)**: `[[note name]]`
  - Link to headers: `[[note#Heading]]`
  - Link to blocks: `[[note#^blockid]]`
  - Display text (alias): `[[note|display text]]`
  - Markdown link format also supported; configurable via settings.
- **Embeds (Transclusions)**: Prepend `!` to a link:
  - Full note embed: `![[note]]`
  - Header embed: `![[note#Heading]]`
  - Block embed: `![[note#^blockid]]`
- **MathJax/LaTeX**:
  - Inline: `$x^2 + y^2 = z^2$`
  - Block: `$$...$$`
- **Mermaid diagrams**: Rendered from ` ```mermaid ` code blocks.
- **Callouts/Admonitions**: Native since v0.14.0.
- **Checkboxes/Task lists**: `- [ ]` for unchecked, `- [x]` for checked.
  - Alternate checkbox characters supported (e.g., `- [?]`, `- [!]`, `- [/]`).
  - Characters other than `x` do NOT get strikethrough styling (changed v0.14.0).
  - Checkboxes have a `data-task` attribute for CSS styling.

### Lesser-Known Markdown Behavior
- Indented text with a preceding blank line renders as a code block.
- A dash (`-`) or equals (`=`) below text triggers setext heading syntax.
- **Strict Line Breaks** setting: when enabled, single line breaks are ignored; two spaces at end of line create a line break.
- Escaping special characters with backslash: `\*`, `\$`, etc.
- **Loose lists**: single blank lines between list items add extra spacing.
- Line break in table cell: use `<br>`.
- Nested code blocks: use four backticks to wrap a code block containing three backticks.

---

## 4. Editing Features

### Auto-Pairing
- Auto-pairing of brackets for markdown formatting symbols: `$`, `=`, `~`, `%` (added v0.13.3/0.13.4).
- Standard bracket/quote auto-pairing for `(`, `[`, `{`, `"`, `` ` ``.

### Indentation
- Tab key for indentation in lists and code blocks.
- Indentation guides (relationship lines) available in both Live Preview and Reading view (native since v0.14.0).
- CSS class `cm-indent` and `cm-active-indent` for styling indentation guides.

### Folding
- Headings and list items are foldable. Fold markers ("..." ellipsis) show when collapsed.
- CM6 fold placeholder selector: `.cm-foldPlaceholder`.
- Collapse indicators in Live Preview/Source mode are part of the editor gutter, not inline.

### Multiple Cursors
- Supported in CM6 editor. Smarter Markdown Hotkeys plugin explicitly supports multiple cursors.

### Spellchecking
- Built-in spellchecker. Improved in v0.13.8+ with RTL support.

### RTL Support
- Basic right-to-left text support added in v0.13.10. Configurable in Settings > Editor.

### Syntax Highlighting
- Built-in syntax highlighting for code blocks (added by v0.13.14 public release).

### Vim Mode
- Enabled via Settings > Editor > Advanced > Vim key bindings.
- Uses CodeMirror vim emulation (`codemirror-vim`).
- Available in both Live Preview and Source mode (since v0.13.8).
- Recommended companion: **vimrc Support Plugin** (`obsidian-vimrc-support`).

### Line Numbers and Gutters
- Gutter selector in CM6: `.markdown-source-view.mod-cm6 .cm-gutters`.
- Active line: `.cm-active` class.
- Cursor styling: `.cm-s-obsidian .cm-cursor`.
- Trailing whitespace: `.cm-line .cm-trailing-space-new-line` CSS class.

---

## 5. Link Handling

### Wikilinks (default)
- `[[note name]]`: links to a note by name (case-insensitive matching, shortest path).
- `[[note name|alias]]`: displays custom text.
- `[[note name#Heading]]`: deep link to a heading.
- `[[note name#^blockid]]`: deep link to a specific block.
- Setting: `Use [[Wikilinks]]` toggle. Can switch to standard markdown links.

### Link Behavior in Live Preview
- Click on links directly to open them (no modifier key needed).
- On mobile: press and hold to edit the link.
- Ctrl/Cmd+click is used for following links in source mode.

### Page Preview (Hover)
- Core plugin `page-preview`: hover an internal link to see content preview.
- In editor mode, press Ctrl/Cmd while hovering.

### Auto Link Title
- Community plugin: automatically fetches titles from the web for pasted URLs.

### Paste URL into Selection
- Community plugin: paste a URL "into" selected text, creating a markdown link.

---

## 6. Embed Syntax and Behavior

### Basic Embed Syntax
- `![[note]]`: embeds the full note content inline.
- `![[note#Heading]]`: embeds only the content under a specific heading.
- `![[note#^blockid]]`: embeds a specific block.
- `![[image.png]]`: embeds an image from the vault.
- External image: `![alt](url)` renders in Live Preview.
- Audio, video, PDF files can be embedded using the wikilink syntax.

### Block References
- Any paragraph can be referenced by appending `^blockid` to it.
- The `^` syntax creates a block ID, which can be linked to with `[[note#^blockid]]`.

---

## 7. Callouts

### Native Callout Syntax (since v0.14.0)

```markdown
> [!info] Callout title
> Content of callout
```

- The type determines the styling and icon.
- Natively supported on Obsidian Publish.

### Callout Features
- Foldable/collapsible callouts: `> [!info]+ Expandable` or `> [!info]- Collapsed by default`.
- Custom callout types via CSS or plugins.
- Custom icons via `--callout-icon` CSS variable.

---

## 8. Tags, Frontmatter/Properties

### Tags
- Inline tags: `#tagname` anywhere in the note body.
- Clicking a tag opens a search for all notes with that tag.
- Tags can be nested: `#parent/child`.
- Tags are recognized in the frontmatter `tags:` field.

### YAML Frontmatter / Properties
```yaml
---
aliases:
  - alternate name
tags:
  - seedling
publish: true
---
```
- **Aliases**: alternate names for a note; appear in link autocomplete.
- **Tags**: array or inline.
- Custom key-value pairs for Dataview, Templates, etc.

---

## 9. Tables, Code Blocks, Math

### Tables
- Pipe-delimited markdown tables render in both Reading mode and Live Preview (v0.14.1).
- Line breaks within cells: use `<br>`.
- Alignment via colons: `:---`, `:---:`, `---:`.
- Advanced Tables plugin: Tab navigation, auto-formatting, formulas.

### Code Blocks
- Triple backtick syntax with optional language identifier.
- Built-in syntax highlighting (since v0.13.14).
- Custom code block renderers via `registerCodeBlockPostProcessor`.
- Special languages: `mermaid`, `dataview`, `dataviewjs`, `query`.

### Math Blocks (MathJax)
- Inline math: `$expression$`
- Block math: `$$expression$$`
- Extended via plugins for preambles, chemistry, proofs.

---

## 10. Core Plugins with Editor Relevance

| Plugin | ID | Editor Relevance |
|---|---|---|
| Command Palette | `command-palette` | Ctrl/Cmd+P to invoke any command |
| Quick Switcher | `switcher` | Ctrl/Cmd+O to jump to files |
| Editor Status | `editor-status` | Status bar to show/change editor mode |
| Outline | `outline` | Heading outline of current file |
| Page Preview | `page-preview` | Hover preview of internal links |
| Backlinks | `backlink` | Show backlinks in sidebar/status bar |
| Outgoing Links | `outgoing-link` | Show outgoing links + unlinked mentions |
| Note Composer | `note-composer` | Merge, split, and refactor notes |
| Templates | `templates` | Insert template content |
| Slash Commands | `slash-commands` | Type `/` to trigger commands |
| Word Count | `word-count` | Word count in status bar |
| Search | `global-search` | Full-text search across vault |
| File Recovery | `file-recovery` | Restore recent snapshots |
| Slides | `slides` | Present from markdown using `---` separators |

---

## 11. Important Editor Settings

- **Default location for new notes**: configurable to specific folder
- **New link format**: "Shortest path when possible" is recommended
- **Use Wikilinks**: toggle between `[[wikilink]]` and `[](markdown link)` format
- **Strict Line Breaks**: when enabled, single line breaks are ignored
- **Readable Line Length**: controls line width
- **Vim key bindings**: toggle in Editor > Advanced
- **Live Preview / Source Mode default**: configurable
- **Spellcheck**: built-in, toggleable
- **RTL text support**: configurable
- **Default location for attachments**: configurable

---

## 12. CodeMirror 6 Technical Details

### CSS Classes (CM6)
- `.cm-line` replaces `.CodeMirror-line`
- `.cm-active` for active line
- `.cm-s-obsidian .cm-cursor` for cursor
- `.cm-foldPlaceholder` for fold markers
- `.markdown-source-view.mod-cm6 .cm-gutters` for gutters
- `.cm-indent` and `.cm-active-indent` for indentation guides
- `.cm-trailing-space-new-line` for trailing whitespace markers
- `.is-live-preview` on `div.markdown-source-view` distinguishes Live Preview from Source Mode
- `span.external` for external links in Edit Mode vs. `a.external` in Reading Mode

### Environments
- **Desktop CM6: Editor (Source)** == **Mobile CM6: Editor (Source)**
- **Desktop CM6: Editor (Live Preview)** is unique — hybrid of edit + read
- **Desktop: Preview** == **Mobile: Preview**

---

## 13. Notable Community Plugins for Editor Enhancement

| Plugin | Purpose |
|---|---|
| Outliner | Work with lists like Workflowy/RoamResearch |
| Sentence Navigator | Select, move, delete by whole sentences |
| Remember Cursor Position | Persists cursor and scroll position per note |
| Ghost Fade Focus | Fades unfocused lines for distraction-free writing |
| Longform | Novel/screenplay writing support |
| Various Complements | IDE-style word autocompletion from vault content |
| cMenu | Minimal floating text editor toolbar |
| Linter | Enforces consistent markdown styling rules |
| Easy Typing | Auto-capitalize, auto-space as you type |
| Paste to Current Indentation | Paste maintaining indentation level |
| Sort & Permute Lines | Sort selected lines |
| Text Format | Case transformations |
| Footnote Shortcut | Quick footnote insertion |
| Navigate Cursor History | Jump back/forward through cursor positions |
| Lapel | Heading level markers in gutter |
