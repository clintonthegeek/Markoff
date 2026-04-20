# Consumer-facing editor-state surface (integration requirements)

**Audience:** Markoff library developers (including those on the current
rewrite branch).
**Author:** Corbomite app team, 2026-04-20.
**Status:** requirements document — no Markoff code changes yet. Intended
to inform the rewrite so the consumer-facing API is right from the
start.

---

## 1. Why this matters

Corbomite (and any other host app) wants the surrounding menubar,
toolbar, and palette to behave *like a word processor*:

- **Bold / Italic / Strikethrough / Inline-code / Blockquote** toolbar
  buttons should be **checked** when the cursor is inside the
  corresponding inline span, and **unchecked** otherwise. Clicking them
  applies or strips the formatting.
- **Heading 1…6** radio submenu should have the correct level checked
  based on the current line. Empty selection on a plain paragraph → no
  item checked.
- **Table submenu** (Insert/Delete Row/Column) should be **enabled only
  when the cursor sits inside a GFM pipe table**. Otherwise the whole
  submenu grays out.
- **Toggle Checkbox** should be checked when the current line has a
  `- [x]` or `- [ ]` task marker.
- **Fold All / Unfold All / Toggle Fold** — `Toggle Fold` is only
  meaningful on a heading line; greyed out otherwise.
- **Insert Link** vs **Insert Wiki Link** — neither toggle state
  applies; always enabled while the active view is a `MarkdownView`.
- **Format > Increase/Decrease Heading** — enabled only when the cursor
  is on a heading (Increase capped at H6, Decrease at H1) or on a
  plain paragraph (Increase only).

Today, Corbomite already has:

- `cursorInTable()` → drives Table submenu enable-state. ✓
- `currentHeadingLevel()` → drives Heading radio check-state. ✓
- `isFolded(path)` → per-heading fold state. ✓
- `toggleFoldAtCursor()` → caller doesn't need to *know* if the cursor
  is on a heading, because the API no-ops otherwise — but UX-wise we
  still want to grey out the menu entry when it'd no-op.

Everything else (Bold/Italic/Strikethrough/InlineCode/BlockQuote/
ListItem/Callout/CodeBlock/Link/WikiLink state) is **not currently
queryable** from Markoff. Corbomite's `MainWindow::refreshEditorActions`
has to leave those toolbar buttons in a static "enabled but never
checked" state, which looks unfinished next to the heading radio and
the table gate.

This document describes the full contextual surface the consumer
needs, in a form the Markoff rewrite can implement cleanly.

---

## 2. What we need to know, in plain language

For any cursor position (with or without a selection) in an active
`Markoff::Editor`, Corbomite wants answers to these questions:

### 2.1 Inline-span context (affects Format menu / toolbar checkstate)

- Is the cursor (or the entire selection) inside **bold** (`**…**` or
  `__…__`)?
- …inside **italic** (`*…*` or `_…_`)?
- …inside **strikethrough** (`~~…~~`)?
- …inside **inline code** (`` `…` ``)?
- …inside a **highlight** (`==…==`)?
- …inside a **link** (`[text](url)`) — if so, what's the URL?
- …inside a **wiki-link** (`[[target|alias]]`) — if so, what's the
  target?
- …inside a **tag** (`#tag/subtag`)?
- …inside an **embed** (`![[…]]` or `![alt](url)`)?
- …inside a **footnote reference** (`[^1]`)?
- …inside a **math span** (`$…$` or `$$…$$`)?

For toggle-style commands (Bold / Italic / etc.) the key predicate is
"**the selection** is entirely inside a matching span". For informative
commands (Insert Link, Copy Link URL) the predicate is "the cursor
sits inside a link span".

### 2.2 Block-level context (affects Heading radio, Toggle Checkbox,
### Insert Callout enable-state, etc.)

- What kind of block contains the cursor? (paragraph, heading,
  blockquote, callout, code block, GFM table, list item, HR,
  frontmatter, math display block)
- If **heading**: what level (1–6)?
- If **list item**: what list type (bullet / ordered / task)? What task
  state (unchecked / checked / cancelled)? What nesting level?
- If **callout**: what callout type (`note` / `tip` / …) and what
  title?
- If **blockquote**: how deeply nested?
- If **code block**: what language? Is it fenced or indented?
- If **GFM table**: row index, column index, total rows, total
  columns, alignment of the current column.
- If **frontmatter**: what key is the cursor on (if any)?

Many of these map 1:1 to menu enable-state gates:

| Menu/Toolbar item             | Gate/check predicate                   |
|-------------------------------|----------------------------------------|
| Format > Bold                 | `spanIsInsideBold`                     |
| Format > Italic               | `spanIsInsideItalic`                   |
| Format > Strikethrough        | `spanIsInsideStrikethrough`            |
| Format > Inline Code          | `spanIsInsideInlineCode`               |
| Insert > Block Quote          | checked when `blockKind == BlockQuote` |
| Insert > Toggle Checkbox      | checked when current line has task glyph |
| Insert > Callout…             | disabled inside an existing callout    |
| Heading > N                   | checked when `headingLevel == N`       |
| Heading > Increase            | disabled when `headingLevel == 6`      |
| Heading > Decrease            | disabled when `headingLevel <= 1` (unless showing "strip") |
| Table > Insert Row/Col/Delete | enabled when `blockKind == Table`      |
| Table > Insert Row/Col        | no change                              |
| Table > Delete Row            | additionally disabled when `tableRows == 1` |
| Table > Delete Column         | additionally disabled when `tableCols == 1` |
| View > Toggle Fold at Cursor  | enabled when current block is a heading |

### 2.3 Document-level context (affects app-shell state)

- Current `ViewMode` — Corbomite already has this on
  `NoteEditorWidget`, not Markoff's problem.
- Read-only? — already on `Editor::isReadOnly()`.
- Word / character count — already fired via `wordCountChanged`.

---

## 3. Proposed API shape

We suggest **one struct snapshot + one signal**, modelled on Qt's
`QTextCursor::charFormat()` but typed for Markdown. This keeps the
consumer's wiring to a single connection.

```cpp
namespace Markoff {

/// A snapshot of the editor's per-cursor contextual state, suitable
/// for driving menu/toolbar enable + check state in the host app.
/// Fields are value-typed; copying is cheap.
struct EditorContext {
    // ---- Block context ----
    enum class BlockKind {
        Paragraph, Heading, BlockQuote, Callout, CodeBlock,
        Table, ListItem, HorizontalRule, FrontMatter, MathDisplay,
        Empty,
    };
    BlockKind blockKind = BlockKind::Paragraph;

    int  headingLevel = 0;          // 1..6 when blockKind == Heading, else 0

    // Populated when blockKind == Callout:
    QString calloutType;            // e.g. "note", "tip"
    QString calloutTitle;

    // Populated when blockKind == CodeBlock:
    QString codeBlockLanguage;
    bool    codeBlockFenced = false;

    // Populated when blockKind == ListItem:
    enum class ListMarker { Unordered, Ordered, Task };
    ListMarker listMarker = ListMarker::Unordered;
    enum class TaskState { NotATask, Unchecked, Checked, Cancelled };
    TaskState  taskState = TaskState::NotATask;
    int        listNestingLevel = 0;

    // Populated when blockKind == BlockQuote:
    int blockquoteDepth = 0;

    // Populated when blockKind == Table:
    struct TableContext {
        int row = 0, col = 0;
        int rows = 0, cols = 0;
        Qt::Alignment columnAlignment;
        bool isHeaderRow = false;
    };
    std::optional<TableContext> table;

    // ---- Inline-span context ----
    // `true` when the selection (or cursor if empty) lies entirely
    // within a span of the named kind. For toggle actions, host UI
    // sets QAction::setChecked(value).
    bool inBold          = false;
    bool inItalic        = false;
    bool inStrikethrough = false;
    bool inInlineCode    = false;
    bool inHighlight     = false;
    bool inMathInline    = false;

    // `true` when the cursor sits inside the corresponding span.
    // For these, host UI typically doesn't use setChecked — it opens
    // a sub-menu ("Copy link URL", "Edit alias", etc.).
    struct LinkContext {
        QString url;     // "" when external; fill target for wiki
        QString text;
        bool isWikiLink = false;
        bool isEmbed    = false;
    };
    std::optional<LinkContext> link;

    struct TagContext { QString tag; };
    std::optional<TagContext> tag;

    struct FootnoteContext { QString id; };
    std::optional<FootnoteContext> footnote;

    // ---- Convenience flags ----
    bool hasSelection = false;
    bool atBlockStart = false;
    bool atBlockEnd   = false;
    bool readOnly     = false;
};

class Editor : public QGraphicsView {
    // ...
public:
    /// O(1) snapshot of the current cursor's contextual state. Safe to
    /// call from a slot connected to `contextChanged`, or on demand.
    EditorContext context() const;

Q_SIGNALS:
    /// Emitted whenever the cursor position, selection, document
    /// structure, or inline/block classification of the cursor's
    /// surroundings changes. Debounced to at most one emission per
    /// ~16 ms (one frame) so a host connecting refreshEditorActions()
    /// does not thrash on rapid arrow-key navigation.
    void contextChanged(const EditorContext &ctx);
};

} // namespace Markoff
```

### 3.1 Why a single snapshot, not many fine-grained signals

Corbomite's `refreshEditorActions()` already iterates ~40 `QAction`s
every time it runs; each check is a hash lookup plus a setter. The
cost of walking the whole set is negligible compared with the cost of
repositioning a `QGraphicsScene` focus item. So we want **one** signal
that says "something about the cursor's surroundings changed; here's
the new truth", and a **pull** accessor for on-demand re-querying
(e.g. on active-leaf-changed).

Compare the alternative — separate `boldStateChanged(bool)`,
`italicStateChanged(bool)`, `headingLevelChanged(int)`, etc. signals —
which forces consumers to write N nearly-identical slots and wire N
connections. It also makes adding a new inline kind a breaking change
for every consumer.

### 3.2 Debouncing and emission triggers

`contextChanged` should fire when, and only when, any of:

- The cursor moves to a different inline run or different block.
- The selection expands or contracts across a span boundary.
- The document is reparsed and the cursor's surroundings are
  reclassified (e.g. the user just typed `**` closing a bold span).
- The read-only state changes.

It should *not* fire on every keystroke inside the same run — emitting
at ~60 Hz is wasteful when the host is going to do O(N) action updates.

A one-frame (~16 ms) debounce is plenty. An empty snapshot is fine
when the editor has no focused text item.

### 3.3 Pull accessor

`EditorContext Editor::context() const` — called by consumers who need
to refresh without waiting for the signal, e.g. immediately after
`Workspace::activeLeafChanged` when the new `Markoff::Editor` hasn't
emitted anything yet.

Implementation is the same computation as the signal; the signal is
just `context(); emit contextChanged(that)`.

---

## 4. Mapping existing accessors

These already exist and would remain as thin wrappers over the new
context struct (or stay as first-class methods — either is fine):

| Existing accessor       | Equivalent field on `EditorContext`        |
|-------------------------|--------------------------------------------|
| `cursorInTable()`       | `ctx.blockKind == BlockKind::Table`        |
| `currentHeadingLevel()` | `ctx.headingLevel`                         |
| `isReadOnly()`          | `ctx.readOnly`                             |
| `cursorLine()` / `cursorColumn()` | left on the pull API as-is       |
| `isFolded(path)`        | not covered by context — remains a doc-state query |

Breaking the existing accessors is **not** desirable; we're still
using them from Corbomite's Phase 2+3 wiring (`MainWindow.cpp`
`refreshEditorActions`). New API should be additive.

---

## 5. How Corbomite will consume this

Sketch — Corbomite Phase 2+3 follow-up, once Markoff ships the API:

```cpp
// src/app/MainWindow.cpp (sketch)
void MainWindow::connectEditorContext(NoteEditorWidget *editor)
{
    auto *ed = editor->editor();   // Markoff::Editor*
    connect(ed, &Markoff::Editor::contextChanged,
            this, &MainWindow::onEditorContextChanged);
    onEditorContextChanged(ed->context());  // prime initial state
}

void MainWindow::onEditorContextChanged(const Markoff::EditorContext &ctx)
{
    KActionCollection *ac = actionCollection();

    auto setCheck = [ac](const QString &id, bool on) {
        if (auto *a = ac->action(id)) a->setChecked(on);
    };
    auto setEnable = [ac](const QString &id, bool on) {
        if (auto *a = ac->action(id)) a->setEnabled(on);
    };

    using BK = Markoff::EditorContext::BlockKind;

    // Format checkstate
    setCheck(QStringLiteral("format_bold"),          ctx.inBold);
    setCheck(QStringLiteral("format_italic"),        ctx.inItalic);
    setCheck(QStringLiteral("format_strikethrough"), ctx.inStrikethrough);
    setCheck(QStringLiteral("format_inline_code"),   ctx.inInlineCode);

    // Heading radio
    for (int i = 1; i <= 6; ++i)
        setCheck(QStringLiteral("heading_%1").arg(i), ctx.headingLevel == i);
    setEnable(QStringLiteral("heading_increase"), ctx.headingLevel < 6);
    setEnable(QStringLiteral("heading_decrease"), ctx.headingLevel >= 1);

    // Table submenu
    const bool inTable = ctx.blockKind == BK::Table;
    for (const QString &id : tableActionIds) setEnable(id, inTable);
    if (ctx.table) {
        setEnable(QStringLiteral("table_delete_row"), ctx.table->rows > 1);
        setEnable(QStringLiteral("table_delete_col"), ctx.table->cols > 1);
    }

    // Callout — disable Insert > Callout when already inside one
    setEnable(QStringLiteral("insert_callout"), ctx.blockKind != BK::Callout);

    // Fold at cursor only on headings
    setEnable(QStringLiteral("toggle_fold"), ctx.blockKind == BK::Heading);

    // List / task
    setCheck(QStringLiteral("toggle_checkbox"),
             ctx.taskState == Markoff::EditorContext::TaskState::Checked);
}
```

One connection per live `Markoff::Editor`. Swap on
`Workspace::activeLeafChanged`. No per-keystroke reparse hooks.

---

## 6. Implementation hints (non-binding)

You know the internals better than we do, but a few notes from the
consumer side:

- **Block classification** is already computed by the parser (block
  trees hang off `Document`). The new context field is just the leaf
  block type at the cursor's offset plus a few typed-field projections.
- **Inline classification** is trickier because spans are overlays on
  block text. The existing highlighter already knows the run the cursor
  is inside. Lifting that classification into a typed enum (rather than
  a `QTextCharFormat` property) is the main work.
- Start with a **stub** that only reports the fields Corbomite
  currently uses (`blockKind == Table`, `headingLevel`, `inBold`) and
  leaves the rest at default-zero. Corbomite's consumer loop tolerates
  unknown/zero fields gracefully. Shipping the API surface early lets
  us validate the shape before you commit to the full classification.
- **Don't** expose `QTextCursor` or raw document offsets in the public
  type. Consumers who need those still have `cursorLine()` /
  `cursorColumn()` on the pull API.
- **Test hooks**: Corbomite's `tst_mainwindow_action_wiring` already
  asserts initial-state enable-gates. When Markoff adds the context
  API, we'll extend that test to construct a `MarkdownView` with
  fixture markdown and verify the radio/check state flips as the
  cursor moves across pre-seeded spans. A pull-API
  `Editor::context()` makes that test trivial.

---

## 7. Out of scope (things we are **not** asking for)

- A plugin-facing equivalent of Obsidian's `editor-menu` hook — that's
  a separate Corbomite concern (see outer repo `docs/backlog.md` §3
  "Markoff editor right-click menu enrichment"). Markoff just needs to
  offer the context surface; Corbomite owns injecting menu items into
  its right-click menu.
- Serialization of the context into workspace state — this is
  transient runtime info, not something to persist.
- Fine-grained per-format transaction hooks for plugin content
  generation. The `editorChange`/`editorCallback` layer in Obsidian is
  a separate topic; contextChanged is read-only observation.
- Anything about the reading view — `Corbomite::ReadingView` is a
  different widget, owned by a different library.

---

## 8. Acceptance — how we'll know this is done

When all of these hold:

1. `Markoff::Editor::context()` returns an `EditorContext` with at
   least `blockKind`, `headingLevel`, `table`, `inBold`, `inItalic`,
   `inStrikethrough`, `inInlineCode`, `readOnly` populated.
2. `Markoff::Editor::contextChanged` fires at most once per frame for
   sustained cursor motion inside the same run, and exactly once for
   each distinct classification change.
3. A Corbomite test (new, host-side) can seed a document with
   pre-formatted markdown, move the cursor to a known position via the
   existing test-only synthesize helpers, and observe the expected
   context snapshot.
4. Existing accessors (`cursorInTable`, `currentHeadingLevel`,
   `isReadOnly`) continue to return values consistent with the
   corresponding context fields.

Once those land we'll retire Corbomite's current "static enable, never
checked" toolbar state for Format actions and ship the word-processor
UX.

---

## 9. Related existing docs

- `libs/markoff-family/libs/markoff/docs/specs/2026-04-02-markoff-public-api-design.md`
  — the broader public-API shaping doc this spec extends.
- `libs/markoff-family/libs/markoff/docs/specs/2026-04-16-qaction-shortcuts-design.md`
  — the `QAction` + `ActionId` surface Corbomite already consumes
  (extended in the Cluster V rollout 2026-04-20 with `SetHeading1..6`
  and `cursorInTable`).
- Outer repo `docs/obsidian-audit/domains/editor.md` §2, §5 —
  Obsidian's equivalent `editor` methods: `getSelection`,
  `getCursor`, `somethingSelected`, `getLine`, `getTokenAt`. The
  `getTokenAt` return shape (`{start, end, string, type, state}`) is a
  close cousin of the inline-classification problem; worth skimming.
