# markoff-source

Fully-owned QtWidgets Source view on `markoff-core`. Replaces the Qutepart-based legacy source view over time.

> **Required reading before seam work:**
> [`../../docs/VIEW-IMPLEMENTORS-GUIDE.md`](../../docs/VIEW-IMPLEMENTORS-GUIDE.md)
> — the cross-cutting view↔model concerns and contracts.
>
> §B.1/§B.3 cursor authority is **closed** here too — `markoff-source` shares
> `SourceTextDocumentBinding`, and the 2026-05-27 caret-authority fix
> (`../../docs/specs/2026-05-27-flat-view-enter-and-caret-authority-design.md`)
> wired `caretResolved` → `setTextCursor` in its Editor.

## WP unification (2026-05-28)

Source view is structurally identical to styled — same `widgetFlatView()`,
same caret-authority chokepoint, same Enter/backspace semantics. It is
distinguished only by *not rendering* the inline markdown markers (the
KSyntaxHighlighting pass keeps them visible as characters; styled hides
them). Paragraph margins are applied via `Editor::applyParagraphMargins()`
on every `d2DocumentChanged` (5pt top + 5pt bottom). Spec
`../../docs/specs/2026-05-28-flat-view-wp-unification-design.md`.

## Public surface
- `Markoff::Source::Editor` — `Markoff::MarkdownView` subclass composing an
  inner `QPlainTextEdit`. Main public widget.
- `Markoff::Source::FindBar` — standalone find UI.

### MarkdownView contract overrides (contract-v2 arc, 2026-06-09)

`Source::Editor` implements the full base contract:

| Override | Notes |
|---|---|
| `setDocument` | wires `SourceTextDocumentBinding` + KSyntaxHighlighting |
| `cursorPosition()` / `setCursorPosition()` | `blockNumber()+1`, `positionInBlock()+1`; set via `QTextCursor` positioned at the flat visual line |
| `scrollPositionVisualLine()` / `setScrollPositionVisualLine()` | reads/sets `verticalScrollBar()` as fraction of maximum |
| `setReadOnly(bool)` / `isReadOnly()` | delegates to the inner QPlainTextEdit |
| `hasCursor()` | returns `true` |
| `hasEditing()` | returns `!isReadOnly()` |
| `attachFindController` / `detachFindController` | `Detail::SourceFindAdapter`; uses `ExtraSelections` on the inner editor; attach after `setDocument` |
| `undo()` / `redo()` | inherited from base (→ `doc->undoD2/redoD2`); no-op while read-only |
| `setTheme` / `theme()` | stores + signals in base; override applies the theme palette to the inner editor |
| `setFontScale` / `fontScale()` | scales inner QPlainTextEdit font from a lazily-captured base size; rescales gutter; re-applies paragraph margins |
| `toggleBold` / `toggleItalic` / `toggleStrikethrough` / `toggleInlineCode` | thin wrappers over `Markoff::FormatOps::wrapToggle`; re-apply cursor from the returned `std::optional<QtRange>` |
| `insertLink` | thin wrapper over `Markoff::FormatOps::insertLink` |
| `setHeadingLevel(int)` | thin wrapper over `Markoff::FormatOps::setHeadingLevel` |
| `contextChanged` signal | recomputed on `QPlainTextEdit::cursorPositionChanged` only (NOT `d2DocumentChanged` — see spec §7 deviation); change-gated |
| `cursorPositionChanged` signal | emitted from `QPlainTextEdit::cursorPositionChanged` |
| `scrollPositionChanged` signal | emitted on vertical scroll bar changes |

**contextChanged trigger note:** `d2DocumentChanged` is intentionally not
connected. The syntax highlighter's format-only `contentsChange` notifies
reach `d2DocumentChanged` via the binding's no-op edit path; connecting it
would false-fire and defeat the change-gate. Every real structural key that
changes block kind also moves the caret, so cursor-driven triggering covers
normal interactive use. Low-severity staleness gap on programmatic kind
changes without a caret move — see queue #15.

## Internal
- `Markoff::Source::Detail::Gutter` — line-number gutter, child of the editor. Single-column at v0; polymorphic-column shape (per legacy `markoff-live::FoldGutter`) when fold arrows arrive.
- `Markoff::Source::Detail::InnerEditor` — thin QPlainTextEdit subclass promoting protected geometry accessors to public.

## Dependencies
- Qt6 Core / Gui / Widgets
- KF6::SyntaxHighlighting
- `markoff-core` (Theme, MarkoffDocument, Session, SourceTextDocumentBinding, SearchEngine)

## Conventions
- C++20, Qt6.8+, CMake 3.19+.
- SPDX `GPL-3.0-or-later` on every file.
- `tr()` for user-visible strings.
- `QIcon::fromTheme()` for icons.
- Tests prefix `tst_source_widget_*`.

## Spec
`docs/specs/2026-04-29-source-widget-design.md`
