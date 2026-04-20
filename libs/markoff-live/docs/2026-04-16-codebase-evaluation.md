# Markoff Codebase Evaluation & Architecture Decision

**Date:** 2026-04-16
**Scope:** State-of-library review plus the architecture decision around keeping the split-text-item scene model.

## 1. Overall assessment

Clean foundation, mid-polish phase. Solid layered architecture, unusually disciplined documentation practice, and most remaining work is execution against designs already written — not architectural rethinks. Call it a confident B+ with the ingredients to reach A once a few structural debts are paid down.

### What's genuinely good

- Layered architecture holds up. `markoff-parser` (Qt Core only) cleanly separated from `markoff` (widgets / KF6 / math). `Editor.h` exposes a library-first surface with no Corbomite-specific concepts leaking through. The scope-discipline note in `CLAUDE.md` is actually reflected in the code.
- GPL-harvest discipline (TextControl fork, Kate/Qt source studied before hand-rolling) will age better than private-header tricks.
- Spec / plan / archive layout plus the "Recently fixed" section in `TODO.md` — rare in solo projects. Good fuel for multi-session continuity.
- Editable-tables spec (`docs/specs/2026-04-16-editable-tables-design.md`) is a model document. The "`QTextDocument` already uses `QTextDocumentLayout`, no layout harvest needed" discovery collapsed the implementation risk from a multi-thousand-line layout fork to ~410 LOC.
- Typed `LinkRenderer` emission surface and the `ActionId` enum show an editor API that's thought through, not retrofitted.

### Yellow flags

- `Editor.cpp` at 2,201 LOC — self-identified god class in `TODO.md`.
- `TextControl.cpp` (2,572 LOC harvested Qt fork) has **zero direct tests**.
- Dual parser paths: tree-sitter drives the editor; MD4C-based `DocumentBuilder` still powers the reading-view `Renderer`.
- Round-trip fidelity bug: blank lines collapse through `SceneCoordinator::toMarkdown()` serialization.
- Dead API surface: `EditorSettings` stores six fields (`tabSize`, `lineNumbers`, `lineWrap`, `highlightCurrentLine`, `highlightingEnabled`, `tripleClickSelectsLine`) that nothing reads.
- Hardcoded keyboard shortcuts in `Editor::keyPressEvent` instead of `QAction`-based — spec exists (`docs/specs/2026-04-16-qaction-shortcuts-design.md`), unimplemented.
- `toggleCheckbox()` is broken by the U+FFFC substitution layer.
- Math reveal complexity (~300 LOC with reentrancy guards) — author self-flags this.
- `FoldingTypes.h` public header has a hidden include-order dependency on `markoff-parser`'s `HeadingInfo`.
- `setFontSize()` mutates `m_theme` directly, coupling font size and theme.
- Zero accessibility support.

## 2. Recommended development focus

### The four things that actually matter (ranked)

**1. Add direct tests for TextControl.** You forked ~2,600 LOC of Qt's text editing state machine. Today, the only safety net is that the fork's behavior reflects the original — there's no regression detector if that drifts. This is the single highest-leverage risk in the codebase: higher than any feature on the TODO. A regression in cursor movement, input method handling, preedit composition, or drag-and-drop would be invisible until a user hit it.

**2. Retire MD4C / `DocumentBuilder` before adding more features.** Reading-view `Renderer` still uses MD4C while the editor uses tree-sitter. Every Obsidian grammar extension (embeds, block refs) has to be taught to both parsers. The architecture doc already names this as "planned for removal" — it should probably be a prerequisite for the next round of grammar work, not a follow-up.

**3. Fix round-trip fidelity (blank lines lost on selectAll+copy).** Correctness bug, not polish. Affects file integrity, and the resulting line-number drift bleeds into the global-coordinate subsystem. Quietly dangerous.

**4. Bundle the performance TODOs.** `ts_tree_edit()` incremental parsing, `rehighlightBlock()` targeted updates, and the `ensureHeadingMap()` cache fix all cluster around the "full reparse on every keystroke" problem. They share testing infrastructure and should land together. Not painful today; will compound at 10k-line-note scale.

### Secondary items worth clearing

- Migrate keyboard shortcuts to `QAction` (spec written).
- Either wire `EditorSettings` fields or delete them — don't leave dead public API.
- Fix `toggleCheckbox()` against substitution state (read `CheckboxTextObject::CheckedProperty` instead of line text; add the three-state plain → `[ ]` → `[x]` → plain cycle).
- Fix formatting-action toggles: `setCheckable(true)`, cursor-position-aware checked state, toggle-off without selection via `SourceSpan` parent delimiter ranges, add `ToggleHighlight` and `ToggleComment` action IDs.
- Decouple `setFontSize()` from theme mutation.
- Extract formatting actions, search logic, and scroll math out of `Editor.cpp`.

### Suggested sequencing

1. **TextControl tests** — unblocks safe refactoring everywhere else.
2. **Round-trip fidelity fix** — correctness first.
3. **QAction shortcut migration** — spec already written.
4. **MD4C retirement** — prerequisite for grammar extensions.
5. **Performance bundle** — `ts_tree_edit()` + targeted rehighlight + heading map cache.
6. **Editable tables** — spec already written, low-risk given the layout-engine discovery.
7. **Polish** — checkbox, `EditorSettings`, `setFontSize()`, `Editor.cpp` extraction, formatting-action toggles.
8. **Grammar extensions** — embeds (`![[...]]`), block references (`^block-id`) after MD4C is gone.

## 3. Architecture decision: keep the split-text-item scene model

### The question

Whether to collapse the current split-scene-item architecture — multiple `MarkdownTextItem`s + `BlockItem`s + `SelectionManager` cross-boundary protocol + `SceneCoordinator` orchestrating item lifecycle — into a simpler single-text-widget model.

### Why this came up

The trajectory looks suspicious on inspection:

- Inline math and checkboxes already use `QTextObjectInterface` (opaque U+FFFC glyphs in the text flow).
- The editable-tables spec walks tables *back into* `MarkdownTextItem`'s `QTextDocument` as `QTextTable` frames.
- `MarkdownTextItem` uses a plain `QTextDocument` with Qt's default `QTextDocumentLayout` (not a forked one as originally assumed) — so Qt's native rich-text layout is already doing heavy lifting.

That's a pattern: when a feature can be reduced to "something in the text flow," complexity collapses. The split-item scene model is what you do when a feature *can't* be reduced. The question was whether the remaining features that justify it still do.

### The four options examined

| | **Stay** | **Shrink** | **Rich objects** | **Fork deeper** |
|---|---|---|---|---|
| Split scene model | Full | Embeds-only | Gone | Gone |
| Cross-boundary protocol | Yes | Yes | Gone | Gone |
| Single text widget | No | No | Yes | Yes |
| Interactive in-place embed editing | Possible | Possible | **Not possible** | Possible |
| Upfront cost | 0 new LOC | ~500 LOC migration | ~2–3k LOC migration | ~6,260 LOC harvest + ongoing ABI |
| Selection / cursor / undo coherence | Engineered at boundaries | Engineered | Native | Native (after `QTextCursor` patch) |
| Mental model going forward | N items + protocol | N items + protocol (thinner) | One widget | One widget |
| Commits now to embed fidelity | No | No | **Yes (opaque-only)** | No |

### Why "fork deeper" was ruled out

An exploratory scout of Qt's `~/src/qtbase` found:

- `QTextDocumentLayout` implementation is **4,217 LOC** with ~1,900 LOC of private-class logic embedded in the `.cpp`.
- `QTextFrame` has **zero virtual methods** — effectively sealed to external subclassing. Adding a custom frame type requires patching `QTextDocumentLayoutPrivate::createData()` and its dispatch.
- `QTextCursor` is hardcoded for `QTextTable` across ~60 references (cell navigation, `currentTable()`, `selectedTableCells()`). Custom frame types with non-trivial cursor behavior require additional harvest from the 2,594 LOC `qtextcursor.cpp`.
- Required private-header dependencies (`qtextdocument_p.h`, `qtextengine_p.h`, `qtextobject_p.h`, `qabstracttextdocumentlayout_p.h`) are ABI-unstable across Qt 6.x minor versions.
- `QTextTableData` is defined inline and private in `qtextdocumentlayout.cpp` — any table support in the fork inherits it as an undocumented internal class.
- Realistic total harvest: **~6,260 LOC**, plus ABI pinning to a specific Qt minor version with regeneration required per upgrade.

That cost buys exactly one capability the lighter options don't provide: **native interactive in-place editing of nested text content inside embeds**. One capability for 6k+ LOC of Qt-engine ownership and permanent upstream reconciliation is not a good trade.

### Why "rich objects" was the surprise candidate (and why it still lost)

`QTextObjectInterface` — the mechanism currently powering `MathTextObject` and `CheckboxTextObject` for inline math glyphs and checkboxes — has **no intrinsic size limit**. A block-sized `QTextObjectInterface` handler can paint arbitrarily large custom rectangles inline with text. This would collapse the split model entirely: one `MarkdownTextItem` contains the whole document; embeds, Mermaid, web frames, and images all become U+FFFC-backed objects. Native selection, cursor, and undo coherence for free. **No new Qt fork.**

Trade-off: objects are treated as opaque single-character units. Cursor lands before or after, never inside. That forecloses interactive in-place embed editing — a user would click an embed to navigate to the source note, not to edit the embed's contents in place.

Rejected because:

- **YAGNI in the wrong direction.** Committing to opaque embeds now would be a design choice made for architectural purity, not a user need. There is no current requirement that rules out interactive embeds; this would pre-decide that the product will never have them.
- **Significant rewrite against working code.** ~2–3k LOC of migration churn for architectural cleanliness while the existing split model is functioning.
- **The complexity argument doesn't survive a concrete-pain check.** No current pain points are attributable to the split model. Reveal complexity is bounded. Cross-boundary protocol works. Global-coordinate translation works (post-audit-top-4 fixes). User-facing behavior is indistinguishable from a native text widget.

### Why "stay" won

- **Zero upfront cost.** Existing architecture works; known issues are bugs and polish, not architectural rot.
- **Preserves flexibility.** Interactive embed editing remains possible if it becomes a product requirement.
- **Complexity is bounded and mostly paid.** The split model's integration costs (`SelectionManager`, global-coordinate translation, `cursorAtBoundary` signals) are one-time, not per-feature-compounding.
- **No reported day-to-day pain.** The author confirmed there are no current pain points attributable to the split model. It behaves as a native text widget from a user's perspective.

### Honest caveat: what "stay" does NOT get you that a native architecture would

Do not confuse "stay and polish" with "equivalent to the fork or rich-objects paths." Three things the split model will never make native, no matter how well it's polished:

1. **Selection coherence across block items.** You have a cross-boundary protocol. It works; it'll never be as clean as `QTextCursor` over one document. Every new selection-adjacent feature (multi-cursor, rectangular selection, drag-select-autoscroll edge cases) pays a boundary tax.
2. **Undo stack across boundaries.** Each `MarkdownTextItem` has its own `QTextDocument` with its own undo stack. Cross-item operations (cut across boundary, find-replace across items) need coordination. If any of that coordination is missing today, those will be edge-case bugs forever.
3. **Math reveal reentrancy.** The ~300 LOC complexity is partly structural — reveal state lives per-item. Collapsing to a single document simplifies this to per-document state. Debugging existing code won't make the per-item state disappear.

These costs are real. They're just less bad than the alternatives' costs right now.

### When to revisit this decision

Concrete triggers that should reopen the question:

1. **Interactive in-place embed editing is ruled out as a product requirement.** If the product decides embeds are always navigate-to-source, the split model's main remaining justification evaporates and **rich objects** becomes the right answer.
2. **Cross-item coherence bugs accumulate.** If undo-across-items, selection-across-items, or find-replace-across-items produce recurring edge-case regressions, the cost of maintaining the split model is actively hurting. At that point, **rich objects** becomes pragmatic.
3. **Math reveal reentrancy requires a third or fourth revision.** If the ~300 LOC subsystem gains more complexity rather than losing it under the planned per-item simplification, the per-item architectural root cause is confirmed. Lean **rich objects**.
4. **An Obsidian-style hybrid live-preview embed feature is formally specified.** If scope decides embeds need partial in-place editing (Obsidian's live-preview mode allows some in-embed interaction), the **fork** becomes the only honest answer — rich objects can't deliver this, and "stay" can only deliver it with the existing scene-item pattern (which works but is never native).

If none of those triggers fire, the split model is the right answer indefinitely.

### Pre-migration work that should happen anyway

Several items on the recommended TODO are worth doing regardless of future architectural direction, and they make an eventual migration much cheaper if it ever happens:

- `Editor.cpp` god-class extraction.
- Span-offset tracking consolidation (remove the double-redundancy between `MarkdownHighlighter::adjustSpanOffsets()` and the full reparse).
- MD4C retirement / `DocumentBuilder` removal.
- Cross-item undo audit — may surface bugs that update the "stay" trigger list.

## 4. Praise

You've avoided the two most common pitfalls in editor-widget projects:

1. You didn't build a custom layout engine before you needed to. `QTextDocument` earned its keep, and the editable-tables discovery that no layout harvest is needed is the direct payoff of that restraint.
2. You wrote specs before code and kept them in sync. The spec / plan / archive layout is mature; implemented specs get archived instead of rotting.

The GPL-harvest discipline (TextControl fork from Qt source, Kate/Qt study before hand-rolling features like folding) is philosophically coherent and will pay compounding dividends. The dated spec / plan naming convention makes the project navigable across sessions. The "Recently fixed" section in `TODO.md` is a rare and effective context-preservation technique that other solo projects should steal.
