# markoff-source

Fully-owned QtWidgets Source view on `markoff-core`. Replaces the Qutepart-based legacy source view over time.

> **Required reading before seam work:**
> [`../../docs/VIEW-IMPLEMENTORS-GUIDE.md`](../../docs/VIEW-IMPLEMENTORS-GUIDE.md)
> — the cross-cutting view↔model concerns and contracts. This leaf shares
> `SourceTextDocumentBinding` with `markoff-styled`, so the §B cursor-authority
> gap applies here too; the cursor-authority fix that lands for styled should
> close it for source in the same stroke.

## Public surface
- `Markoff::Source::Editor` — `QPlainTextEdit` subclass; main public widget.
- `Markoff::Source::FindBar` — standalone find UI.

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
