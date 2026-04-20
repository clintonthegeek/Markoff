# Appendix D: Official Help & Sandbox Vault

Agent report: complete reading of the Obsidian help repo's Sandbox vault (canonical formatting examples) and key documentation pages.

---

## Sandbox Vault Files (32 files)

The Sandbox vault ships with every Obsidian installation and contains canonical examples of every formatting feature.

### Root Files

**Start here.md** — Welcome page with navigation links using `-> [[note|display text]]` syntax, callout examples (`> [!Warning]`, `> [!Note]-`)

**Plugins make Obsidian special for you.md** — Core vs community plugins description

**Vault is just a local folder.md** — Block references with `^0f681f`, collapsible callout `> [!Note]-`, SUMMARY callout type

### Formatting/ (21 files — the canonical formatting examples)

#### Blockquote.md
```markdown
> Human history is the long terrible story of man trying to find something other than God which will make him happy.
> \- C.S. Lewis
```

#### Callout.md — 12 callout types with aliases
| Type | Aliases |
|------|---------|
| `note` | — |
| `abstract` | `summary`, `tldr` |
| `info` | `todo` |
| `tip` | `hint`, `important` |
| `success` | `check`, `done` |
| `question` | `help`, `faq` |
| `warning` | `caution`, `attention` |
| `failure` | `fail`, `missing` |
| `danger` | `error` |
| `bug` | — |
| `example` | — |
| `quote` | `cite` |

Foldable with `+`/`-`. Custom CSS with `--callout-color` and `--callout-icon`.

#### Code block.md
- Language-specified fenced code blocks (` ```js `)
- Tab-indented code blocks (4 spaces)

#### Comment.md
- Inline: `%%inline%%`
- Block: `%%\nblock content\n%%`
- Visible in editing view, hidden in reading view

#### Diagram.md
- Mermaid diagrams (sequenceDiagram, graph TD)
- `internal-link` class for linking nodes to Obsidian notes
- Special character handling: `"special char"` in node labels

#### Embeds.md
- `![[note]]` embed syntax

#### Emphasis.md
- `*italic*`, `_italic_`
- `**bold**`, `__bold__`
- Combined: `_You **can** combine them_`

#### Footnote.md
- Named: `[^1]`, `[^bignote]`
- Multi-paragraph: indented content under the footnote definition
- Inline: `^[This is inline.]`
- **Inline footnotes only work in reading view**

#### Format your notes.md
- Master page that embeds ALL other formatting pages via `![[note]]`

#### Heading.md
- `#` through `######` (h1-h6)

#### Highlighting.md
- `==highlight text==`

#### Horizontal divider.md
- `***`, `---`, `___`

#### Images.md
- `![alt](url)`
- Resizing: `![alt|200](url)` (width only)

#### Inline code.md
- Single backticks: `` `code` ``

#### Internal link.md
- `[[note]]` wikilink syntax

#### Links.md
- External: `[text](url)`
- Auto-linking: bare `http://...` URLs
- Obsidian URI: `obsidian://open?vault=...&file=...`
- Escaping spaces: `%20` or `< >` angle brackets

#### Lists.md
- Unordered: `-`
- Ordered: `1.`
- Nested lists (any type under any other)

#### Math.md
- Block: `$$..$$` (vmatrix example)
- Inline: `$...$`
- MathJax/LaTeX rendering

#### Strikethrough.md
- `~~text~~`

#### Table.md
- Pipe tables with alignment (`:---`, `:---:`, `---:`)
- Escaped pipe in wikilinks within tables: `[[note\|alias]]`

#### Task.md
- `- [x]` checked, `- [ ]` unchecked
- `- [?]` — any character counts as complete
- Tasks support formatting, tags, and links inline

---

## Key Documentation Pages

### Basic formatting syntax.md

**Paragraphs and Line Breaks:**
- Blank lines separate paragraphs
- Line breaks: 2 trailing spaces or Shift+Enter
- Strict line breaks setting

**Headings:** h1-h6 with `#`

**Inline Formatting:**
| Style | Syntax |
|-------|--------|
| Bold | `**text**` |
| Italic | `*text*` or `_text_` |
| Strikethrough | `~~text~~` |
| Highlight | `==text==` |
| Bold + italic | `***text***` |
| Nested | `**Bold and _nested italic_**` |

**Escaping:** Backslash before special characters: `\*`, `\_`, `\#`, `` \` ``, `\|`, `\~`, `1\.`

**Internal Links:**
- Wikilink: `[[note]]`
- Markdown: `[text](note.md)`

**External Links:**
- `[text](url)`
- URI escaping with `%20` and `< >`

**External Images:**
- `![alt](url)`
- Dimensions: `|640x480` or width-only `|100`

**Lists:**
- Unordered: `-`, `*`, `+`
- Ordered: `1.` or `1)`
- Task lists: `- [x]`, `- [ ]`, `- [?]`, `- [-]`
- Nesting any list type under any other

**Horizontal Rules:** `***`, `---`, `___`

**Code:**
- Inline: backticks, double backticks for containing backticks
- Fenced: triple backticks or tildes, language identifier, syntax highlighting
- Nesting: 4+ backticks wrapping 3-backtick blocks

**Footnotes:**
- `[^1]`, `[^note]`
- Inline: `^[text]` (reading view only)

**Comments:**
- Inline: `%%inline%%`
- Block: `%%\nblock\n%%`

### Advanced formatting syntax.md

**Tables:**
- Pipes `|`, hyphens `-`, alignment with colons
- Escaped pipes for aliases/image sizing: `\|`
- Right-click context menu for table manipulation in Live Preview

**Mermaid Diagrams:**
- sequenceDiagram, graph TD
- `internal-link` class for Obsidian note linking

**Math (MathJax):**
- Block `$$...$$`, inline `$...$`

### Callouts.md

**Syntax:** `> [!type] Title`

**13 supported types with aliases** (see table above)

**Features:**
- Foldable: `+` (expanded) or `-` (collapsed) after type identifier
- Nested callouts: multiple `>` levels
- Custom CSS: `.callout[data-callout="type"]` with `--callout-color: r,g,b` and `--callout-icon: lucide-id` or SVG
- Insert callout command; right-click to change type in Live Preview

### Properties.md

**Format:** YAML frontmatter between `---` delimiters

**Adding:** Cmd/Ctrl+`;`, "Add file property" command, or typing `---`

**Property Types:**
| Type | Format |
|------|--------|
| Text | `key: value` |
| List | `key: [a, b]` or YAML list |
| Number | `key: 42` |
| Checkbox | `key: true` / `key: false` |
| Date | `key: YYYY-MM-DD` |
| Date & time | `key: YYYY-MM-DDTHH:MM:SS` |
| Tags | `tags: [tag1, tag2]` |

**Default Properties:**
- `tags` — searchable labels
- `aliases` — alternative note names for link suggestions
- `cssclasses` — CSS classes for styling

**Deprecated:** `tag`, `alias`, `cssclass` (singular forms)

**Internal links in properties must be quoted:** `"[[Link]]"`

**Display modes:** Visible, Hidden, Source

**Navigation:** Tab, Shift+Tab, arrows for property editing

### Tags.md

- `#tag` syntax in body, `tags:` list in YAML
- Nested: `#inbox/to-read`
- Valid chars: letters, numbers, `_`, `-`, `/`, Unicode/emoji
- Must contain at least one non-numerical character
- Case-insensitive

### Editing shortcuts.md

Complete keyboard shortcut tables for Windows/Linux and macOS:
- Common: copy, cut, paste, undo, redo
- Text editing: delete word, delete line
- Text navigation: word, line, page, note boundaries
- Text selection: extend selection by word, line, etc.

### Multiple cursors.md

- Alt+click (Option+click on macOS) for additional cursors
- Shift+Alt+drag (Shift+Option+drag on macOS) for rectangular selection
- Middle mouse button drag for rectangular selection

### Folding.md

- Fold headings and indented lists via hover arrow
- Settings: Fold indent, Fold heading
- Commands: "Fold all headings and lists", "Unfold all headings and lists"
- Hotkey-assignable: "Fold less", "Fold more"

### Views and editing mode.md

- Three modes: Reading view, Live Preview, Source mode
- Reading view: Ctrl+E toggle
- Live Preview: inline formatting, syntax visible at cursor
- Source mode: raw Markdown
- Ctrl+click view switcher for side-by-side reading+editing
- Settings: Default view for new tabs, Default editing mode

### Obsidian Flavored Markdown.md

- Based on CommonMark + GitHub Flavored Markdown + LaTeX
- **No Markdown inside HTML elements** (intentional)
- Supported extensions: `[[Link]]`, `![[Link]]`, `![[Link#^id]]`, `^id`, `[^id]`, `%%Text%%`, `~~Text~~`, `==Text==`, code blocks, task lists, callouts, tables

### HTML content.md

- Sanitized HTML support
- No Markdown rendering inside HTML blocks
- HTML blocks must be self-contained (no blank lines within)
- Supported: `<!-- comments -->`, `<u>`, `<span>`/`<div>` with inline styles, `<s>`, `<iframe>`

### Internal links.md

- Wikilink: `[[note]]` or `[[note.md]]`
- Markdown: `[text](note.md)` (URL-encoded spaces)
- Link to heading: `[[note#heading]]`, same-note `[[#heading]]`, subheadings `[[note#h1#h2]]`
- Search headers across vault: `[[## search]]`
- Link to block: `[[note#^block-id]]`, auto-suggest on `^`
- Block identifiers: `^id` at end of paragraph (with space); separate line for structured blocks
- Display text: `[[note|display]]` (wikilink), `[display](note.md)` (markdown)
- Invalid chars in links: `# | ^ : %% [[ ]]`

### Embed files.md

- `![[note]]` for notes, `![[note#^block]]` for blocks
- Images: `![[image.jpg]]`, resize `![[image.jpg|100x145]]` or `![[image.jpg|100]]`
- External images: `![width](url)`
- Audio: `![[audio.ogg]]`
- PDF: `![[file.pdf]]`, specific page `#page=N`, height `#height=N`
- Lists via block reference: `![[note#^list-id]]`
- Search results: ` ```query ` code block

### Embed web pages.md

- `<iframe src="URL"></iframe>`
- YouTube: `![](https://www.youtube.com/watch?v=ID)`
- Twitter/X: `![](https://twitter.com/user/status/ID)`

### Aliases.md

- YAML `aliases:` list property
- Auto-generates `[[Real Name|Alias]]` when linking via alias
- Unlinked mentions via Backlinks panel

### Hotkeys.md

- Customizable keyboard shortcuts for commands
- Assign via Settings > Hotkeys, multiple combos per command
- Displayed as US keyboard layout

### Drag and drop.md

- Drag tabs to rearrange/split
- Drag from file explorer, search, backlinks, in-note links
- Drop on: tab header (Alt/Shift for anywhere), folder, editor (inserts link), Bookmarks
- External HTML auto-converts to Markdown
- External files: imported to attachment folder (Ctrl/Option for `file://` links)
- Dragging out creates `obsidian://` URL

### Search.md

- Ctrl+Shift+F to open
- Terms: word matching, `"exact phrase"`, `OR`, parentheses, `-` negation, `<`/`>` operators
- Operators: `file:`, `path:`, `content:`, `match-case:`, `ignore-case:`, `tag:`, `line:`, `block:`, `section:`, `task:`, `task-todo:`, `task-done:`
- Property search: `[property]`, `[property:value]`, `[property:null]`
- Regex: `/pattern/`
- Sort: filename, modified time, created time (asc/desc)
- Embedded search: ` ```query ` code block

### Canvas.md

- `.canvas` files using JSON Canvas format
- Card types: text, note, media, web page, folder
- Text cards support Markdown/links/code blocks
- Selection: click, drag select, Shift+click, Ctrl+A
- Arrange: drag, Alt+drag to duplicate, Shift for axis lock, Space to disable snap
- Resize: drag edges, Shift for aspect ratio
- Connections: drag from edge circles, labels (double-click), colors
- Groups: create, rename (double-click)
- Navigation: Space+drag or middle mouse for pan, Shift+scroll for horizontal, Space/Ctrl+scroll for zoom
- Zoom: fit (Shift+1), to selection (Shift+2), reset

### Templates.md

- Template folder setting
- Variables: `{{title}}`, `{{date}}`, `{{time}}`
- Format strings: `{{date:YYYY-MM-DD}}` using Moment.js tokens
- Template properties merge with note properties
- Commands: "Insert template", "Insert current date", "Insert current time"
