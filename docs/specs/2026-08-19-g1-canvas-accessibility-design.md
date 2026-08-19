# markoff-canvas accessibility — G1 design

**Date:** 2026-08-19
**Status:** DRAFT — scope decided (§3), normative shape settled;
task decomposition deferred to the plan.
**Gate closed:** G1 of the canvas production arc
([`2026-08-13-canvas-production-design.md`](2026-08-13-canvas-production-design.md)
§8) — deferred 2026-08-14, reopened and answered 2026-08-19.
**Predecessor context:** production spec §5.4 (a11y as its own phase),
spike spec ([`2026-08-13-markoff-canvas-spike-design.md`](2026-08-13-markoff-canvas-spike-design.md))
§5 and §9 — "the largest unpriced item", "no accessibility work of any
kind exists in this tree".
**Plan:** not yet written. This spec is the input to it.

---

## 1. Goal and non-goals

**Goal.** Make `Markoff::Canvas::View` usable with a screen reader:
document structure, block roles, text content, caret and selection
position, and change notifications all reach the platform
accessibility bridge (AT-SPI on Linux, UIA/NSAccessibility elsewhere
via Qt's own backends).

**Why now.** Two independent reasons, only the first of which is
urgent in principle:

1. **Canvas is a regression against what it replaced.**
   `markoff-source` (`QPlainTextEdit`) and `markoff-styled`
   (`QTextEdit`) inherit a complete `QAccessibleTextInterface` from
   Qt for free. Canvas is a bare `QAbstractScrollArea` with no
   `QAccessibleInterface` at all, so a screen reader sees an unnamed,
   contentless scroll pane. G2 (2026-08-18) made canvas Corbomite's
   sole LivePreview engine, so Corbomite's *editing* surface lost the
   a11y it used to get for nothing. (Reading mode still runs on
   styled and is unaffected.)
2. **G2 closing is the seam where Markoff sets its own roadmap.**
   Per the 2026-08-19 Corbomite handoff §"pace", nothing about
   canvas's remaining roadmap needs to be dictated by Corbomite's
   dogfood queue. **User decision 2026-08-19: this arc is scoped as
   Markoff's own roadmap work, not as a Corbomite-deadline fix.**
   Reason (1) is motivation and sets the floor; it does not set the
   schedule or truncate the scope.

**Non-goals for this arc:**

- **The old leaves.** `markoff-source`, `markoff-styled`,
  `markoff-live` are untouched — they already have Qt's defaults, and
  standstill applies (live is retired per G3).
- **High-contrast theming, motion-reduction, font-size preference
  plumbing.** The e-arc framing doc §1.2 lumps these under "a11y" but
  they are theme/preference work, not assistive-technology work. Out
  of scope; log to `docs/queue.md` if wanted.
- **Keyboard-only operability.** Already delivered — canvas has a
  full key pipeline, `focusNextPrevChild`, and structural navigation.
  Gaps found here are bugs against existing contracts, not this arc.
- **i18n / `tr()` beyond existing convention.** Accessible names use
  `tr()` like everything else; no new i18n machinery.
- **A public a11y API for embedders.** The `QAccessible` factory is
  registered internally; consumers get it by using the widget. No new
  header in `include/markoff/canvas/`.

## 2. The constitutional problem, and why it decides the shape

`QAccessibleTextInterface` is defined over **a single flat character
offset space**: `characterCount()`, `text(start, end)`,
`cursorPosition()`, `selection(index, &start, &end)` are all
whole-document `int`s.

That is exactly what **C4 forbids** — "one document coordinate space:
per-block UTF-8 byte offsets. No `applyFlatEdit`, no `flatView()`, no
cross-block byte sums."

So the §8 framing of G1 ("full `QAccessibleTextInterface` vs. basic
role/name") was not a sizing question. Implementing the interface
monolithically on `View` would require a second whole-document index
space and a flat↔block map — a **constitutional amendment**, not a
task. There is precedent for granting one (`ProjectionMap` is a
sanctioned second index space precisely because it is confined to one
file and never crosses the layout boundary), but it would have to be
granted deliberately, with the same care.

**The per-block accessibility tree avoids the question entirely**, and
that is the decisive reason to prefer it — not merely its ergonomics.

## 3. Decision (user, 2026-08-19)

| Question | Decision |
|---|---|
| **Shape** | **Per-block accessibility tree.** `View` exposes itself as a container; each block is a child accessible object implementing `QAccessibleTextInterface` over *its own* buffer. |
| **Acceptance** | **In-process offscreen tests + one manual Orca pass at arc close.** The ratchet rides on `QAccessible::queryAccessibleInterface` unit tests; a single `--direct` Orca session validates the end-to-end bridge, with per-task user permission per the repo rule. |
| **Driver** | **Markoff's own roadmap.** No Corbomite deadline shapes the phase order. |

**Consequences of the shape decision:**

- **C4 is untouched.** Every offset an a11y object handles is a
  per-block offset in that block's own buffer. No cross-block sums,
  no flat view, no amendment, no carve-out. `tests/check-constitution.sh`
  keeps passing unmodified.
- **Realization cost is bounded per block** (§5).
- **It matches how AT clients already consume documents.** Browsers
  expose exactly this shape; Orca and NVDA navigate block-child trees
  natively.
- **Cost:** it is the largest of the three options in code volume.
  Roughly a new `src/Accessibility.{h,cpp}` with two classes plus an
  event-notification seam in `View`, against the ~50 lines a
  role/name-only stub would have been.

## 4. Architecture

### 4.1 Objects

Two accessible classes, both new, both private to the leaf
(`src/Accessibility.h` / `.cpp` — no public header):

**`CanvasAccessible : QAccessibleWidget`** — wraps `View`.

- Role: `QAccessible::Document`.
- `state().editable` tracks `View::isReadOnly()`; `focusable`,
  `focused` from the widget.
- `childCount()` → `View::blockCount()` (the *document* block count,
  not `realizedBlockCount()` — the a11y tree is the document, not the
  viewport).
- `child(i)` / `indexOfChild()` → a `CanvasBlockAccessible` for
  `View::blockIdAt(i)`.
- `childAt(x, y)` → hit-test via existing block geometry.
- `text(QAccessible::Name)` → `View::inlineTitle()` when set,
  otherwise a `tr()`'d generic ("Markdown document").
- Implements `QAccessibleSelectionInterface`? **No** — selection is
  text selection, not child selection; it belongs on the block
  objects.

**`CanvasBlockAccessible : QAccessibleInterface, QAccessibleTextInterface`**
— one per `BlockId`.

- Role mapped from `BlockKind` (§4.2).
- `text(start, end)` etc. operate on `document()->blockText(id)`,
  converted byte↔QChar with the existing `coords::` helpers
  (`<markoff/core/TextUnits.h>`). **This is the same conversion the
  leaf already does everywhere** — nothing new is invented.
- `cursorPosition()` → `View::caretByteOffset()` converted, but only
  when `View::caretBlock() == id`; otherwise the block reports no
  caret.
- `selection()` → the intersection of the view's selection range with
  *this* block. Cross-block selections therefore appear as a
  selection on each spanned block, which is the correct and
  conventional presentation for a block tree.
- `rect()` → `View::blockRect(id)` mapped to global coords.
- `characterRect(offset)` / `offsetAtPoint(point)` → require layout;
  see §5.

### 4.2 `BlockKind` → `QAccessible::Role`

`BlockKind` (core, 11 values) maps as:

| `BlockKind` | Role | Notes |
|---|---|---|
| `Paragraph` | `Paragraph` | |
| `Heading` | `Heading` | level from the block's `Level` attr → `QAccessible::Level`-equivalent via attributes |
| `CodeBlock` | `EditableText` | no dedicated code role in Qt; language goes in the description |
| `ListItem` | `ListItem` | checked state from the `Checked` attr → `state().checkable`/`checked` |
| `BlockQuote` | `Section` | |
| `HorizontalRule` | `Separator` | no text interface |
| `Image` | `Graphic` | name from `View::mediaLabelFor(id)`; no text interface |
| `Math` | `StaticText` | name is the source; rendered glyph is not readable |
| `Mermaid` | `Graphic` | name from `mediaLabelFor(id)` |
| `HtmlBlock` | `EditableText` | raw source is what the user edits |
| `Table` | `Table` | **see §6 — deferred** |

Blocks with no text interface (`HorizontalRule`, `Image`, `Mermaid`)
implement `QAccessibleInterface` only and return `nullptr` from
`interface_cast` for the text interface. That is legal and expected.

### 4.3 Folding and hidden blocks

`View` already models folding (`isBlockFolded`, `isBlockHidden`).
Hidden blocks must report `state().invisible = true` **and stay in the
child list** — removing them would make child indices unstable across
a fold toggle, which breaks AT clients holding references. A folded
head block additionally gets `state().expandable = true` and
`expanded` reflecting `isBlockFolded()`, and exposes
`QAccessibleActionInterface` with an expand/collapse action wired to
`View::toggleFold(id)`.

### 4.4 Notifications

`QAccessible::updateAccessibility()` calls are the part that is easy
to get wrong by omission. The seam is `View`'s existing signal/update
points — **no new deferral, no `singleShot`** (C2):

| Trigger | Event |
|---|---|
| caret moves (`caretChanged`) | `QAccessibleTextCursorEvent` |
| selection changes | `QAccessibleTextSelectionEvent` |
| block text edited | `QAccessibleTextInsertEvent` / `RemoveEvent` on that block |
| block inserted/removed | `QAccessibleEvent(ObjectCreated / ObjectDestroyed)` |
| focus in/out | `QAccessibleEvent(Focus)` |
| fold toggled | `QAccessibleStateChangeEvent` (expanded) |
| read-only flipped | `QAccessibleStateChangeEvent` (editable) |

Emitting these unconditionally is cheap when no AT client is attached
— `QAccessible::isActive()` short-circuits — so there is no
"only when a screen reader is running" conditional to maintain.

### 4.5 Registration

A `QAccessible::InstallFactory` at leaf init, guarded so repeated
`View` construction does not re-register. Object lifetime for the
per-block children follows Qt's `QAccessibleCache` convention: block
accessibles are created on demand and keyed by `BlockId`, and
`QAccessible::deleteAccessibleInterface` is called when a block is
removed from the document.

## 5. Realization and cost

Canvas realizes only blocks near the viewport
(`realizedBlockCount()` ≪ `blockCount()`). This splits the interface
cleanly:

- **Text content, character count, caret offset, selection range,
  roles, states** come from `MarkoffDocument` (`blockText`,
  `iterateBlocks`, attrs). **No layout needed** — a screen reader can
  walk the entire document without realizing a single extra block.
  This is the common case and it is free.
- **Geometry and line granularity** — `characterRect()`,
  `offsetAtPoint()`, and `textAtOffset(..., LineBoundary)` — need a
  `QTextLayout`. These force realization of the queried block.

**The bound:** an AT client asking for geometry realizes *one block at
a time*, which is exactly the cost of scrolling that block into view
and is already budgeted. There is no path where an a11y query
realizes the whole document. Contrast: a flat `characterCount()` over
a monolithic interface would have needed every block's length up
front, and `textAtOffset(LineBoundary)` over flat offsets has no
natural per-block bound at all. **This containment is a second
independent argument for the tree shape**, and the perf phase should
assert it (a test that walks all blocks' text and asserts
`realizedBlockCount()` is unchanged).

E9's four perf budgets are unaffected — a11y adds no per-keystroke
work beyond an `updateAccessibility()` call that no-ops when inactive.

## 6. Tables — explicitly deferred within this arc

`QAccessibleTableInterface` over canvas's table blocks is a genuinely
separate problem: canvas models a table as a *2-D grid inside a 1-D
block sequence* (spike §9 calls this out as one of the hard cases),
and the cell-level accessible objects would be a third class with
their own coordinate story.

**Decision:** tables get `QAccessible::Table` as their role and a
usable accessible name/description this arc, but **not**
`QAccessibleTableInterface`. A screen reader will announce "table" and
read its text content linearly rather than navigating cells.

This is a stated, logged limitation — not an oversight — on the same
footing as the spike's own deferrals. If it is later wanted, the
inputs already exist (`View::tableCellRect`, `caretTableCell`,
`caretTableContext`), so it is additive.

## 7. Testing

New `tests/tst_canvas_accessibility.cpp`, offscreen, in the standard
suite. All of it runs in-process via
`QAccessible::queryAccessibleInterface` — **no AT-SPI bridge, no
display, no `--direct`**, so it joins the ratchet like any other test:

- Tree shape: `childCount()` tracks `blockCount()` across insert /
  remove / fold.
- Role mapping: one case per `BlockKind` row in §4.2.
- Text interface: `text()`, `characterCount()`, `cursorPosition()`,
  `selection()` against known fixtures, including multi-byte UTF-8
  (the byte↔QChar conversion is the obvious break point).
- Cross-block selection presents as per-block selections.
- Folding: hidden blocks stay in the child list, report `invisible`,
  and the expand action toggles.
- Notifications: a `QAccessible::installUpdateHandler` spy asserts the
  §4.4 event table fires on each trigger.
- Realization bound (§5): walking every block's text leaves
  `realizedBlockCount()` unchanged.

**Baseline: 315/315 today.** Every task raises it, never lowers it.

**Manual verification (once, at arc close):** an Orca session over the
demo app, driven by the user, confirming that document navigation,
block-by-block reading, caret announcement, and editing echo actually
work through the real bridge. This needs `--direct` and therefore
explicit per-task permission at the time. It is acceptance, not a
ratchet — it does not gate individual tasks.

## 8. Standstill after this spec lands

- `libs/markoff-canvas/` — active workfront (already open; the
  production arc's plan-named-seam restriction died with that arc).
- `libs/markoff-core/` — **expected to need nothing.** Everything
  §4 needs (`blockText`, `iterateBlocks`, `BlockKind`, attrs,
  `coords::`) already exists and is public. Any core change that
  surfaces is a **finding to report**, not a fix to make inline.
- `markoff-styled` — bug-fix only (unchanged).
- `markoff-source` — untouched, permanent (unchanged).
- `markoff-live` — retired (G3, 2026-08-19).

## 9. Open questions for the plan

These do not block the plan being written; they are the first things
it should pin down.

1. **Block-accessible lifetime under CRDT churn.** Remote edits can
   delete a block while an AT client holds its interface. Qt's cache
   handles the pointer, but the eviction *trigger* needs picking —
   most likely `View::onDocumentChanged` diffing the id list.
2. **Accessible name for untitled documents.** `inlineTitle()` is
   optional and Corbomite may not set it. Fall back to the file name?
   Canvas does not know one. Probably a generic `tr()` string plus a
   settable property — but adding a property contradicts §1's "no new
   public API", so this needs a call.
3. **Heading level exposure.** Qt has no first-class heading-level
   accessor on `QAccessibleInterface`; it goes through
   `QAccessibleAttributesInterface` (Qt 6.8+) or the role hierarchy.
   Confirm what Orca actually reads before picking. (Local Qt is
   6.11.1, so the attributes interface is available.)
4. **Phase count.** Provisionally: A1 tree + roles + registration,
   A2 text interface, A3 notifications, A4 folding + actions,
   A5 realization-bound test + Orca pass. The plan decides.

## 10. Gates

| Gate | When | Question |
|---|---|---|
| A-G1 | before A5 | run the Orca pass now, or defer it to a later dogfood session? (needs `--direct` permission either way) |

No other user gate is anticipated. Tables (§6) and the theme-side
a11y items (§1 non-goals) are decided-out, not gated.
