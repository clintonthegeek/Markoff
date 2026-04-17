# Markoff TODO

Polish items and known issues. Newer items at the top of each section.
Items marked **(blocked: spec)** need a design decision before
implementation; the rest are implementation tasks.

## Big-ticket features (need spec or brainstorm first)

- [ ] **Math cursor reveal polish** — reveal is now click-only
  (arrow-key reveal removed) and much simpler than previous drafts. The
  remaining guards (`m_inSubstitution`, `m_inCursorUpdate`,
  `m_snappingCursor`) may still be collapsible. Low priority — no
  reported pain.
- [ ] **Cross-item undo coordination.** Each `MarkdownTextItem` has its
  own `QTextDocument` with its own undo stack. Multi-item operations
  (selectAll + toggleBold, replaceAll, cut across boundary) cannot be
  atomically undone. `tst_undo_grouping` only covers single-item cases.
  Decide: (a) centralized undo coordinator that groups cross-item
  edits, or (b) document as a known limitation. Coordinator is the
  better long-term fix — it's a one-time plumbing cost that pays off
  for every future cross-item operation. **(blocked: spec)**
- [ ] **Parse-path unification.** `SceneCoordinator::reparse()`,
  `detectTableRegions()`, `ensureHeadingMap()`, and
  `Document::fromMarkdown()` each run independent full-document
  tree-sitter parses per reparse cycle. Collapsing to one shared AST
  also retires the remaining MD4C path. Prerequisite to incremental
  parsing and to any new Obsidian grammar extension work.
- [ ] **Obsidian-flavored grammar additions** in the vendored
  tree-sitter grammar:
  - `![[embed]]` (embed prefix on wikilinks)
  - `^block-id` block reference
  Requires forking the vendored grammar. Note: `==highlight==` and
  `%%comment%%` are already handled by the vendored grammar
  (`highlight` and `obsidian_comment` nodes).

## Performance

- [ ] Incremental tree-sitter parsing — use `ts_tree_edit()` to update
  the old tree instead of full reparse on every keystroke. Land after
  parse-path unification.
- [ ] Incremental rehighlight — only rehighlight blocks whose spans
  actually changed, not the entire document. Compare old and new span
  maps to find dirty blocks. Currently `applyInlineSubstitutions` calls
  `hl->rehighlight()` per text item per reparse — expensive.
- [ ] **`ensureHeadingMap()` does a full tree-sitter reparse.** Every
  call to `enclosingHeadingPath`, `headingAtBlock`, `headingIndexForItem`
  triggers `ensureHeadingMap()` which — when dirty — runs a **fresh**
  `TreeSitterParser` over the entire document. The heading map is
  marked dirty on every `foldStateChanged`, so folding a single heading
  re-parses the full document. Fold when parse-path unification lands.
- [ ] **Search highlight walks the full item list multiple times per
  keystroke.** `highlightAllMatches()` walks all items finding matches,
  then `updateMatchCount()` walks them again to recompute the current
  index. For large documents with many matches this is O(n\*m) per
  keystroke.
- [ ] **`replaceAll()` doesn't wrap or cross-item-coordinate undo.**
  Uses raw `m_coordinator->items()` iteration without the
  `textItemsInSearchOrder` helper — inconsistent with `findText`. Also
  note the `first.endEditBlock()` line that operates on a different
  cursor variable than `begin`; it works because both point at the same
  document but the code is confusing.
- [ ] Performance benchmark harness — no existing test measures
  keystroke-to-reparse latency or large-document load time. Needed
  before tuning any of the above.

## Live Preview Polish

- [ ] Heading hash prefix visibility: when cursor is on a heading line
  but NOT adjacent to the hashes (e.g., at end of heading text), should
  the hashes be visible? Currently they hide. Obsidian shows them for
  the entire line. May want heading prefix to be line-level, not
  element-level.
- [ ] Footnote superscript rendering — `QTextCharFormat::AlignSuperScript`
  may not render correctly in the editor's graphics-item paint path. May
  need a custom paint pass like the math substitution.
- [ ] **`snapCursorPastDelimiters` is disabled whenever any U+FFFC is
  present in the document.** Documents with math or checkboxes lose
  cursor-snap-past-hidden-delimiters behavior entirely. Consider
  computing spans in substituted-document coordinates (or keeping two
  span maps) so snap works alongside substitution.

## Style / Theme API

- [ ] **Consolidate hardcoded visual constants into `Theme`.** See
  `architecture.md` Theme System §. Affects:
  - `MarkdownTextItem::paintDecoratedRanges` (code block bg
    `0xf5f5f5`, border `0xe0e0e0`, language label `0x9e9e9e`)
  - `Editor::highlightAllMatches` (search yellow + current-match orange)
  - `DecoratedRange::colorForCalloutType` (callout accent table)
  - `CheckboxTextObject::drawObject` (checked green, unchecked gray)
  - `TableStyle` struct is already defined but unused — either wire it
    in as the table grid/header/padding theme, or delete it (see Dead
    Code section).
- [ ] KDE color scheme integration (Breeze Dark, etc.) — load via
  `Theme::fromSchemeFile()` extended to read KDE color schemes alongside
  the existing QOwnNotes INI format.
- [ ] **`Theme::fromSchemeFile()` hardcodes QOwnNotes INI key format.**
  The index numbers and key patterns (`ForegroundColorEnabled_0`,
  `Bold_4`, `FontSizeAdaption_12`) are QOwnNotes-specific but undocumented
  in the public API. A new user trying to create a theme file would have
  to reverse-engineer the expected format.

## Rendering polish

- [ ] Horizontal rules as actual graphical lines — partially done (the
  text is painted transparent and a graphical line drawn over it in
  `DecoratedRange::HorizontalRule`). Fine for now; revisit only if we
  need drag-resize or theming.
- [ ] Blockquote left border: **done** via per-block depth-aware accent
  bars in `paintDecoratedRanges`. Remaining: nested-list-inside-quote
  edge cases.
- [ ] List bullet rendering — styled bullet character instead of raw `-`.

## Editor API gaps
- [ ] **Live-typed pipe tables don't auto-convert to `QTextTable`
  incrementally.** `SceneCoordinator::reparse()` detects structure
  changes only by segment count and text/block type. If a user types
  `| a | b |\n|---|---|` inside an existing text item, segment count
  doesn't change → the structure-unchanged path runs → `reconcile(doc,
  {})` is called with empty regions → new table isn't converted.
  Workaround today: full `loadMarkdown()` re-detects tables. Fix: have
  the reconcile path re-run `detectTableRegions` on the segment text
  and convert any new regions that don't already have a matching
  `QTextTable` frame.
- [ ] **`TableConverter::reconcile()` called with empty regions from
  reparse.** The alignment info isn't re-derived — it lives only in
  the initial `convert()` call. If the source markdown changes its
  separator row (e.g., `:---:` → `---`), the table's in-memory
  alignment state is stale until a full reload.
- [ ] **Formatting actions are not word-processor-style toggles.** The
  toolbar buttons for Bold, Italic, Strikethrough, and Inline Code have
  two UX problems:

  **1) No visual feedback:** Actions are not `setCheckable(true)`.
  When the caret is inside `**bold text**`, the Bold button doesn't
  appear depressed/checked. `onCursorMoved()` doesn't inspect
  formatting at the cursor position.

  **2) Toggle-off broken without selection:** `wrapSelection()` only
  strips delimiters when there's an active selection. With a bare
  cursor inside `**bold text**`, pressing Ctrl+B inserts empty
  delimiters (`****`) instead of removing the enclosing `**...**`.

  **3) Missing actions for highlight and comment:** `==highlight==` and
  `%%hidden text%%` are parsed by tree-sitter and rendered, but have no
  corresponding `ActionId` entries, no QActions, no `wrapSelection()`
  calls. Need `ToggleHighlight` and `ToggleComment` in `ActionId`.

  **Implementation path:** expose a "format flags at cursor" query
  from the highlighter / span map; call it from `onCursorMoved()` to
  update checked state; extend `wrapSelection()` for the
  no-selection toggle-off case using `SourceSpan::parentCharStart/End`;
  add the two missing action IDs and wire them up.

- [ ] **Round-trip fidelity: blank lines lost in selectAll+copy.**
  Copying the full document via selectAll+copy produces markdown that
  differs from the original source — blank lines between blocks are
  dropped or normalized. Root cause likely in
  `SceneCoordinator::toMarkdown()` which joins items with a hardcoded
  `\n` or `\n\n`, discarding original inter-block whitespace. Affects
  line-number precision for cross-tool integrations.
- [ ] **Context menu lacks formatting actions.** Right-click with a
  selection offers only Undo/Redo/Cut/Copy/Paste/Select All. You can't
  right-click a selection and Bold/Italic/etc. Minor UX gap.
- [ ] **`Editor::setReadOnly()` uses a hand-maintained action whitelist.**
  Every new editing action must be added to the static array in
  `setReadOnly()` or it's accidentally editable in read-only mode.
  Should be a predicate on the action itself (e.g., a `ReadOnly` vs
  `Editing` category in `ActionId`) rather than a manual list.
- [ ] **Link interaction modes.** Two configurable modes needed,
  controlled via `EditorSettings`. Prerequisites: standard markdown
  links (`[text](url)`) must get `setAnchor(true)` /
  `setAnchorHref()` in the highlighter (currently only wikilinks are
  annotated). **(blocked: spec)**

  **Standard mode** (current): clicking a link reveals the raw
  markdown. Ctrl+click follows the link.

  **Obsidian mode**: links are directly followable by click. Hovering
  shows a pointing-finger cursor. Caret movement (arrow keys) into a
  link reveals the raw markdown and makes it unfollowable. Right-click
  offers "Edit Link" which selects the link destination text.

- [ ] **EditorSettings is declared but never applied.** The struct has
  fields for `tabSize`, `lineNumbers`, `lineWrap`, `highlightCurrentLine`,
  `highlightingEnabled`, `tripleClickSelectsLine`, but `setEditorSettings()`
  just stores the struct — nothing reads or applies any of these values.
  Either wire them up or remove the dead API surface.
- [ ] **`FoldingTypes.h` has hidden include-order dependency.** The
  public header forward-declares `HeadingInfo` but `computeHeadingPaths()`
  takes `const QList<HeadingInfo> &`. The consuming TU must have
  already included the markoff-parser header. Should either include
  the header or move the function to a non-public header.
- [ ] **`setFontSize()` mutates the theme.** `Editor::setFontSize()`
  modifies `m_theme.textFont` directly, so `editor->theme()` returns a
  theme reflecting the current font size regardless of what was set via
  `setTheme()`.
- [ ] **`ImageBlockItem` doesn't respond to width changes.**
  `SceneCoordinator::setItemWidth()` calls `setTextWidth()` for text
  items but ImageBlockItem is created with a `maxWidth` at
  construction and never updated on viewport resize.
- [ ] Cross-item find/replace wraparound works but doesn't surface
  "wrapped" feedback to the caller — UI can't show "End of file
  reached, search wrapped".
- [ ] `Editor::wrapSelection` doesn't handle "selection has delimiters
  at start/end with trailing content" or partial-overlap edge cases.

## Code Quality / Architecture

- [ ] **Editor.cpp is a 2200-line god class.** Up from 1600 at the last
  audit. Search/replace, formatting actions, scroll position math, link
  signal subscription, table operations, QAction registry all live
  here. Defer extraction until a concrete feature (regex search,
  multi-cursor, scripting host) forces specific seams — premature
  extraction tends to add surface without reducing coupling.
- [ ] **Span offset tracking is doubly-redundant** between
  `MarkdownHighlighter::adjustSpanOffsets()` (incremental, per
  keystroke) and the full reparse in `SceneCoordinator::reparse()`
  (periodic, replaces span map). Intentional but worth documenting.
  (Previously triply-redundant — the table offset mapping was removed
  in April 2026.)
- [ ] **`dynamic_cast` pattern for item type dispatch is inconsistent.**
  Some callsites check `isTextItem()` then `static_cast`, others use
  `dynamic_cast` without checking. Standardize on `isTextItem()` +
  `static_cast`.
- [ ] **`goto` in `MarkdownTextItem::keyPressEvent`.** The CJK autocorrect
  logic uses `goto cjk_done;`. Restructure as a helper function or
  early-return.

## Dead / orphaned code

Handled in the 2026-04-16 foundation pass. `TableStyle.h` and
`MarkoffBlockData.h` were confirmed unused and removed.
`tst_completion_triggers.cpp` was registered in `tests/CMakeLists.txt`
(it had been orphaned — the tests themselves pass unchanged).

`StubBlockItem.h/cpp` remains in `src/` intentionally: it's reachable
only via the private include path (no host app can link to it), it's
a legitimate minimal `BlockItem` reference implementation, and it's
used both by `tests/tst_selection.cpp` and `app/scene-demo/main.cpp`.
Extracting it into `tests/` would complicate the demo app's build for
no runtime benefit.

## Testing

- [ ] **MarkdownHighlighter has no tests.** AST-to-format application,
  delimiter hiding, code-block KSyntaxHighlighting integration,
  cursor-aware visibility — all untested.
- [ ] **SceneCoordinator has no direct tests.** Item lifecycle,
  reparse, serialization round-trip, heading map building,
  applyFoldVisibility are tested only indirectly through Editor.
- [ ] **CheckboxTextObject has no tests.** Related: `toggleCheckbox()`
  bug above persists because nothing checks it.
- [ ] **Cross-item undo** (see Editor API gaps) has no test coverage.

## Accessibility

- [ ] **No accessibility support.** None of the QGraphicsItems set
  accessibility properties. MarkdownTextItem doesn't expose its text
  content to `QAccessibleInterface`. Screen readers will see a
  QGraphicsView with opaque items. Needs a scoping pass before code —
  there is no point adding `QAccessible*` stubs without a plan to
  validate end-to-end with a real screen reader (Orca / NVDA / VoiceOver).

## Build hygiene

- [ ] **CMakeLists.txt enables no warning flags.** No
  `-Wall -Wextra -Wpedantic`, no `-Werror`. For a library hosting a
  ~2,600-line Qt fork this is a real drift risk. Add opt-in via a
  `MARKOFF_PEDANTIC` cache variable so it doesn't break downstream
  consumers with stricter toolchains.

## Recently fixed (for context)

- **TextControl direct test coverage** (2026-04-16): six new test files —
  cursor (13 slots), selection (8), editing (10), input (10, absorbs
  tst_cjk_autocorrect), links (6), integration through MarkdownTextItem
  (6) — plus header-only `tests/support/textcontrol_testutil.{h,cpp}`
  with factories and event-synthesis helpers. 53 slots covering Tier 1
  (fork-specific / known-risky) + Tier 2 (general correctness).
  `tst_cjk_autocorrect.cpp` removed; its cases live in
  `tst_textcontrol_input`. Plan:
  `docs/plans/2026-04-16-textcontrol-test-coverage.md`. Spec:
  `docs/specs/2026-04-16-textcontrol-test-coverage-design.md`.
- **Foundation pass** (2026-04-16): doc refresh (architecture.md,
  TODO.md, spec status headers for QAction shortcuts + editable
  tables); `Editor::toggleCheckbox()` rewritten to inspect
  `CheckboxTextObject::CheckedProperty` rather than grepping
  substituted document text, with three-state cycle (plain → unchecked
  → checked → plain); `SceneCoordinator::reparse()` now clears
  `m_inReparse` synchronously before emitting so `reparsed` handlers
  can edit text without having the follow-up reparse suppressed;
  `tests/tst_completion_triggers.cpp` (previously orphaned from
  CMakeLists) registered; dead headers `TableStyle.h` and
  `MarkoffBlockData.h` removed. New tests: `tst_checkbox_toggle`,
  `tst_scene_coordinator`, and `cursorColumnIsLineLocal` /
  `cursorColumnAfterSubstitutedMath` in `tst_global_coordinates`.
- **Editable tables v1** (2026-04-16): pipe tables convert to
  `QTextTable` frames inside `MarkdownTextItem` via `TableConverter`
  at load time. Native cell editing, Tab/Shift+Tab cell navigation,
  context menu for insert/delete row/column, column alignment
  preserved through reparse. `TableBlockItem` removed.
  Spec: `docs/specs/2026-04-16-editable-tables-design.md`.
  Plan: `docs/plans/2026-04-16-editable-tables.md`.
- **QAction-based keyboard shortcuts** (2026-04-16): `ActionId` enum
  + `createActions()` registry with `QKeySequence::Undo` / `Copy` /
  `Find` / `Bold` / etc. Host apps retrieve actions via
  `Editor::action(id)`. Read-only mode toggles an editing-action
  allowlist. Spec: `docs/specs/2026-04-16-qaction-shortcuts-design.md`.
- **Audit top-4 fixes** (2026-04-16): `selectAll()` now selects across
  all items via SelectionManager. `cut()` now removes fully-selected
  block items. `goToLine()` now navigates across item boundaries.
  `cursorLine()` returns document-global source line. Ctrl+A routes
  through the same path as context-menu Select All.
  Note: `cursorColumn()` was NOT fixed in this pass — still item-local
  (see Editor API gaps).
- **Heading folding v1**: `Editor::fold/unfold/toggleFold/
  toggleFoldAtCursor/foldAll/unfoldAll/foldAllAtLevel/foldLevel` +
  JSON persistence. Left gutter with triangle arrows; Ctrl+Click folds
  all at level. Auto-unfold on `scrollToHeading` and `findText`. State
  keyed by heading hierarchy path; reconciled per reparse.
- **LinkRenderer emission surface** (Cluster J phase 3).
  `TextControl::linkActivated` bridged through
  `Editor::handleLinkActivated` to the typed `LinkRenderer`. Wikilinks
  annotated with `wikilink://` anchor hrefs by the highlighter.
  Standard markdown links are NOT yet clickable (no anchor properties
  set — see Editor API gaps / Link interaction modes).
- **Math reveal simplified**: arrow-key reveal removed; only mouse
  clicks expand a math glyph to source for editing. Earlier ~300 LOC
  reentrancy-guarded subsystem is now much smaller.
- **In-editor image rendering**: `ImageBlockItem` wired into
  `SceneCoordinator::loadMarkdown()` via `MarkdownSplitter`. Uses
  `ResourceProvider` for path resolution.
- **Checkbox rendering via `CheckboxTextObject`** (QTextObjectInterface).
  Click-to-toggle in `MarkdownTextItem::mousePressEvent()`. NOTE: the
  keyboard action `Editor::toggleCheckbox()` is still broken — see
  Editor API gaps.
- **`Editor::wrapSelection`** toggles off when the selection is
  already wrapped OR when the selection is inside outer delimiters.
- **`Editor::findText`** wraps within a single text item.
- **`Renderer::setTheme()` added**; reading-view CSS derives colors
  from the theme. (Renderer lives in markoff-parser / host app.)
- **`MarkdownHighlighter` code-block content** uses `Theme::codeFont`.
- **Inline math** via `QTextObjectInterface`.
- **`Editor::scrollToHeading`** correctly converts `sourceOffset`
  (UTF-8 byte offset) to line number.
- **`MathRenderer` font size** is configurable.
