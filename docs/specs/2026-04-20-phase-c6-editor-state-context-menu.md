# Phase C6 — Consumer editor-state surface + context-menu contribution point

**Status:** drafted 2026-04-20
**Markoff tag on completion:** `v0.5.0`
**Absorbs:** Corbomite's editor-state prescription (recovered at Markoff
`v0.2.8`; stranded-on-submodule-master for three weeks before Phase C
ownership transferred).
**Ordering:** third in Phase C, after C1 (DI seam, done, `v0.3.0`) and C5
(reading-mode interaction parity, done, `v0.4.0`).

## 1. Scope (narrowed via brainstorming)

This is a **wrapper spec** over the full 640-line prescription at
[`libs/markoff-live/docs/specs/2026-04-20-consumer-editor-state-surface.md`](../../libs/markoff-live/docs/specs/2026-04-20-consumer-editor-state-surface.md)
("consumer-spec"). That document — authored by the Corbomite app team
before Phase C ownership transferred — is the authoritative design
record for the full API shape. This wrapper narrows scope to what ships
in C6 and captures Phase-C-specific orchestration (tag, sequencing,
breaking-change manifest, acceptance criteria, Corbomite consumer beat).

### 1.1 What ships in C6 (Option C: stub + defer unused fields)

Following the pattern established in C5 (defer speculative coverage
until a consumer materializes), C6 ships the full API *surface* (struct
shape stable) with an *initial* field population limited to what
Corbomite's `MainWindow::refreshEditorActions` needs today.

**New public types (`libs/markoff-live/include/markoff/EditorContext.h`):**

- `struct Markoff::EditorContext` — full struct shape per consumer-spec §3
  (lines 136-213). All fields declared; scope below governs which are
  populated vs. default-zero in C6.
- Nested types: `EditorContext::BlockKind` enum, `EditorContext::ListMarker`
  enum, `EditorContext::TaskState` enum, `EditorContext::TableContext`
  struct, `EditorContext::LinkContext` struct, `EditorContext::TagContext`
  struct, `EditorContext::FootnoteContext` struct.

**New `Markoff::Editor` members (`libs/markoff-live/include/markoff/Editor.h`):**

- `EditorContext Editor::context() const` — O(1) pull accessor.
- `Q_SIGNAL void contextChanged(const EditorContext &ctx)` — debounced
  ~16 ms; emits on run / block / read-only transitions.
- `Q_SIGNAL void aboutToShowContextMenu(QMenu *menu, const EditorContext &ctx, const QPoint &globalPos)` —
  fires mid-`contextMenuEvent` after Markoff's built-ins and before
  `menu.exec()`. Passed menu is stack-local; subscribers don't retain
  pointers.

### 1.2 Populated fields (C6 initial classifier scope)

In `context()`'s initial implementation, these fields carry meaningful
values. Everything else declared in the struct stays default-zero /
`std::nullopt`.

| Field                                            | Populated | Source                                                     |
| ------------------------------------------------ | --------- | ---------------------------------------------------------- |
| `blockKind`                                      | ✅        | `Heading` (via heading-prefix scan), `Table` (via `QTextCursor::currentTable()`), `ListItem` (list-item detection — existing highlighter logic), `Empty` (zero-length block), else `Paragraph` |
| `headingLevel`                                   | ✅        | Current `currentHeadingLevel()` body                       |
| `table` (optional TableContext)                  | ✅        | `{row, col, rows, cols, isHeaderRow}` from `QTextTable` when inside one; `columnAlignment` left default for C6 |
| `inBold / inItalic / inStrikethrough / inInlineCode` | ✅        | Inline-span classifier on current cursor position (see §2) |
| `hasSelection`                                   | ✅        | Selection-range check                                      |
| `atBlockStart / atBlockEnd`                      | ✅        | Cursor position vs. block bounds                           |
| `readOnly`                                       | ✅        | Existing `isReadOnly()`                                    |
| `inHighlight` / `inMathInline`                   | ❌        | Default `false`. Deferred — no current consumer.           |
| `link` / `tag` / `footnote` / `calloutType` / `calloutTitle` / `codeBlockLanguage` / `codeBlockFenced` / `listMarker` / `taskState` / `listNestingLevel` / `blockquoteDepth` | ❌ | Default / `std::nullopt`. Deferred. |

### 1.3 Deferred field-population (future C-phase when a consumer materializes)

The unpopulated fields above are declared in the struct (so consumers
can write forward-compatible code) but return default-zero values in
C6. Future work fills them in:

- **Inline link/wiki-link context** — unblocks "Copy link URL", "Edit
  alias" right-click entries. Natural fit for C4 (renderer unification)
  or a dedicated C8 follow-on.
- **Callout / blockquote / code-block sub-context** — unblocks
  "Insert Callout disable-when-already-in-callout", "Rename code-block
  language", etc. Same destination.
- **Task-state detail, list-marker detail** — unblocks richer list UX.
- **Math/highlight inline spans** — unblocks math-context right-click.

Rationale matches C5's deferral pattern: struct shape stable, consumer
code forward-compatible, classifier work lands incrementally as
consumers need it. No speculative classifier infrastructure.

### 1.4 Context-menu contribution point (§9 of consumer-spec)

C6 ships the `aboutToShowContextMenu(QMenu *, const EditorContext &, const QPoint &)`
signal per consumer-spec §9.2 — **after-built-ins** shape (§9.2
paragraph 1), not the alternative section-tags-before-built-ins shape.
Rationale: after-built-ins keeps Markoff out of the section-ordering
business; Corbomite already has `MenuSectionHelper` (Cluster R) and
handles ordering host-side.

Markoff's existing `Editor::contextMenuEvent` (at
`libs/markoff-live/src/Editor.cpp:705`) has two branches: a
table-context menu (line 712-736) and a general-context menu (line 738+
with undo/redo/cut/copy/paste/select-all). C6 emits the signal in
**both branches** so subscribers can contribute regardless of cursor
context. The table branch's special-case ordering is preserved
(consumers see a menu that already has Insert Row/Col/Delete/Select
items in it — they can either replace those by disconnecting from
Corbomite's slot conditionally, or just accept the existing table
items as the built-ins in the table case).

## 2. Internal classifier hints (implementation-side)

Only binding guidance here — the plan captures the concrete steps.

**Block classification** — cheap. Extract the heading/table detection
from existing `currentHeadingLevel()` / `cursorInTable()` into a
`classifyBlockAtCursor()` helper returning `BlockKind` + populated
sub-context. ListItem detection mirrors what `MarkdownHighlighter`
already does for list bullets. `Empty` is a zero-length block.

**Inline classification** — this is the bulk of the internal work.
Markoff's `MarkdownHighlighter` already tags runs with
`QTextCharFormat` properties for bold/italic/strike/inline-code during
`highlightBlock`. The classifier reads those properties off the char
format at the cursor position:

```cpp
// Sketch — plan will concretize.
auto classifyInline(const QTextCursor &cursor) {
    const QTextCharFormat fmt = cursor.charFormat();
    bool inBold = fmt.fontWeight() >= QFont::Bold;
    bool inItalic = fmt.fontItalic();
    bool inStrikethrough = fmt.fontStrikeOut();
    bool inInlineCode = fmt.hasProperty(InlineCodeProperty);  // or equivalent
    // ...
}
```

The right property keys / font-format flags live in
`MarkdownHighlighter.cpp` — plan will read and use the actual ones.
If any of the four toggles don't have a preserved format flag (e.g.
strikethrough may use a text-decoration property rather than
`fontStrikeOut`), the plan documents the actual storage mechanism.

**Selection-wide classification** — for toggle actions, the predicate
is "entire selection is inside a matching span" (consumer-spec §2.1
last paragraph). C6 implements this as: if selection is non-empty,
the inline-bool fields are `true` iff **both** `charFormat()` at
selection start and end carry the matching flag. This is an
approximation (could miss interior gaps) but matches Qt's own behavior
for `QTextEdit::fontBold()` etc. Good enough for toggle UX.

**Debouncing** — `QTimer` single-shot at 16ms, reset on each cursor
motion / re-parse / selection change. First emission after a run of
changes settles. Matches consumer-spec §3.2.

## 3. Breaking-change manifest for CorbomiteApp

None expected on the Markoff side — C6 is **purely additive**. Existing
`cursorInTable()`, `currentHeadingLevel()`, `isReadOnly()` stay as-is.

**Corbomite-side rewrite (the adapter beat):**

- `MainWindow.cpp` gains `connectEditorContext(NoteEditorWidget *)` +
  `onEditorContextChanged(const EditorContext &)` + `connectEditorContextMenu(NoteEditorWidget *)` +
  `onAboutToShowContextMenu(QMenu *, const EditorContext &, const QPoint &)` per
  consumer-spec §5 and §9.5 sketches.
- `MainWindow::refreshEditorActions` either shrinks (replaced by the
  signal-driven updates) or stays alongside (called on
  `activeLeafChanged` to prime initial state via the pull accessor).
- Existing static "enabled but never checked" state on Format toolbar
  toggles retires — buttons now show check state per cursor position.
- Right-click menu in the editor grows Format/Heading/Insert/Table
  entries beyond the current Undo/Redo/Cut/Copy/Paste/Select-All
  baseline.

## 4. Acceptance criteria

From consumer-spec §8 (narrowed to populated fields) + §9.7:

1. `Markoff::Editor::context()` returns an `EditorContext` with
   `blockKind`, `headingLevel`, `table`, `inBold`, `inItalic`,
   `inStrikethrough`, `inInlineCode`, `hasSelection`, `atBlockStart`,
   `atBlockEnd`, `readOnly` populated.
2. `Markoff::Editor::contextChanged` fires at most once per frame for
   sustained cursor motion inside the same run, and exactly once for
   each distinct classification change.
3. A Markoff-side test seeds a document with pre-formatted markdown,
   moves the cursor to known positions, and observes the expected
   context snapshot. (`tst_editor_context` or similar.)
4. Existing `cursorInTable` / `currentHeadingLevel` / `isReadOnly`
   return values consistent with the new context fields.
5. `Markoff::Editor::aboutToShowContextMenu` fires once per right-click,
   after Markoff's built-in items and before `menu.exec()`. Applies to
   both the table-branch and general-branch of `contextMenuEvent`.
6. The passed `EditorContext` matches `context()` at the right-click
   cursor position.
7. Subscribers can append items / separators / submenus; Markoff
   preserves their order as added.
8. `markoff-testapp` (standalone, no subscribers) still has a functional
   context menu — built-ins unchanged.

**Corbomite-side acceptance** (checked at the adapter commit):

9. Format toolbar (Bold/Italic/Strikethrough/InlineCode) shows correct
   check state as cursor moves across pre-formatted markdown in Live
   mode.
10. Heading radio (H1-H6) reflects `headingLevel` correctly.
11. Table submenu enable-state gates on `blockKind == Table`; per-row /
    per-col items additionally gate on `rows > 1` / `cols > 1`.
12. Right-click menu in Live-mode editor exposes Format/Heading/
    Insert/Table entries via `MenuSectionHelper`; built-ins still
    present.
13. End-to-end Corbomite smoke: Ctrl+B / Ctrl+I / right-click →
    Heading 2 / right-click → Insert Callout all produce expected
    markdown mutations.

## 5. Sequencing

1. Draft C6 plan at `libs/markoff-family/docs/plans/2026-04-20-phase-c6-editor-state-context-menu.md`.
2. Implement on Markoff `master` — staged commits:
   - **Commit A:** `EditorContext` header + nested types + default-
     constructed accessors on `Editor` (stub body returns empty
     context). Tag not applied yet.
   - **Commit B:** Block classifier (`classifyBlockAtCursor`) +
     unit-level classifier test.
   - **Commit C:** Inline classifier + unit-level test (probes
     MarkdownHighlighter-tagged formats at cursor).
   - **Commit D:** `contextChanged` signal + debounce + emission
     wiring on cursor/selection/parse/readonly changes.
   - **Commit E:** `aboutToShowContextMenu` emission in both branches
     of `contextMenuEvent` + test.
   - **Commit F:** End-to-end context test (`tst_editor_context`)
     covering populated fields with fixture markdown.
   - Tag `v0.5.0` at end of F.
3. Bump Corbomite submodule pin to `v0.5.0`.
4. Ship Corbomite adapter commit(s):
   - `connectEditorContext` + `onEditorContextChanged` replacing /
     augmenting `refreshEditorActions`.
   - `connectEditorContextMenu` + `onAboutToShowContextMenu`.
   - `tst_mainwindow_action_wiring` extended to cover check-state
     updates (not just enable-state) via a fixture-driven test.
5. Return to Markoff only if cleanup is non-trivial — none expected.

## 6. Decisions recorded

- **D1** Option C scope (stub + defer unused fields) adopted over full
  classification. Rationale: matches C5's "defer speculative coverage"
  pattern; ships Corbomite's word-processor UX today without
  over-building classifier infrastructure for features no consumer
  currently needs.
- **D2** Full struct shape is stable at C6. Rationale: consumer code
  can write `if (ctx.link) …` forward-compatibly; future field
  populations don't churn consumer call sites.
- **D3** Context-menu signal emits **after** built-ins (consumer-spec
  §9.2 paragraph 1), not the section-tags-before-built-ins alternative.
  Rationale: keeps Markoff out of the section-ordering business;
  `MenuSectionHelper` owns it host-side.
- **D4** Signal emits in **both branches** of `contextMenuEvent` (table
  + general), not just one. Rationale: consumers want to contribute
  regardless of cursor-in-table state. Table branch's built-in Insert
  Row/Col/Delete/Select items stay as the table-case baseline.
- **D5** No breaking changes to existing accessors. Rationale:
  Corbomite's Phase 2+3 wiring still uses them; breaking would force a
  same-commit Corbomite rewrite. Additive API is safer.
- **D6** Inline classifier reads `QTextCharFormat` properties written
  by `MarkdownHighlighter`, not a new parser pass. Rationale: reuses
  existing classification work; avoids double-parse.
- **D7** Selection-wide span predicate checks format at selection
  start and end (not a full scan). Rationale: matches Qt's own
  `QTextEdit::fontBold()` behavior; good enough for toggle UX;
  full-scan can be added if a consumer needs exact semantics.
- **D8** C6 closes when the Corbomite adapter commit ships (same
  "corbomite shipped" → "done" pattern as C5), not at Markoff `v0.5.0`
  alone.

## 7. Out of scope

- Per consumer-spec §7: serialization of context into workspace state;
  plugin `editorChange`/`editorCallback` layer (that's Obsidian's
  transaction hook, separate concern); ReadingView context (different
  widget, different library).
- Per §1.3 above: ~15 unpopulated fields. Struct shape stable; fields
  return defaults.
- Per §1.4: section-ordering inside the context menu — host concern.
