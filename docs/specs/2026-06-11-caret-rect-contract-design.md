# caretRect() — MarkdownView contract extension

**Date:** 2026-06-11 · **Status:** Shipped with this commit · **Driver:**
Corbomite completion revival
(`Corbomite:docs/superpowers/specs/2026-06-11-completion-revival-design.md` §4).

`virtual QRect MarkdownView::caretRect() const` — the caret rectangle in the
view widget's own coordinate system; invalid `QRect{}` when no caret is
established (no document / no focus / no text-bearing cursor state). Base
default: invalid. Consumers anchor transient UI (completion popups) at
`bottomLeft()`.

Implementations: source/styled map the inner text widget's `cursorRect()`
viewport→editor; live reads the window `activeFocusItem`'s
`cursorRectangle` property and maps scene→widget (read-only query — no new
cursor authority, INVARIANTS #3; the focused TextEdit IS the active-focus
item whenever a TextCaret/cell edit is live, so all text-bearing delegate
kinds are covered without per-delegate QML work).

Tests: `tst_markdown_view_base::caretRect_baseDefault_isInvalid`,
caret-rect slots in `tst_view_contract_source` / `tst_view_contract_styled`,
`tst_view_contract_live_caret_rect` (falsifiability-probed).
