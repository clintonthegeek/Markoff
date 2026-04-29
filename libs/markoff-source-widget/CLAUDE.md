# markoff-source-widget

Fully-owned QtWidgets Source view on `markoff-foundation`. Replaces the Qutepart-based `markoff-source` (legacy) over time.

## Public surface
- `Markoff::Source::Widget::Editor` — `QPlainTextEdit` subclass; main public widget.
- `Markoff::Source::Widget::FindBar` — standalone find UI.

## Internal
- `Markoff::Source::Widget::Gutter` — line-number gutter, child of the editor. Single-column at v0; polymorphic-column shape (per legacy `markoff-live::FoldGutter`) when fold arrows arrive.

## Dependencies
- Qt6 Core / Gui / Widgets
- KF6::SyntaxHighlighting
- `markoff-foundation` (Theme, MarkoffDocument, Session, SourceTextDocumentBinding, SearchEngine)

## Conventions
- C++20, Qt6.8+, CMake 3.19+.
- SPDX `GPL-3.0-or-later` on every file.
- `tr()` for user-visible strings.
- `QIcon::fromTheme()` for icons.
- Tests prefix `tst_source_widget_*`.

## Spec
`docs/specs/2026-04-29-source-widget-design.md`
