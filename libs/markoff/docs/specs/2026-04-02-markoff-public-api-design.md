# Markoff Public API Design

> **Status: IMPLEMENTED** — All seven public headers shipped. Retained for
> design rationale and API contract documentation.

## Overview

This spec defines markoff's stable public API — the surface that Corbomite and
third-party apps target. Markoff is a Qt6/C++ widget library for editing and
displaying Obsidian-flavored markdown. It owns all Obsidian-specific markdown
concerns (parsing, rendering, syntax extensions). App-level features (completion
popups, vault navigation, session management) remain in the host application.

**Design approach:** Composed API with shared configuration objects. The widgets
(`Editor`, `ReadingView`) accept focused configuration structs (`Theme`,
`EditorSettings`, `RenderSettings`) and an abstract `ResourceProvider`. The
`Document` class provides rich query methods so apps can build outline panels,
backlink views, and tag lists without re-parsing.

## Public Headers

```
include/markoff/
├── Document.h           # Document model + query API
├── Editor.h             # Editing widget
├── EditorSettings.h     # Behavioral editor config
├── ReadingView.h        # Lightweight read-only view
├── RenderSettings.h     # Layout config
├── ResourceProvider.h   # Abstract resource resolution interface
└── Theme.h              # Per-element styling
```

Seven headers. `Renderer.h` becomes internal — apps use the widgets, not the
renderer directly.

Everything lives in the `Markoff::` namespace.

---

## Theme

Per-element styling harvested from QOwnNotes' scheme model. Each markdown
element gets an independent `QTextCharFormat` controlling font, color, weight,
size, and decorations.

### Element Enum

```cpp
enum class Element {
    // Base
    Text,                   // Default text foreground/background
    CurrentLineBackground,

    // Headings
    H1, H2, H3, H4, H5, H6,

    // Inline formatting
    Bold, Italic, Strikethrough, InlineCode,
    Highlight,              // ==text==
    Comment,                // %%text%%
    Tag,                    // #tag

    // Links
    Link,                   // [text](url)
    WikiLink,               // [[note]]
    BrokenLink,             // Unresolvable link
    Image,                  // ![alt](src)

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
    MaskedSyntax,           // Hidden delimiters in live preview
    TrailingSpace,
};
```

### Theme Struct

```cpp
struct Theme {
    QHash<Element, QTextCharFormat> formats;

    QFont textFont;         // Base proportional font
    QFont codeFont;         // Base monospace font

    static Theme defaultLight();
    static Theme defaultDark();
    static Theme fromSchemeFile(const QString &path);   // QOwnNotes INI compat
};
```

**Font size adaptation:** Following QOwnNotes, heading formats carry scaled font
sizes (H1 ~200%, H2 ~160%, H3 ~130%, etc.) baked into their `QTextCharFormat`.
The `Theme::textFont` point size serves as the base; heading formats derive from
it via the factory methods.

**Code font:** Code-related elements (`InlineCode`, `CodeBlock`, `CodeKeyword`,
etc.) default to `Theme::codeFont`. All other elements default to
`Theme::textFont`.

**Factory methods:**
- `defaultLight()` / `defaultDark()` — hardcoded sensible defaults.
- `fromSchemeFile()` — loads QOwnNotes INI format for theme migration and
  compatibility with the existing ecosystem of user-created QOwnNotes themes.

---

## EditorSettings

Behavioral editor configuration. Maps directly to Corbomite's kcfg `Editor`
group.

```cpp
struct EditorSettings {
    int tabSize = 4;
    bool lineNumbers = false;
    bool lineWrap = true;
    bool highlightCurrentLine = true;
    bool highlightingEnabled = true;
};
```

---

## RenderSettings

Layout configuration shared by both `Editor` (live preview mode) and
`ReadingView`.

```cpp
struct RenderSettings {
    int maxWidthPx = 0;            // 0 = fill container
    int marginPx = 16;
    bool showFrontmatter = false;
    bool renderImages = true;
    bool renderCodeHighlighting = true;
};
```

**What moved out:**
- `baseFontSizePt` → `Theme::textFont` point size
- `basePath` → `ResourceProvider`

---

## ResourceProvider

Abstract interface for resolving vault-relative references. The host app
implements this with vault-aware logic. Markoff ships a simple filesystem
default.

```cpp
class ResourceProvider {
public:
    virtual ~ResourceProvider() = default;

    // Resolve image reference to a loadable URL
    // e.g. "photo.png" from ![[photo.png]]
    // Returns empty QUrl if unresolvable
    virtual QUrl resolveImage(const QString &name) const = 0;

    // Resolve embed reference to its markdown content
    // e.g. "Other Note" from ![[Other Note]]
    // Returns nullopt if unresolvable
    virtual std::optional<QString> resolveEmbed(const QString &name) const = 0;

    // Resolve link target to a URL (for navigation, hover previews)
    // e.g. "My Note" from [[My Note]], "My Note#heading" from [[My Note#heading]]
    // Returns empty QUrl if unresolvable
    virtual QUrl resolveLink(const QString &target) const = 0;

    // Check if a link target exists (for broken link styling)
    // Separate from resolveLink() for performance — the highlighter calls
    // this frequently and only needs a boolean
    virtual bool linkExists(const QString &target) const = 0;
};
```

### FilesystemResourceProvider

```cpp
class FilesystemResourceProvider : public ResourceProvider {
public:
    explicit FilesystemResourceProvider(const QString &basePath);

    QUrl resolveImage(const QString &name) const override;
    std::optional<QString> resolveEmbed(const QString &name) const override;
    QUrl resolveLink(const QString &target) const override;
    bool linkExists(const QString &target) const override;

private:
    QString m_basePath;
};
```

Simple default: resolves names relative to `basePath` on the filesystem.
Handles `name.md` and `name` → `name.md` fallback. No vault-aware
shortest-path matching — that's the app's job.

---

## Document Query API

`Document` retains its existing API (`fromMarkdown`, `sourceText`, `isEmpty`,
`extractSubpath`, `frontmatter`, `markdownContent`, `footnoteCount`,
`footnoteContent`) and gains query methods and info structs.

### Info Structs

```cpp
struct HeadingInfo {
    int level;              // 1-6
    QString text;           // Heading text without # markers
    int sourceOffset;       // Character offset in source for navigation
};

struct LinkInfo {
    enum Type { Standard, Wiki, Image, Embed };
    Type type;
    QString target;         // Raw target: "note", "note#heading", "https://..."
    QString displayText;    // Display text or alt text for images
    int sourceOffset;
};

struct TagInfo {
    QString name;           // Without #, e.g. "project/active"
    int sourceOffset;
};

struct FootnoteInfo {
    int number;             // 1-based
    QString label;          // Original label, e.g. "fn1"
    QString content;        // Footnote body text
};
```

All are value types with `sourceOffset` for editor navigation.

### Query Methods

```cpp
class Document {
public:
    // ... existing API unchanged ...

    QList<HeadingInfo> headings() const;
    QList<LinkInfo> links() const;
    QList<LinkInfo> wikiLinks() const;      // Convenience filter over links()
    QList<TagInfo> tags() const;
    QList<FootnoteInfo> footnotes() const;
    int wordCount() const;
    int characterCount() const;
};
```

---

## Editor Widget

QGraphicsView-based markdown editor. Accepts all configuration objects, emits
signals for app integration, provides slots for toolbar/menu binding.

### Modes

```cpp
enum class Mode { Source, LivePreview };
```

- **Source:** Raw markdown with syntax highlighting.
- **LivePreview:** Blocks near cursor show raw text; blocks far from cursor show
  rendered output. Tables, code blocks, and other non-text elements render as
  interactive block items.

### Configuration

```cpp
void setTheme(const Theme &theme);
Theme theme() const;

void setEditorSettings(const EditorSettings &settings);
EditorSettings editorSettings() const;

void setRenderSettings(const RenderSettings &settings);
RenderSettings renderSettings() const;

void setResourceProvider(ResourceProvider *provider);   // Non-owning
```

### Content

```cpp
void setPlainText(const QString &text);
void clear();                       // Equivalent to setPlainText({})
QString toPlainText() const;

const Document *document() const;   // Live parsed document, updated on text change
```

### Editing Actions (slots for toolbar/menu binding)

```cpp
void undo();
void redo();
void cut();
void copy();
void paste();
void selectAll();
```

### Formatting Actions

```cpp
void toggleBold();
void toggleItalic();
void toggleStrikethrough();
void toggleInlineCode();
void insertLink();              // Wraps selection in [text](url)
void insertWikiLink();          // Wraps selection in [[text]]
void insertImage();             // Inserts ![](url) or ![[name]]
void insertCodeBlock();
void insertBlockQuote();
void insertHorizontalRule();
void insertTable(int rows, int cols);
void increaseHeadingLevel();
void decreaseHeadingLevel();
void toggleCheckbox();          // Cycle: none → [ ] → [x] → none
void insertCallout(const QString &type);
```

### Action Registry

All editing and navigation commands are available as `QAction` instances via
a typed enum. These actions carry icons, shortcuts, translated text, and
read-only enable/disable state — consumers add them directly to toolbars and
menus without rewiring.

```cpp
enum class ActionId {
    Undo, Redo,
    Cut, Copy, Paste, SelectAll,
    Find, FindNext, FindPrevious, Replace,
    ZoomIn, ZoomOut,
    ToggleBold, ToggleItalic, ToggleStrikethrough, ToggleInlineCode,
    InsertLink, InsertWikiLink, InsertImage,
    InsertCodeBlock, InsertBlockQuote, InsertHorizontalRule, InsertTable,
    IncreaseHeading, DecreaseHeading, ToggleCheckbox,
    ToggleFoldAtCursor, FoldAll, UnfoldAll,
};

QAction *action(ActionId id) const;   // Single action by ID
QList<QAction*> actions() const;      // All registered actions
```

**Basic toolbar usage:**

```cpp
auto *toolbar = addToolBar(tr("Format"));
toolbar->addAction(editor->action(Markoff::ActionId::ToggleBold));
toolbar->addAction(editor->action(Markoff::ActionId::InsertTable));
```

**Popup widget on a toolbar button (e.g., table size grid):**

Some actions benefit from a richer interaction than a single click. The
canonical example is Insert Table, where a grid popup lets the user choose
dimensions visually. Markoff exposes the `QAction` and the parameterized
`insertTable(rows, cols)` method; the consumer owns the popup widget.

```cpp
// 1. Retrieve the QAction and the QToolButton the toolbar created for it.
QAction *tableAction = editor->action(Markoff::ActionId::InsertTable);
auto *btn = qobject_cast<QToolButton *>(toolbar->widgetForAction(tableAction));

// 2. Build a QMenu containing a QWidgetAction with your custom grid widget.
auto *menu = new QMenu(btn);
auto *wa = new QWidgetAction(menu);
auto *grid = new TableSizeGrid(menu);   // your custom QWidget
wa->setDefaultWidget(grid);
menu->addAction(wa);

// 3. Attach the menu to the button.
//    MenuButtonPopup: click → default action (3×3), arrow → grid popup.
//    InstantPopup: entire button opens the grid popup.
btn->setMenu(menu);
btn->setPopupMode(QToolButton::MenuButtonPopup);

// 4. Connect the grid's selection signal to Editor::insertTable().
connect(grid, &TableSizeGrid::sizeSelected, editor, &Markoff::Editor::insertTable);
```

The same `QWidgetAction`-in-`QMenu` pattern works for any action that needs
a popup (color pickers, heading level selectors, callout type choosers, etc.).
Markoff owns the action identity and the editing method; the consumer owns
the popup presentation.

### Cursor & Navigation

```cpp
int cursorLine() const;         // 1-based
int cursorColumn() const;       // 1-based
QRect cursorScreenRect() const; // Screen-space rect for popup positioning
void goToLine(int line);
void scrollToHeading(const HeadingInfo &heading);
```

### Search

```cpp
bool findText(const QString &text, QTextDocument::FindFlags flags = {});
bool replaceText(const QString &find, const QString &replace,
                 QTextDocument::FindFlags flags = {});
int replaceAll(const QString &find, const QString &replace,
               QTextDocument::FindFlags flags = {});
```

### Signals

```cpp
// Content
void textChanged();
void modeChanged(Markoff::Editor::Mode mode);

// Cursor
void cursorPositionChanged(int line, int column);

// Editing state
void undoAvailable(bool available);
void redoAvailable(bool available);
void modificationChanged(bool modified);

// Link/navigation
void linkClicked(const QString &target);
void linkHovered(const QString &target);

// Completion triggers
void wikiLinkTrigger(int cursorPosition);
void tagTrigger(int cursorPosition);
void completionDismissHint();

// Document structure changed (fired after re-parse)
void headingsChanged(const QList<Markoff::HeadingInfo> &headings);
void linksChanged(const QList<Markoff::LinkInfo> &links);
void tagsChanged(const QList<Markoff::TagInfo> &tags);
void wordCountChanged(int count);
```

---

## ReadingView Widget

Lightweight read-only view. Shares `Theme`, `RenderSettings`, and
`ResourceProvider` with the Editor. No editing machinery.

Currently backed by QTextBrowser internally; will transition to a read-only
QGraphicsScene (same rendering path as Editor in LivePreview mode, minus
editing). The public API is stable through that transition.

### API

```cpp
class ReadingView : public QWidget {
    Q_OBJECT

public:
    explicit ReadingView(QWidget *parent = nullptr);
    ~ReadingView() override;

    // Content
    void setDocument(const Document &doc);
    void setMarkdown(const QString &markdown);  // Convenience: parse + display
    const Document *document() const;

    // Configuration
    void setTheme(const Theme &theme);
    Theme theme() const;

    void setRenderSettings(const RenderSettings &settings);
    RenderSettings renderSettings() const;

    void setResourceProvider(ResourceProvider *provider);   // Non-owning

    // Scroll
    qreal scrollFraction() const;
    void setScrollFraction(qreal fraction);
    void scrollToHeading(const HeadingInfo &heading);

    // Size hint for embedding contexts
    int naturalHeight(int width) const;

Q_SIGNALS:
    void linkClicked(const QString &target);
    void linkHovered(const QString &target);
};
```

**`setMarkdown()` convenience** — for hover previews, canvas cards, and other
contexts where the caller has raw text and doesn't want to manage a `Document`.

**`naturalHeight()`** — returns the layout height for the document at a given
width. Essential for canvas cards and popups that size to content.

---

## Corbomite Integration Sketch

How Corbomite would wire up markoff with this API:

```cpp
// App startup — create shared objects
auto theme = Markoff::Theme::fromSchemeFile(Settings::schemePath());
// or: Markoff::Theme::defaultDark();

auto *resourceProvider = new VaultResourceProvider(vaultModel);
// VaultResourceProvider implements Markoff::ResourceProvider
// with vault-aware shortest-path resolution

Markoff::RenderSettings renderSettings;
renderSettings.maxWidthPx = Settings::maxWidth();
renderSettings.showFrontmatter = Settings::showFrontmatter();

// Editor tab
auto *editor = new Markoff::Editor(parent);
editor->setTheme(theme);
editor->setEditorSettings({
    .tabSize = Settings::tabSize(),
    .lineNumbers = Settings::lineNumbers(),
    .lineWrap = Settings::lineWrap(),
});
editor->setRenderSettings(renderSettings);
editor->setResourceProvider(resourceProvider);
editor->setMode(Markoff::Editor::Mode::LivePreview);
editor->setPlainText(noteDocument->markdown());

// Wire signals
connect(editor, &Markoff::Editor::textChanged, this, [=]() {
    noteDocument->setMarkdown(editor->toPlainText());
});
connect(editor, &Markoff::Editor::cursorPositionChanged,
        statusBar, &StatusBar::updateCursorInfo);
connect(editor, &Markoff::Editor::linkClicked,
        this, &MainWindow::navigateToNote);
connect(editor, &Markoff::Editor::wikiLinkTrigger,
        this, &MainWindow::showWikiLinkCompletion);
connect(editor, &Markoff::Editor::headingsChanged,
        outlinePanel, &OutlinePanel::updateHeadings);

// Canvas card (embedded preview)
auto *preview = new Markoff::ReadingView(cardWidget);
preview->setTheme(theme);
preview->setRenderSettings(renderSettings);
preview->setResourceProvider(resourceProvider);
preview->setMarkdown(cardMarkdown);
int height = preview->naturalHeight(cardWidth);
```

---

## What This API Does NOT Cover

- **Completion popups** — app concern. Markoff signals triggers; the app shows UI.
- **Vault navigation** — app concern. Markoff signals link clicks; the app navigates.
- **Session management** — app concern. The editor edits whatever text it's given.
- **File I/O** — app concern. Markoff never touches the filesystem except through
  `ResourceProvider` for rendering resources.
- **Print/PDF export** — deferred to a future API extension.
- **Collaborative editing** — out of scope for v1.
- **Extension/plugin system** — out of scope for v1. Can be added later if
  third-party syntax extensions are needed.

---

## Binary Compatibility

- All widget classes (`Editor`, `ReadingView`) use pimpl (`struct Private`).
- Configuration structs (`Theme`, `EditorSettings`, `RenderSettings`) are value
  types — no pimpl needed.
- Info structs (`HeadingInfo`, `LinkInfo`, `TagInfo`, `FootnoteInfo`) are value
  types.
- `ResourceProvider` is a pure virtual interface — adding new virtual methods is
  an ABI break. New resolution methods should be added via a
  `ResourceProvider2` interface or optional overloads if ever needed.
