# markoff-core

Shared primitives for the Markoff tri-view family (live preview, source, reading).

Public contract:
- `Markoff::MarkdownView` — abstract QWidget base implemented by each leaf view widget.
- `Markoff::MarkoffDocument` — canonical markdown source + undo + cached parse. Views attach via `setDocument()`.
- `Markoff::SearchController` / `ReplaceController` / `SearchAdapter` — view-agnostic find/replace engine.
- `Markoff::Theme`, `Markoff::ResourceProvider`, `Markoff::LinkResolver` (future work in Phase C) — currently live in markoff-live and will migrate here as duplication is consolidated.

Depends on: Qt6 (Core, Gui, Widgets), KF6::SyntaxHighlighting, MarkoffParser. Does NOT depend on any leaf widget library.

See `docs/specs/2026-04-20-tri-view-unified-api-design.md` at the Markoff top level for the overall architecture.
