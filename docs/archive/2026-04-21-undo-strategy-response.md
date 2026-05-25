# Re: Q2 — cross-leaf undo strategy

**Picking B, and pushing back on the framing.**

Before the vote, one load-bearing piece of your Option A needs to be disambiguated, because as stated it collapses to two very different proposals with very different costs.

You wrote:

> Each block is a QTextCursor-based slice (or a QTextFrame/custom object format) over the canonical QTextDocument.

`QTextCursor` is a position/range into a `QTextDocument` — not a view. Qt renders a `QTextDocument` through exactly one `QAbstractTextDocumentLayout` into exactly one surface. `QTextFrame` is a layout region *within* a document; it does not carve a document into independently-rendered sub-widgets. There is no mechanism in Qt 6.8 for N `QGraphicsItem`s to each display a different slice of one shared `QTextDocument`. So A, taken literally, is one of:

- **A(i) — retire the scene-graph.** `Markoff::Live::Editor` becomes a single `QTextEdit` (or custom `QAbstractScrollArea` over one `QTextDocument`). All blocks live in that one doc; folding via `QTextBlock::setVisible`; tables via `QTextTable`; math/checkboxes via `QTextObjectInterface`; images via `QTextObjectInterface` renders. This is a **full rewrite of Markoff::Live**, not "a meaningful refactor."
- **A(ii) — keep the scene-graph, project slices.** Requires writing a custom `QAbstractTextDocumentLayout` from scratch that dispatches paint/hit-test into N `QGraphicsItem`s. That is Calligra-scale — multi-person-year. Not C3 scope; not C3 + two alphas scope.

You almost certainly mean A(i). Please name it that way so the cost conversation happens on real numbers.

## The real cost of A(i), beyond the pitch's disclosure

1. **Folding + rich-text is a documented Qt cliff.** `QTextEdit`'s `QTextDocumentLayout` is private (`qtextdocumentlayout_p.h`). Folding across rich content requires either subclassing a private class, a bespoke `QAbstractTextDocumentLayout`, or dropping to `QPlainTextDocumentLayout` — which loses `QTextTable`. Our feature set has all of {folding, tables, math, images, per-block decorations}. Qt's beaten paths support any three of those together; the fourth pushes you into custom-layout territory. Qt Creator has folding because it's plain-text. Marknote has tables because it has no folding. Calligra has both because they wrote their own `KoTextDocumentLayout` — a multi-year, multi-developer effort. We would be the first Qt markdown editor to ship all four in one `QTextDocument`.
2. **Per-block decorations** (`DecoratedRange`: code-block backgrounds, callout borders, HR separators) move from `MarkdownTextItem::paintDecoratedRanges` into the single viewport's `paintEvent`, walking blocks via `blockBoundingRect`. Doable, not free.
3. **Viewport-driven image rescaling** is cheap today because `ImageBlockItem` is a `QGraphicsItem` with direct access to viewport geometry. As a `QTextObjectInterface`, `intrinsicSize()` runs on layout passes; you don't get cheap notification of viewport resize.
4. **Per-block parse debouncing stops working.** `SceneCoordinator`'s 150ms item-level reparse is per-item today. With one doc, every `contentsChange` drives a reparse strategy that has to rediscover the affected range. New infrastructure.
5. **Selection/cursor cross-block becomes free.** Native `QTextCursor` replaces the entire `SelectionManager` state machine. Drag-select across "blocks", `Shift+Arrow` at boundaries, mime serialisation for copy-paste — all native. This is the one real architectural win A(i) hands us.
6. **`TableConverter` / `TableSerializer` port cleanly.** They already operate on `QTextTable` inside a single `QTextDocument`. That subsystem survives intact.

Net: A(i) is the scope of "rewrite Live from v0.1.0." It's not a C3 work-unit; it's its own Phase with its own spec + plan docs. `v0.6.0-alpha.1` is optimistic — this is closer to a `v0.7` with a multi-alpha runway.

## On B, which the pitch is unfair to

You framed B as "two undo stacks fighting" → "worst of both worlds." That's B implemented badly. B done properly is one stack, full stop:

- `MarkoffDocument` owns one `QUndoStack`. Exposed as `MarkoffDocument::undoStack()`.
- Every leaf's internal `QTextDocument` has `setUndoRedoEnabled(false)`. Qt's native per-doc undo is disabled everywhere. There is no second stack to fight.
- All edits — Source, Live, Reading-that-shouldn't-edit — route through cursor ops that push a single command type onto the shared stack:

  ```cpp
  class MarkdownDelta : public QUndoCommand {
      qsizetype offset;
      QString   removed;
      QString   inserted;
  };
  ```

- Structural ops (table row/column insert, heading level change across a selection) are composites that `push()` a sequence of `MarkdownDelta`s inside a macro.
- On undo/redo, the command rewrites canonical. Leaves subscribe via canonical's `contentsChange` and re-project into their view representation — for Live, that's its scene graph; for Source, that's Qutepart's buffer; for Reading, that's the rendered AST tree.

**What B pays, honestly:**

- Per-leaf offset ↔ canonical-offset translation table. **The exact Phase-A-deferred work A also has to build** — A just renames "translation" to "projection." Your pitch implies B pays this cost *on top of* A's cost; it's the same cost.
- Three or four `QUndoCommand` subclasses. Reasonable work, not scope-explosion.
- Leaves treat their internal `QTextDocument`s as scratch view state: regenerated from canonical on change, not authoritative copies that can desync.

**What B keeps:**

- Every subsystem in `libs/markoff-live`: interactive tables with native cell cursors, `MathTextObject` click-to-edit, `CheckboxTextObject` toggles, `DecoratedRange`, scene-level folding with fold gutter, viewport-driven image rescale, per-block parse debouncing, `SelectionManager` (or we take the selection-free-lunch later, independently).
- The years of "piece it together in the scene graph" work that `libs/markoff-live/docs/archive/07-atomic-blocks-and-tables.md` documents as load-bearing for the Obsidian UX we're targeting. Going to A(i) throws that away and re-incurs it against Qt's single-`QTextDocument` constraints — where, per the Qt survey, *no Qt-based editor of note has shipped all of our features simultaneously.*

## Your objection that B "defeats the point of C3"

C3's promise is "three leaves subscribe to shared content via signals." Note that Source already subscribes at the *markdown-text* level — Qutepart has its own buffer; it does not share a `QTextDocument` object with anyone. Reading renders from a parsed AST; it does not share a `QTextDocument` either. Forcing Live — and only Live — to share at the `QTextDocument` *object* level is not symmetric subscription. It's a privileged coupling for Live that the other two leaves neither need nor provide.

The symmetric model is: **canonical is markdown bytes; all three leaves subscribe to markdown-byte changes; each leaf projects canonical into its native representation; undo lives at the canonical-markdown level.** That's B. That's also what actually matches how Source and Reading already work.

## Vote: B

Reserve A(i) as a separately-scoped future phase with its own spec and plan docs. If we later decide Markoff::Live's scene-graph is more cost than value, A(i) on top of B is a clean internal refactor — the canonical buffer and shared undo stack are already there, and A(i) becomes purely Markoff::Live-internal with zero cross-leaf contract churn. Doing A(i) *as* C3 front-loads the hardest engineering problem in the library and commits us to a Qt path that no comparable editor has trod.

## Two specific asks before you redraft

1. **Rename and restate.** If it's A(i) you want, say "retire the scene-graph and rebuild Live on a single `QTextEdit`." If it's A(ii), explain how you get N independently-rendered `QGraphicsItem`s from one `QTextDocument` without a custom `QAbstractTextDocumentLayout`.
2. **Restate B as** "unified `QUndoStack` on `MarkoffDocument`, leaves push markdown-delta commands, native Qt undo disabled leaf-side." The "dual stack" characterisation is a straw man of B.

If after that you still want A(i), that's a legitimate disagreement about scope appetite and we can have it with honest numbers. But B is not C3-in-name-only, and A is not a safely-sized refactor. Those are the two mis-framings I'd like cleared before the call gets made.
