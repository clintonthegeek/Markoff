# Markoff Phase 1 — Library Foundation Design Specification

## Overview

Markoff is Corbomite's bespoke markdown editing and rendering library. Phase 1
establishes the foundation: a forked Qt text editor widget, MD4C-based parsing,
a reading view renderer, a minimal test application, and a thin adapter that
lets Corbomite use Markoff as a drop-in replacement for its current regex-based
render engine.

Markoff is an independent library. It does not depend on or know about
Corbomite. Corbomite adapts to Markoff, never the other way around. A thin
adapter in Corbomite bridges the two interfaces temporarily until Corbomite
migrates to Markoff's native API.

---

## Project Structure

```
libs/markoff/
├── CMakeLists.txt              # library target: Markoff
├── include/markoff/
│   ├── Document.h              # parsed markdown (opaque AST)
│   ├── Editor.h                # forked text editor widget
│   ├── ReadingView.h           # rendered reading view widget
│   ├── Renderer.h              # Document → QTextDocument rendering
│   └── RenderSettings.h        # rendering configuration
├── src/
│   ├── Document.cpp
│   ├── DocumentBuilder.cpp     # MD4C SAX callbacks → Document AST
│   ├── DocumentBuilder_p.h     # internal header
│   ├── Editor.cpp              # forked from qplaintextedit.cpp
│   ├── Editor_p.h              # forked from qplaintextedit_p.h
│   ├── TextControl.cpp         # forked from qwidgettextcontrol.cpp
│   ├── TextControl.h           # internal header
│   ├── TextControl_p.h         # forked from qwidgettextcontrol_p*.h
│   ├── ReadingView.cpp
│   ├── Renderer.cpp
│   └── vendor/
│       ├── md4c.c              # vendored MD4C parser
│       └── md4c.h
├── tests/
│   ├── CMakeLists.txt
│   └── tst_document.cpp
├── app/
│   ├── CMakeLists.txt          # executable: markoff-testapp
│   ├── main.cpp
│   └── MainWindow.h/cpp
└── docs/                       # existing research documents
```

### Dependencies

- Qt6: Core, Gui, Widgets
- KDE Frameworks 6: SyntaxHighlighting, I18n
- JKQTMathText (for LaTeX math rendering)
- MD4C (vendored, MIT license)

### Build Targets

- `Markoff` — shared library
- `markoff-testapp` — test application executable (not installed)
- `tst_markoff_document` — unit tests

---

## Public API

### Document.h — Parsed Markdown

```cpp
namespace Markoff {

class Document {
public:
    ~Document();

    static std::unique_ptr<Document> fromMarkdown(const QString &markdown);

    QString sourceText() const;
    bool isEmpty() const;
    QString extractSubpath(const QString &subpath) const;

private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace Markoff
```

`Document` is the parsed representation of a markdown string. Phase 1 treats
the internal AST as opaque — consumers create a `Document` and pass it to a
`Renderer` or `ReadingView`. The AST is exposed incrementally in later phases
as the editor needs node-level access.

`fromMarkdown()` parses the input using MD4C and builds the internal AST via
`DocumentBuilder`. Returns a non-null `Document` (empty input produces an
empty document, not null).

`extractSubpath()` extracts a section of the document by heading name
(`"#Introduction"`) or block ID (`"#^block-id"`). Returns the extracted
markdown, or empty string if not found. This is a document-structural
operation that walks the AST, not a regex over the source text.

### RenderSettings.h — Rendering Configuration

```cpp
namespace Markoff {

struct RenderSettings {
    int baseFontSizePt = 14;
    int maxWidthPx = 0;        // 0 = fill container
    int marginPx = 16;
    bool showFrontmatter = false;
    bool renderImages = true;
    bool renderCodeHighlighting = true;
};

} // namespace Markoff
```

Pure rendering configuration. No vault paths, no note paths — those are
application concerns that Markoff does not handle. Corbomite's adapter
translates its `RenderProfile` + `RenderOptions` into `RenderSettings`.

### Renderer.h — Document to QTextDocument

```cpp
namespace Markoff {

class Renderer {
public:
    Renderer();
    ~Renderer();

    void setSettings(const RenderSettings &settings);
    RenderSettings settings() const;

    std::unique_ptr<QTextDocument> renderToTextDocument(
        const Document &doc) const;

private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace Markoff
```

The `Renderer` walks the `Document`'s AST and produces a `QTextDocument` with
HTML content, styled according to the current `RenderSettings`. This is the
primary rendering path for Phase 1 — used by the `ReadingView` and by the
Corbomite adapter for canvas cards.

The `Renderer` is stateless per-call — `setSettings()` configures defaults,
and each `renderToTextDocument()` call uses those settings without side
effects.

### ReadingView.h — Reading Mode Widget

```cpp
namespace Markoff {

class ReadingView : public QWidget {
    Q_OBJECT
public:
    explicit ReadingView(QWidget *parent = nullptr);
    ~ReadingView();

    void setDocument(const Document &doc);
    void setSettings(const RenderSettings &settings);

Q_SIGNALS:
    void linkClicked(const QString &target);

private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace Markoff
```

A non-editable widget that renders a `Document` in reading mode. Internally
uses a `Renderer` and a `QTextBrowser` to display the result. Emits
`linkClicked()` when the user clicks a link (wikilink or standard).

`setDocument()` triggers a re-render. `setSettings()` updates rendering
configuration and triggers a re-render if a document is loaded.

### Editor.h — Forked Text Editor Widget

```cpp
namespace Markoff {

class Editor : public QAbstractScrollArea {
    Q_OBJECT
public:
    explicit Editor(QWidget *parent = nullptr);
    ~Editor();

    void setPlainText(const QString &text);
    QString toPlainText() const;

Q_SIGNALS:
    void textChanged();

private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace Markoff
```

Forked from Qt's `QPlainTextEdit` and `QWidgetTextControl`. Phase 1 is
functionally identical to `QPlainTextEdit` for plain text editing — typing,
cursor navigation, selection, clipboard, undo/redo, scrolling, IME, and
accessibility all work. The API is intentionally narrow; it grows as we add
live preview, syntax highlighting, and markdown-aware editing.

---

## Qt Widget Fork

### Source Files

| Qt Source | Markoff Target | ~Lines |
|-----------|---------------|--------|
| `qplaintextedit.h` | `Editor.h` (public) | 294 |
| `qplaintextedit.cpp` | `Editor.cpp` | 3,231 |
| `qplaintextedit_p.h` | `Editor_p.h` | 159 |
| `qwidgettextcontrol_p.h` | `TextControl.h` | 289 |
| `qwidgettextcontrol_p_p.h` | `TextControl_p.h` | 216 |
| `qwidgettextcontrol.cpp` | `TextControl.cpp` | 3,577 |

Source: `~/src/qtbase/src/widgets/widgets/`

### Decoupling from Qt Private API

The forked code uses Qt's private headers in three ways:

1. **Pimpl macros** (`Q_D`, `Q_Q`, `Q_DECLARE_PRIVATE`): Replaced with our
   own pimpl pattern. The `Private` classes become plain nested structs with a
   back-pointer to the public class. No `QWidgetPrivate` inheritance.

2. **`QWidgetPrivate` base class**: `QPlainTextEditPrivate` inherits through
   `QAbstractScrollAreaPrivate` → `QFramePrivate` → `QWidgetPrivate`. We
   break this chain. `Editor::Private` is a plain struct. For widget
   functionality (viewport, scrollbars), we call through `Editor`'s public
   `QAbstractScrollArea` API.

3. **Internal Qt helpers**: Replaced with public API equivalents or inlined
   where the logic is small.

### Class Renaming

| Qt Class | Markoff Class |
|----------|--------------|
| `QPlainTextEdit` | `Markoff::Editor` |
| `QPlainTextEditPrivate` | `Markoff::Editor::Private` |
| `QPlainTextEditControl` | (merged into Editor) |
| `QWidgetTextControl` | `Markoff::TextControl` |
| `QWidgetTextControlPrivate` | `Markoff::TextControl::Private` |
| `QPlainTextDocumentLayout` | `Markoff::DocumentLayout` |

### What We Strip

- Rich text mode (`setAcceptRichText`, HTML processing)
- `QTextTable` cell navigation (re-added in Phase 2 with atomic blocks)
- Auto-bullet list formatting (replaced by markdown-aware formatting later)
- Placeholder text rendering

### What We Keep Unchanged

- Keyboard input handling + IME composition
- Mouse events + cursor positioning
- Selection (word, line, block, drag, shift-click)
- Clipboard (cut/copy/paste)
- Undo/redo coordination with `QTextDocument`
- Cursor blinking
- Scrollbar management and viewport painting
- Find operations
- Drag-and-drop

### Success Criteria

`Markoff::Editor` compiles, links, and behaves identically to `QPlainTextEdit`
for plain text editing. All standard text editing operations work: type, select,
copy, paste, undo, redo, scroll, find, drag-and-drop.

---

## MD4C Parsing

### Vendoring

MD4C (`md4c.c` + `md4c.h`) is copied into `src/vendor/`. MIT license,
GPL-compatible. Single C file, added directly to the CMake sources.

### Parser Flags

```cpp
MD_DIALECT_GITHUB
| MD_FLAG_WIKILINKS
| MD_FLAG_LATEXMATHSPANS
| MD_FLAG_STRIKETHROUGH
| MD_FLAG_TASKLISTS
| MD_FLAG_TABLES
```

### DocumentBuilder

Internal class (not public API) that receives MD4C SAX callbacks and builds
the `Document`'s internal AST. Follows Penelope's `ContentBuilder` pattern:

- `enter_block(type, detail)` / `leave_block(type)`: push/pop block stack
- `enter_span(type, detail)` / `leave_span(type)`: push/pop inline format stack
- `text(type, text)`: append to current block with current formatting

For Phase 1, the AST is a list of typed block nodes, each containing inline
content. The block types map directly to MD4C's block types. Inline content
is stored as a list of styled text runs.

### Phase 1 Rendering Coverage

**Rendered:**
- Headings (H1-H6)
- Paragraphs with inline formatting (bold, italic, strikethrough, inline code)
- Links (standard markdown and wikilinks as styled text)
- Lists (ordered, unordered, nested)
- Code blocks (with KSyntaxHighlighting)
- Block quotes
- Tables
- Horizontal rules
- Math (via JKQTMathText, rendered as images in QTextDocument)
- Task lists (checkbox characters as text)

**Not rendered (deferred to later phases):**
- Callouts (rendered as plain blockquotes)
- Embeds (shown as link text)
- Mermaid diagrams (shown as code blocks)
- Highlights `==text==` (shown as plain text)
- Comments `%%text%%` (shown as plain text)
- Tags `#tag` (shown as plain text)
- Images (shown as alt text)

---

## Test Application

### MainWindow

A `QMainWindow` with:
- `QSplitter` containing `Markoff::Editor` (left) and `Markoff::ReadingView`
  (right)
- Toolbar: Open, Save, font size spinbox
- Window title: `"Markoff — filename.md"` or `"Markoff — [untitled]"`
- Accepts file path as command line argument

### Data Flow

```
User types in Editor
    → Editor::textChanged()
    → MainWindow::onTextChanged()
    → Document::fromMarkdown(editor->toPlainText())
    → readingView->setDocument(doc)
    → ReadingView re-renders and displays
```

### File I/O

- Open: `QFileDialog::getOpenFileName()` → read file → `editor->setPlainText()`
- Save: `editor->toPlainText()` → write file
- Keyboard shortcuts: Ctrl+O (open), Ctrl+S (save)

### Build

Separate CMake executable target `markoff-testapp`. Depends on `Markoff`
library target. Not installed — development tool only.

---

## Corbomite Adapter

### MarkoffRenderEngine

```cpp
namespace Corbomite {

class MarkoffRenderEngine : public MarkdownRenderEngine {
public:
    std::unique_ptr<RenderedDocument> render(
        const QString &markdown,
        const RenderOptions &options = {}) const override;
};

} // namespace Corbomite
```

### Implementation

1. If `options.subpath` is set, extract the subpath from the markdown using
   the base class `extractSubpath()` static method.
2. Parse: `Markoff::Document::fromMarkdown(markdown)`
3. Translate configuration:
   - `m_profile.baseFontSizePt` → `RenderSettings::baseFontSizePt`
     (with `options.baseFontSizePt` override if set)
   - Same for `maxWidthPx`, `marginPx`
   - Pass through `showFrontmatter`, `renderImages`, `renderCodeHighlighting`
4. Render: `renderer.renderToTextDocument(doc)`
5. Wrap: `RenderedDocument::fromQTextDocument(std::move(textDoc))`

### Integration

Corbomite swaps `RegexRenderEngine` for `MarkoffRenderEngine` at the
injection site (one-line change). All consumers (NotePreviewWidget, canvas
FileCardItem, hover previews) work unchanged.

The adapter ignores `options.vaultRoot` and `options.notePath` in Phase 1 —
Markoff does not resolve links or load embedded content. These are wired in
later phases.

---

## Testing

### Unit Tests

`tst_document.cpp`:
- `Document::fromMarkdown()` produces non-empty document for valid markdown
- `Document::fromMarkdown("")` produces empty document (not null)
- `extractSubpath("#heading")` returns correct section
- `extractSubpath("#^block-id")` returns correct paragraph
- `extractSubpath("#nonexistent")` returns empty string
- Headings, paragraphs, lists, code blocks, tables parsed correctly
- Wikilinks parsed (MD4C flag)
- Math spans parsed (MD4C flag)

`tst_renderer.cpp`:
- `renderToTextDocument()` returns non-null QTextDocument
- Rendered document contains expected HTML structure for each block type
- `RenderSettings` affect output (font size, margins)
- Code blocks receive syntax highlighting
- Math blocks render via JKQTMathText

### Manual Testing

The test application IS the primary testing vehicle. Load markdown files,
verify rendering, exercise the editor. The existing test vaults
(`testvaults/`) provide real-world markdown samples.

---

## What Phase 1 Does NOT Include

- Live preview mode (cursor-aware rendering in editor)
- Syntax highlighting in the editor
- Atomic blocks (tables, code blocks, callouts as interactive widgets)
- Obsidian extension parsing (callouts, highlights, comments, tags, embeds)
- Link resolution or vault awareness
- Image rendering
- Incremental parsing (tree-sitter)
- Multiple cursors, vim mode, folding
- Print/PDF export
- The zero-width-space cursor stability technique
- The paste subsystem (HTML-to-markdown, image save)

These are all Phase 2+ work that builds on the Phase 1 foundation.

---

## Risks

### Widget Fork Compilation

The primary risk. Qt's text editing code uses private headers extensively.
Decoupling may reveal implicit dependencies that aren't obvious from reading
the headers.

**Mitigation:** Start with the fork and get it compiling before writing any
other code. If a private API dependency proves too deep to remove, we can
keep a thin shim that includes the private header (acceptable for a GPL
project using GPL Qt source). The goal is functional independence, not zero
private header usage.

### MD4C Limitations

MD4C lacks an AST — we build our own from SAX callbacks. This is proven
(Penelope did it) but means we own the entire document model.

**Mitigation:** The `DocumentBuilder` is isolated behind `Document`'s public
interface. If we later migrate to a different parser (cmark, md4qt), only
`DocumentBuilder` changes — the rest of Markoff is unaffected.

### Rendering Fidelity

Phase 1 rendering through `QTextDocument` HTML has known limitations (no
callout boxes, limited CSS). This is acceptable because Phase 1 is a
foundation — we add custom `QPainter` rendering for complex blocks in
later phases.
