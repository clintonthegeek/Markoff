# Markoff TODO

Polish items and known issues to address. Newer items at the top of each
section. Items marked **(blocked: spec)** need a design decision before
implementation; the rest are implementation tasks.

## Big-ticket features (need spec or brainstorm first)

- [ ] **Editable tables** — current `TableBlockItem` is read-only. Two known
  paths: (a) keep custom paint, add cell-edit overlay; (b) pivot to
  `QTextTable` in a hybrid layout. See `TODO-tables.md` for the historical
  attempt and its failure modes. **(blocked: spec)**
- [ ] **Math cursor reveal polish** — inline math cursor reveal is
  implemented but the mechanism is complex (~300 lines with reentrancy
  guards). Consider simplifying — e.g., per-item reveal instead of
  per-glyph reveal.
- [ ] **Obsidian-flavored grammar additions** in vendored tree-sitter:
  - `![[embed]]` (embed prefix on wikilinks)
  - `^block-id` block reference
  Requires forking the vendored grammar. Note: `==highlight==` and
  `%%comment%%` are already supported by the vendored grammar.

## Performance

- [ ] Incremental tree-sitter parsing — use `ts_tree_edit()` to update
  the old tree instead of full reparse on every keystroke. Tree-sitter
  is designed for this; only the changed region gets re-parsed.
- [ ] Incremental rehighlight — only rehighlight blocks whose spans
  actually changed, not the entire document. Compare old and new span
  maps to find dirty blocks.
- [ ] **`ensureHeadingMap()` does a full tree-sitter reparse on every fold
  state change.** Every call to `enclosingHeadingPath`, `headingAtBlock`,
  `headingIndexForItem` triggers `ensureHeadingMap()` which — when dirty —
  runs a fresh `TreeSitterParser` over the entire document. The heading
  map is marked dirty on every `foldStateChanged`, so folding a single
  heading re-parses the full document. Should either cache the heading
  map more aggressively or derive it from the existing reparse rather
  than running a second parse.
- [ ] Reduce spurious `textChanged` signals — `rehighlight()` modifies
  document formatting which fires `contentsChanged` → `textChanged`
  (currently suppressed by `inReparse` guard but still 3 wasted signal
  emissions per keystroke).
- [ ] Consider `QSyntaxHighlighter::rehighlightBlock()` for targeted
  updates instead of full `rehighlight()` after reparse.
- [ ] **Search highlight walks the full item list multiple times per
  keystroke.** `highlightAllMatches()` walks all items finding matches,
  then `updateMatchCount()` walks them again to recompute the current
  index. For large documents with many matches this is O(n*m) per
  keystroke.

## Live Preview Polish

- [ ] Heading hash prefix visibility: when cursor is on a heading line
  but NOT adjacent to the hashes (e.g., at end of heading text), should
  the hashes be visible? Currently they hide. Obsidian shows them for
  the entire line. May want heading prefix to be line-level, not
  element-level.
- [ ] Footnote superscript rendering — `QTextCharFormat::AlignSuperScript`
  may not render correctly in the editor's graphics-item paint path. May
  need a custom paint pass like the math substitution.

## Style / Theme API

- [ ] Callout colors are now centralized in `Renderer.cpp` (`kCalloutColors`
  table) but still hardcoded — they could move to a `Theme` keyed by
  callout-type string for full theme control. Lower priority.
- [ ] KDE color scheme integration (Breeze Dark, etc.) — load via
  `Theme::fromSchemeFile()` extended to read KDE color schemes alongside
  the existing QOwnNotes INI format.
- [ ] **`Theme::fromSchemeFile()` hardcodes QOwnNotes INI key format.**
  The index numbers and key patterns (`ForegroundColorEnabled_0`,
  `Bold_4`, `FontSizeAdaption_12`) are QOwnNotes-specific but undocumented
  in the public API. A new user trying to create a theme file would have
  to reverse-engineer the expected format.

## Rendering polish

- [ ] Horizontal rules as actual graphical lines (not just styled `---`
  text). Either custom paint in `MarkdownTextItem` or a dedicated
  `HorizontalRuleItem` block in the splitter.
- [ ] Task list checkboxes as graphical widgets that toggle on click.
  Currently rendered as the unicode `☐` / `☑` symbols.
- [ ] Blockquote left border (visual indicator beyond just indent + gray
  text). Custom paint in `MarkdownTextItem`.
- [ ] List bullet rendering — styled bullet character instead of raw `-`.

## Parser / Grammar

## Editor API gaps

- [ ] **`toggleCheckbox()` is broken by the substitution layer.** The
  `CheckboxTextObject` replaces `[ ]`/`[x]` in the document with
  `U+FFFC` object replacement characters for graphical rendering. But
  `toggleCheckbox()` reads the line from the substituted document text,
  so `line.contains("- [ ]")` never matches — it sees `- \uFFFC text`.
  Falls through to the else branch, which prepends another `- [ ] `,
  producing `- [ ] - ☐ text` (extra dashes accumulate on each
  invocation). Fix: either (a) inspect the `U+FFFC` character's format
  properties (`CheckboxTextObject::CheckedProperty`) to determine
  checked state, or (b) work from the raw source text (pre-substitution)
  rather than the rendered document. Additionally, the three-state cycle
  should be: plain → `- [ ] text` → `- [x] text` → plain (removing the
  `- ` prefix entirely), not plain → checkbox → toggle checked state
  forever with no way to remove the checkbox.
- [ ] **Formatting actions are not word-processor-style toggles.** The
  toolbar buttons for Bold, Italic, Strikethrough, and Inline Code have
  two UX problems:

  **1) No visual feedback:** Actions are not `setCheckable(true)`.
  When the caret is inside `**bold text**`, the Bold button doesn't
  appear depressed/checked. `onCursorMoved()` doesn't inspect
  formatting at the cursor position. Fix: query the span map on cursor
  movement, determine which formatting flags (`bold`, `italic`,
  `strikethrough`, `code`, `highlight`, `comment`) are active at the
  cursor position, and `setChecked()` accordingly.

  **2) Toggle-off broken without selection:** `wrapSelection()` only
  strips delimiters when there's an active selection (lines 1098-1138).
  With a bare cursor inside `**bold text**`, pressing Ctrl+B inserts
  empty delimiters (`****`) instead of removing the enclosing `**...**`.
  Fix: when there's no selection, detect the enclosing formatting span
  via `SourceSpan::parentCharStart`/`parentCharEnd` and strip its
  delimiters.

  **3) Missing actions for highlight and comment:** `==highlight==` and
  `%%hidden text%%` are parsed by tree-sitter (SourceSpan has
  `highlight` and `comment` flags) and rendered by the highlighter, but
  have no corresponding `ActionId` entries, no QActions, no
  `wrapSelection()` calls, and no toolbar/menu presence. Need:
  `ToggleHighlight` (`==`, `==`) and `ToggleComment` (`%%`, `%%`) in
  `ActionId`, with shortcuts (Obsidian uses Ctrl+Shift+H for highlight;
  comment has no standard shortcut).

  **Implementation path:** The `SourceSpan` infrastructure already
  carries all needed data (formatting flags + delimiter parent ranges).
  The `MarkdownHighlighter` already consumes span maps per-item. The
  work is: (a) expose a "format flags at cursor" query from the
  highlighter or span map, (b) call it from `onCursorMoved()` to update
  checked state, (c) extend `wrapSelection()` for the no-selection
  toggle-off case using parent delimiter ranges, (d) add the two
  missing action IDs and wire them up.

- [ ] **Round-trip fidelity: blank lines lost in selectAll+copy.**
  Copying the full document via selectAll+copy produces markdown that
  differs from the original source — specifically, blank lines between
  blocks are either dropped or normalized. The root cause is likely in
  `SceneCoordinator::toMarkdown()` which joins items with a hardcoded
  `\n` (text-text) or `\n\n` (block involved), discarding the original
  inter-block whitespace from the source. If the source had `\n\n\n`
  between paragraphs, round-tripping collapses it to `\n`. This matters
  for line-number precision: if copied text doesn't match the source
  byte-for-byte, global line numbers drift. Needs investigation into
  whether `MarkdownSplitter` should preserve original separator strings,
  or whether `toMarkdown()` should reconstruct them from source offsets.
- [ ] **Keyboard shortcuts are hardcoded key checks, not QActions.**
  `Editor::keyPressEvent` contains a wall of `if (e->key() == Qt::Key_X)`
  checks for Ctrl+C, Ctrl+X, Ctrl+V, Ctrl+A, Ctrl+F, Ctrl+H, Ctrl+Plus,
  Ctrl+Minus. None use `QAction` or `QShortcut`, so the host app cannot
  remap them, they don't appear in menus with shortcut hints, and adding
  new shortcuts means more hardcoded branches. Should migrate to `QAction`
  instances with standard `QKeySequence` values (`QKeySequence::SelectAll`,
  `QKeySequence::Copy`, etc.) so shortcuts are configurable and discoverable.
- [ ] **Link interaction modes.** Two configurable modes needed, controlled
  via `EditorSettings`. **(blocked: spec)**
  
  **Standard mode** (current behavior, mostly): clicking a link reveals
  the raw markdown (same as clicking any formatted region). Ctrl+click
  follows the link. This is the typical "source-aware" editor model.

  **Obsidian mode**: links are directly followable by click. Hovering
  shows a pointing-finger cursor. But keyboard caret movement (arrow
  keys) into a link reveals the raw markdown and makes it unfollowable
  by click (cursor-in-link = edit mode). When a link IS followable
  (cursor not inside it), right-click offers "Edit Link" which: (a)
  ceases mouse-click-to-follow, (b) reveals the raw markdown, (c)
  selects the link destination text for easy replacement.

  **Prerequisites**: standard markdown links (`[text](url)`) must get
  `setAnchor(true)` / `setAnchorHref()` in the highlighter — currently
  only wikilinks are annotated. Both modes need this. The Obsidian mode
  additionally needs: cursor-proximity-aware link state (followable vs.
  editable), hover cursor changes, and context menu integration.
- [ ] **EditorSettings is declared but never applied.** The struct has
  fields for `tabSize`, `lineNumbers`, `lineWrap`, `highlightCurrentLine`,
  `highlightingEnabled`, `tripleClickSelectsLine`, but `setEditorSettings()`
  just stores the struct — nothing reads or applies any of these values.
  Either wire them up or remove the dead API surface.
- [ ] **`FoldingTypes.h` has hidden include-order dependency.** The public
  header forward-declares `HeadingInfo` but `computeHeadingPaths()` takes
  `const QList<HeadingInfo> &`. The consuming TU must have already included
  the markoff-parser header that defines `HeadingInfo`, or compilation
  fails. Should either include the header or move the function to a
  non-public header.
- [ ] **`setFontSize()` mutates the theme.** `Editor::setFontSize()`
  modifies `m_theme.textFont` directly, so after calling `setFontSize(18)`,
  `editor->theme()` returns a theme with 18pt font regardless of what was
  set via `setTheme()`. The theme and font size are coupled in a surprising
  way.
- [ ] **`ImageBlockItem` and `TableBlockItem` don't respond to width
  changes.** `SceneCoordinator::setItemWidth()` calls `setTextWidth()` for
  text items but block items are created with a `maxWidth` at construction
  and never updated on viewport resize.
- [ ] Cross-item find/replace wraparound now works for both single-item
  and multi-item documents, but doesn't surface "wrapped" feedback to the
  caller. UI can't show "End of file reached, search wrapped".
- [ ] `Editor::wrapSelection` toggle behavior handles two cases (selection
  IS the wrapped form, and selection is INSIDE outer delimiters), but
  doesn't yet handle "selection has the delimiters at the start/end with
  trailing/leading content" or partial-overlap edge cases.

## Code Quality / Architecture

- [ ] **Editor.cpp is a 1600-line god class.** Search/replace logic,
  formatting actions, scroll position math, and link signal subscription
  could be extracted into focused helper classes. When new features are
  added, Editor.cpp is where all the churn lands.
- [ ] **Span offset tracking is doubly-redundant.** Two systems maintain
  overlapping offset state: `MarkdownHighlighter::adjustSpanOffsets()`
  (incremental, on each keystroke) and the full reparse in
  `SceneCoordinator::reparse()` (periodic, replaces span map). The
  incremental adjuster keeps highlighting approximately correct between
  reparses; the reparse replaces it with a fresh parse. This is
  intentional but worth understanding. (Previously triply-redundant —
  the table offset mapping layer was removed in April 2026.)
- [ ] **`dynamic_cast` pattern for item type dispatch is inconsistent.**
  Some callsites check `isTextItem()` then `static_cast`, others use
  `dynamic_cast` without checking. Should standardize on `isTextItem()` +
  `static_cast` for consistency and performance.
- [ ] **`MarkoffBlockData` appears unused.** The class has `DisplayMode`,
  `renderedHeight`, `renderedCache`, `cacheValid` fields but no code
  creates or reads instances. Verify and remove if dead.
- [ ] **`goto` in `MarkdownTextItem::keyPressEvent`.** The CJK autocorrect
  logic uses `goto cjk_done;`. Should restructure as a helper function
  or early-return.

## Testing

- [ ] **TextControl has zero direct test coverage.** The 2000-line Qt fork
  is exercised only indirectly through MarkdownTextItem. No tests for
  cursor movement edge cases, input method handling, drag-and-drop, link
  activation, preedit composition, or triple-click behavior.
- [ ] **MarkdownHighlighter has no tests.** The AST-to-format application
  is untested. No verification that spans produce correct character
  formats, delimiter hiding works correctly, or code block syntax
  highlighting applies.
- [ ] **SceneCoordinator has no tests.** Item lifecycle, reparse, and
  serialization round-trip are only tested indirectly through Editor.

## Accessibility

- [ ] **No accessibility support.** None of the QGraphicsItems set
  accessibility properties. MarkdownTextItem doesn't expose its text
  content to QAccessibleInterface. Screen readers will see a
  QGraphicsView with opaque items. No high-contrast mode support, no
  keyboard-only navigation testing.

## Recently fixed (for context)

- Audit top-4 fixes (2026-04-16): `selectAll()` now selects across all
  items via SelectionManager (was item-local). `cut()` now removes
  fully-selected block items (tables/images no longer survive cut).
  `goToLine()` now navigates across item boundaries via
  `SceneCoordinator::itemAtGlobalLine()`. `cursorLine()` now returns
  document-global source line via `SceneCoordinator::globalPositionOf()`.
  Ctrl+A keyboard shortcut now routes through the same path as the
  context menu Select All. Spec: `docs/specs/2026-04-15-audit-top4-fixes-design.md`.
  Plan: `docs/plans/2026-04-15-audit-top4-fixes.md`.
- Heading folding (v1): `Editor::fold`, `unfold`, `toggleFold`,
  `toggleFoldAtCursor`, `foldAll`, `unfoldAll`, `foldAllAtLevel`,
  `foldLevel` and persistence hooks (`serializeFoldState` /
  `restoreFoldState`). Left gutter with triangle arrows; Ctrl+Click
  folds all at level. Auto-unfold on `scrollToHeading` and `findText`,
  emitting `foldsAutoExpanded(paths)`. State keyed by heading hierarchy
  path; reconciled per reparse so renames drop folds.
- LinkRenderer emission surface wired (Cluster J phase 3).
  `TextControl::linkActivated` is bridged to `Editor::handleLinkActivated`
  which routes through the typed `LinkRenderer`. Wikilinks annotated with
  `wikilink://` anchor hrefs by the highlighter. Standard markdown links
  are NOT yet clickable (no anchor properties set).
- In-editor image rendering: `ImageBlockItem` is implemented and wired
  into `SceneCoordinator::loadMarkdown()` via the `MarkdownSplitter`
  Image segment type. Uses `ResourceProvider` for path resolution.
- Checkbox rendering via `CheckboxTextObject` (QTextObjectInterface).
  Click-to-toggle handled in `MarkdownTextItem::mousePressEvent()`.
- `Editor::wrapSelection` now toggles off when the selection is already
  wrapped, OR when the selection is inside outer delimiters.
- `Editor::findText` now wraps within a single text item.
- `Editor::setResourceProvider` now forwards to `SceneCoordinator`.
- `Renderer::setTheme()` added; reading-view CSS now derives colors
  from the theme.
- `MarkdownHighlighter` code-block content now uses `Theme::codeFont`.
- Inline math via `QTextObjectInterface` — see `MathTextObject.h`.
- `Editor::scrollToHeading` correctly converts `sourceOffset` (UTF-8
  byte offset) to line number via newline counting.
- `MathRenderer` font size is now configurable.
