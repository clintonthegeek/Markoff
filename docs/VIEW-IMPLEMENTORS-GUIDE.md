# Markoff View Implementor's Guide

> **Status: evergreen reference.** Peer to [`INVARIANTS.md`](INVARIANTS.md).
> Required reading before building a new Markoff view leaf, or before
> hardening an existing one against the edit/cursor seam. Updated as the
> view leaves evolve — when a concern's status changes, edit the status line
> in place. Last substantive revision: 2026-05-27.

## Why this document exists

Markoff has, as of 2026-05-27, three view leaves on the shared
`markoff-core` foundation: `markoff-live` (QML, per-block delegates),
`markoff-source` (QPlainTextEdit, flat text), and `markoff-styled`
(QTextEdit, flat text with inline styling). Every one of them has had to
solve the *same set of cross-cutting problems* — caret placement after a
structural edit, surviving a model rebuild, attributing an edit to the
right block at a boundary, keeping the viewport still. `markoff-live`
solved them first, over many months and three post-mortems. `markoff-styled`
then **re-discovered the same bugs one at a time** during dogfood, because
nothing wrote down that these were solved problems with known shapes.

This guide is that write-down. It is organised as a **catalog of concerns
with contracts**: each concern states the problem, why it bites *every*
view regardless of toolkit, the contract any view must satisfy, and how the
existing leaves satisfy it (or, honestly, where one does not yet). A new
view author works down the catalog. An author hardening an existing view
finds their bug by its number.

It does **not** restate the engineering *discipline* — that is
[`INVARIANTS.md`](INVARIANTS.md)'s job (the eight rules for working in the
seam: cite the developmental record, decide L4 in writing, retire the old
authority in the same plan, falsifiable tests first, etc.). The guide is
the *what and why of the problems*; INVARIANTS is *how you must work while
solving them*. Read both.

### Authoritative companions (cited throughout, not duplicated)

- [`INVARIANTS.md`](INVARIANTS.md) — the eight discipline rules for the
  focus/caret/block-change seam.
- [`specs/2026-05-22-cursor-authority-decision.md`](specs/2026-05-22-cursor-authority-decision.md)
  — the authoritative L3 cursor-authority decision (chokepoint, within-block
  sync contract, anchor preservation). The cursor chapter (§B) summarises and
  points here; this spec is the source of truth.
- [`handoff/2026-05-07-live-binding-developmental-history.md`](handoff/2026-05-07-live-binding-developmental-history.md)
  — *why the live-binding pipeline looks the way it does.* Per-concern
  citations below point into it.
- [`specs/2026-05-27-markoff-core-binding-robustness-design.md`](specs/2026-05-27-markoff-core-binding-robustness-design.md)
  — the single-document binding's forward/reverse path design (the RT1–RT6
  work that closed §A for styled/source).
- [`specs/2026-05-18-b1-buffer-convention-design.md`](specs/2026-05-18-b1-buffer-convention-design.md)
  — the block-buffer convention §1 leans on.

---

## §1. The bimodal foundation (context, not a concern)

Every concern below hangs off this frame. Understand it first.

**One CRDT truth, two view ingresses.** `MarkoffDocument` holds the
document as **per-block CRDT buffers** plus an `IdList` for block order and
sibling causal-LWW maps for kind/attrs/links/footnotes/frontmatter (the "D2"
shape). There are exactly two ways a view feeds edits into that truth:

| Ingress | API | Used by | Coordinate space |
|---------|-----|---------|------------------|
| **Per-block** | `d2ApplyBufferEdit(blockId, byteOffset, removeBytes, insertedUtf8, txn)` | `markoff-live` | per-block byte offset |
| **Single-document** | `applyFlatEdit(startByte, endByte, newText, origin)` via `SourceTextDocumentBinding` | `markoff-source`, `markoff-styled` | global byte offset, **no-separator** |

A view never touches the CRDT primitives directly. It either knows its block
structure (live) and edits a block, or it sees flat text (source/styled) and
lets the binding decompose the edit.

**Three coordinate spaces — keep them straight.** This is the single most
common source of off-by-a-block bugs:

1. **Per-block byte offset** — offset within one block's buffer. Block
   buffers hold *content only*, no trailing `\n` (the **B1 buffer
   convention**; see the b1 spec). Separators are the serializer's job.
2. **No-separator global byte offset** — blocks concatenated with *nothing*
   between them. This is the space `applyFlatEdit` consumes and the space
   `resolveTextAnchor` returns. In it, block N's end byte *equals* block
   N+1's start byte — the boundary is ambiguous, which is why §A.1 exists.
3. **Separator-view ("sep-view") global offset** — blocks joined by `"\n\n"`
   (`interBlockSeparator()`), terminated by a single `"\n"`
   (`finalDocumentTerminator()`). This is what `flatView()` produces (the
   canonical save/parse form). At runtime the flat-text widget views
   (`markoff-styled`, `markoff-source`) instead hold `widgetFlatView()`
   content — single-`\n` between blocks plus `QTextBlockFormat` paragraph
   margins for the visible gap — see §0.2. The mapping between sep-view and
   no-sep is **not** a constant shift; it depends on how many block boundaries
   you have crossed.

`Markoff::Detail::findBlockAtSepByte(doc, sepOff, biasForward)` and
`sliceByBlocks` (in `include/markoff/core/Detail/FlatBlockResolve.h`) are the
shared helpers that map sep-view offsets to `(blockId, byteInBlock,
blockIndex)`. Both the binding and `markoff-source` use them. Use them; do
not hand-roll the walk.

**The canonical-structure invariant, and which ingress enforces it.** After
any `applyFlatEdit`: no internal `\n` in any block buffer, no unintended
empty blocks, separators are exactly single `\n\n`. This is enforced
**only** on the `applyFlatEdit` ingress (its canonicalisation pass splits
inserted text on newline-runs, collapsing runs, never creating empty
blocks). It is deliberately **not** enforced on the per-block path, so
live's intentional empty-paragraph blocks survive. A flat view gets
CommonMark-style blank-line collapsing for free; a per-block view manages
its own emptiness.

**Load-side enforcement (2026-05-29).** `loadFromMarkdown` collapses
internal `\n` → space inside `Paragraph`, `ListItem`, and setext
`Heading` block buffers at the load ingress, so hard-wrapped
markdown content becomes a single canonical buffer per kind. Without
this, every hard-wrap `\n` reaches QTextDocument in flat-view leaves
and creates a spurious QTextBlock boundary inside what should be one
block. Setext headings additionally have their underline line stripped
before the collapse; `serializeHeading` reconstructs the underline
from `(content.size(), level)` on save, and `serializeForSave` bypasses
its untouched-block fast path for setext so reconstruction always runs.
`BlockQuote` still retains its internal `\n`s because the byte range
includes per-line `> ` markers that need marker-aware stripping;
flat-view leaves will still see spurious boundaries inside BlockQuotes
until that's done. Trade-offs accepted: paragraph hard-wraps are not
preserved across save round-trips (matches Obsidian / browser markdown
rendering); setext underline width drifts toward title length on load+save
(CommonMark accepts ≥1, still parses as the same setext heading).

---

## §0.2 — Paragraph delineation is word-processor everywhere

Markoff treats paragraphs as **first-class structural objects**, not as runs
of bytes in a flat string. `markoff-live` lays out per-block QML delegates
with margins; `markoff-styled` and `markoff-source` consume
`MarkoffDocument::widgetFlatView()` (single-`\n` separator between
QTextBlocks) and apply `QTextBlockFormat::topMargin/bottomMargin` to produce
the visible inter-paragraph gap. Pressing Enter creates a *new model block*
(possibly empty, transient) via `applyInteractiveNewline`. The cursor cannot
land "in the gap" because the gap is layout space, not a byte position.

`markoff-source` is a *visual* sibling of `markoff-styled` — distinguished
only by not rendering inline markdown markers (`**`, `_`, `==`, etc. stay
visible as characters). At the structural level (blocks, Enter, backspace,
caret) it is identical to styled.

Authoritative spec:
`specs/2026-05-28-flat-view-wp-unification-design.md`.

---

## §A. Text-sync correctness

The bytes the view shows and the bytes the model holds must agree, in both
directions, without the view's formatting or the user's caret being
collateral damage. For the flat-text leaves this is the
`SourceTextDocumentBinding` forward/reverse path; for live it is
`LiveEditBinding` + the diff-driven model. **This group is SOLVED for all
three leaves** as of the 2026-05-27 binding-robustness work — it is
documented here so the next view does not re-derive it.

### A.1 — Forward edit attributed to the correct block at a boundary

**Problem.** A flat view sees a single offset. An edit *at a block boundary*
(end of block N == start of block N+1 in no-sep space) is ambiguous: which
block gets the inserted character? Get it wrong and a space typed at the end
of a heading lands at the start of the next paragraph, and the reverse sync
then "corrects" the view by leaping the caret.

**Contract.** Resolve the edit offset to a block with an explicit bias rule
(`biasForward = false`: an edit *at* a boundary belongs to the block on the
left, i.e. the one the user was just in). Dispatch single-block
structure-neutral edits straight to `d2ApplyBufferEdit` so the boundary
attribution is decided once, in the binding, not re-inferred by a round-trip.

**Live.** N/A by construction — live always knows which block's delegate
received the keystroke; there is no flat offset to disambiguate.

**Styled / source.** Solved (RT1). `onQtContentsChange` resolves via
`findBlockAtSepByte(…, biasForward=false)` and dispatches three-way:
single-block → `d2ApplyBufferEdit`; cross-block non-structural → direct D2
merge primitives; structural → `applyFlatEdit`. Regression guard:
`tst_styled_dogfood_invariants::typing_at_boundary_does_not_wipe_or_leap`.

**Citations.** Binding-robustness spec §forward-path;
`SourceTextDocumentBinding::onQtContentsChange`.

### A.2 — Reverse sync preserves formatting (no `setPlainText` wipe)

**Problem.** When the model changes (from an undo, a collaborator, a
kind-transition, or the canonicalisation of one's own edit), the view must
update its text. The naïve `setPlainText(serializeForSave())` wipes every
`QTextCharFormat` the styler applied, drops the caret to position 0, and
resets the scroll. Styled hit exactly this: *"the style from the entire
document except the bottom line disappears when I type something."*

**Contract.** Push model→view changes as an **incremental** edit: compute a
common-prefix / common-suffix text-diff and mutate only the differing middle
through a `QTextCursor`. Clamp diff boundaries off UTF-16 surrogate pairs.
Reserve `setPlainText` for initial load, when there is nothing to preserve.

**Live.** N/A in this form — live's diff-driven model (`L2`, Myers over
`(kind, BlockId)`) emits `dataChanged`/`rowsInserted`/`rowsRemoved` and only
the affected delegates re-render. The "don't blow away the whole view"
property is structural, not a diff the view computes.

**Styled / source.** Solved (RT5). `onD2DocumentChanged` does the
prefix/suffix diff via `QTextCursor::insertText`; only initial load
(`syncQtDocumentFromMarkoff`) calls `setPlainText`. **Caveat that leads
directly into §B:** the incremental `insertText` *rides the caret with it*.
That is correct for preserving an unrelated caret but is exactly why a
structural edit leaves the caret in the wrong place — see §B.1.

**Citations.** Binding-robustness spec §reverse-path;
`SourceTextDocumentBinding::onD2DocumentChanged`.

### A.3 — Canonical structure maintained on edit

**Problem.** A flat view can trivially produce illegal structure: type two
blank lines and you have an empty block; paste text with `\n` in it and a
block buffer now contains a structural newline it should not. If the view
and model disagree on what is legal, they fight.

**Contract.** The `applyFlatEdit` ingress canonicalises (see §1). A view
author does *not* re-implement blank-line collapsing; they route structural
insertions through `applyFlatEdit` and trust the invariant. A per-block view
opts out and owns its emptiness.

**Live / styled / source.** Solved — see §1. Live keeps intentional empties;
flat views get collapsing.

**Citations.** §1; `applyFlatEdit` canonicalisation pass
(`MarkoffDocument.cpp`, `splitOnNewlineRuns`).

### A.4 — Cross-block delete merges blocks correctly

**Problem.** Selecting from the middle of block N through the middle of block
N+1 and pressing Delete must *merge* the two blocks. In no-sep coordinates
the separator the user "deleted" is zero-width, so routing this through
`applyFlatEdit` would collapse both endpoints onto the same byte and lose the
merge.

**Contract.** Detect cross-block non-structural deletes in the forward path
and apply direct D2 merge primitives rather than a flat edit.

**Live.** Handled in `LiveStructuralKeyHandler` (Backspace-at-block-start /
Delete-at-block-end merge paths; `establishFocus(result.mergedInto,
joinQtPos)`).

**Styled / source.** Solved (RT3) — the forced deviation from "everything
goes through applyFlatEdit," justified in the binding-robustness spec.
Cross-block-selection-delete test added.

**Citations.** Binding-robustness spec §RT3;
`LiveStructuralKeyHandler.cpp` merge cases.

---

## §B. Cursor authority — the heart of this guide

> **This is the one group the single-document model does not get for free,
> and the one a new flat-text view will rediscover first.** Read
> [`specs/2026-05-22-cursor-authority-decision.md`](specs/2026-05-22-cursor-authority-decision.md)
> — it is authoritative; this section is the orientation around it.

**The governing contract, stated once:**

> After any model-driven change, the view must **resolve a model anchor to a
> view caret, and re-assert that caret once the model has settled.** Never
> let a text-mutation API (`QTextCursor::insertText`, a model `dataChanged`,
> a delegate rebuild) leave the caret wherever it happened to land.

The caret is *not* an integer position into a flat string. It is a
**`Markoff::TextAnchor`** — a CRDT byte anchor `(replicaId, charValue, bias)`
— or a `BlockAnchor` + offset. Integer positions are invalidated by any edit
that shifts bytes; anchors survive because they name a *character*, not a
*coordinate*. The view holds the intent as an anchor and converts to a view
caret only at the moment of display.

This is why cursor/selection lives on the **`Session`**
(`Session::primarySelection()`) as anchor-typed `Selection {anchor, active}`
— it is view-agnostic by construction and portable across views of the same
document.

### B.1 — Caret re-assertion after a structural edit

**Problem.** User presses Enter at the end of a paragraph. On the flat-text
leaves this surfaced as two coupled failures (reproduced 2026-05-27, spec
`specs/2026-05-27-flat-view-enter-and-caret-authority-design.md`): (1) the
structural edit was *dropped* — `applyFlatEdit`'s cursor-edit start-of-next-
block bias + empty-head suppression + the no-empty-block invariant made a lone
boundary `\n` a no-op, so no paragraph was created; and (2) the caret drifted
into the inter-block gap, reading as "the caret jumped into the next
paragraph." The fix is therefore *both* a forward-path change (an interactive
ingress, `applyInteractiveNewline`, that creates a real — possibly transient
empty — block) and the caret re-assertion below.

**Contract.** The structural operation declares the *intended* post-edit
caret as an anchor *before* it mutates the model. After the model settles
and the reverse sync runs, the view re-resolves that anchor to a view caret
and sets it — overriding wherever the text mutation left it.

**Live.** This is precisely what `LiveCursorState::establishFocus(BlockAnchor,
qtPos)` is for. Every structural case in `LiveStructuralKeyHandler` ends with
an `establishFocus` call naming the intended landing block + offset (e.g.
paragraph-split → `establishFocus(BlockAnchor(newBlock), 0)`). The request is
*pending* until the target delegate registers (`delegateAvailable`, at the
end of the `onD2Changed` cascade via `endStructuralCascade` →
`tryResolvePending`). The chokepoint is the *single* place structural caret
placement happens — see invariant discipline in
[`specs/2026-05-22-cursor-authority-decision.md`](specs/2026-05-22-cursor-authority-decision.md).

**Styled / source.** ✅ Solved (2026-05-27). Bare Enter routes through
`SourceTextDocumentBinding::onQtContentsChange` → `applyInteractiveNewline`,
which creates the paragraph and returns the caret's target block. The binding
stages `m_pendingCaret{BlockId, offsetInBlock}` and, at the tail of
`onD2DocumentChanged` (after the reverse diff settles — no `singleShot`,
the signal is already debounced), resolves it to a sep-view position and emits
`caretResolved(start, active)`. Each Editor connects that to `setTextCursor`.
This is the single-document analogue of `LiveCursorState` as the chokepoint.
Under WP unification (2026-05-28) the *rendering* of the structural edit
changed from "literal `\n\n` per boundary" to "single `\n` + margins-driven
gap"; the caret-authority machinery is unchanged.

**Citations.** `LiveCursorState::establishFocus` /
`LiveStructuralKeyHandler.cpp`; `SourceTextDocumentBinding::syncFromSession`;
cursor-authority-decision spec.

### B.2 — Caret survives a model rebuild

**Problem.** Any model change can re-emit the whole view's content (or a
delegate can be destroyed and recreated on a kind-transition). An
integer-positioned caret is now meaningless.

**Contract.** Hold the caret as an anchor on the `Session`; re-resolve after
the rebuild. The anchor names the same character even though its integer
position moved.

**Live.** `LiveCursorState` holds a discriminated cursor
(`TextCaret | BlockSelected | BlockInternalEdit`) keyed by `BlockAnchor`; on
kind-transition `delegateAvailable` re-stages the pending focus so the new
delegate inherits the caret (`LiveCursorState.cpp:425`).

**Styled / source.** 🟡 Mechanism wired. `syncFromSession` now resolves an
externally-driven `Session::primarySelection()` to a sep-view caret and emits
`caretResolved`, so a collaborator-driven caret update lands correctly. Full
parity awaits the local caret being pushed *to* the Session (not wired today).

**Citations.** cursor-authority-decision spec §anchor-preservation;
`LiveCursorState` cursor variants.

### B.3 — Multi-block selection-delete leaves the caret correct

**Problem.** Delete a selection spanning three blocks. The blocks merge
(§A.4); the caret must land at the merge point, not at end-of-document or
position 0.

**Contract.** The merge primitive returns the merged-into block and the join
offset; the view `establishFocus`-es there.

**Live.** `establishFocus(result.mergedInto, joinQtPos)` in the
Backspace/Delete merge cases.

**Styled / source.** ✅ Solved (2026-05-27). The cross-block merge path stages
`m_pendingCaret{mergedInto, joinOffset}`; the chokepoint (B.1) delivers it.

**Citations.** §A.4; `LiveStructuralKeyHandler.cpp` merge cases.

### B.4 — Undo/redo restores the caret

**Problem.** Undo should restore not just the text but the caret to where it
was — anchored, so it is meaningful against the restored text.

**Contract.** Undo/redo (`undoD2`/`redoD2`) emits a model change; the caret
is re-resolved from the Session anchor as in B.1/B.2. The undo path stores
the pre-edit selection so it can be restored.

**Live.** Rides the same `establishFocus` path on the post-undo model change.
(Live still carries known edge-case failures here —
`tst_live_render_focus_chokepoint_invariant` — listed in the project status
as pre-existing; do not assume this corner is pristine.)

**Styled / source.** 🟡 The delivery path exists (any model change re-resolves
from the Session anchor via `syncFromSession`), but full restoration depends on
`undoD2`/`redoD2` repopulating the Session selection — not added in the
2026-05-27 fix. As honest as the live side here.

**Citations.** `applyFlatEdit` undo/redo (`undoD2`/`redoD2`,
`markoff-core/CLAUDE.md` §applyFlatEdit); B.1.

---

## §C. Structure & kind

### C.1 — Canonical structure on edit

See §A.3 — listed there because it is enforced by the same ingress that
handles text sync. Cross-referenced here so a reader scanning "structure"
finds it.

### C.2 — Kind transition without re-entrancy

**Problem.** Typing `## ` at the start of a paragraph should turn it into a
heading; typing `> ` into a blockquote; etc. The view infers the new kind
from the block's text and issues `Cmd::changeKind`. But that command emits
`d2DocumentChanged`, which re-enters the very handler that issued it →
infinite recursion or a re-entrancy guard.

**Contract.** Infer kind during the block walk, but **defer** the
`Cmd::changeKind` dispatch out of the current signal (e.g.
`QTimer::singleShot(0, …)`) so it does not re-enter synchronously. Inference
must be **conservative**: never *demote* a block (don't turn a heading back
into a paragraph just because the `#` is mid-deletion) — return the current
kind on ambiguity. A cascade of spurious demotions was a real styled bug
(fixed by making `inferKindFromPrefix` conservative, commit `fc606b7`).

**Live.** `KindTransition.cpp`'s `inferBlockKind`, run against each Equal-op
block in `LiveListModelBinding::onD2Changed`; calls `Cmd::changeKind` on
mismatch. Hardcoded prefix rules.

**Styled.** Solved — same prefix rules, deferred dispatch via
`QTimer::singleShot(0)`, conservative inference. Documented in
`markoff-styled/CLAUDE.md` §v0.1-invariants. CodeBlock and HorizontalRule are
*not* inferred (fence-state matching; left to the load path).

**Note on the smell.** The deferred dispatch is a `QTimer::singleShot(0)` —
INVARIANTS §6 calls these out as "I gave up on understanding the timing"
smells. Here it is justified (synchronous re-entry into `d2DocumentChanged`)
and the justification is written down. A view author copying this pattern
must write their own justification, per the invariant.

**Citations.** `markoff-live/CLAUDE.md` §kind-transition;
`markoff-styled/CLAUDE.md` §v0.1; INVARIANTS §6.

---

## §D. Viewport

### D.1 — Scroll position preserved across an edit

**Problem.** An in-place edit (no block added or removed) that triggers a
model→view sync must not jump the viewport. Styled reported the viewport
"jumping around wildly" on every keystroke before the reverse-sync was made
incremental (§A.2 was the bulk of the fix) and scroll-preserve was added.

**Contract.** For in-place edits, capture `verticalScrollBar()->value()`
before the sync and restore it after Qt's layout signals settle. For
structural edits (block added/removed), let the toolkit's natural
"ensure caret visible" position the viewport — the caret is the right anchor
there.

**Live.** Scroll is managed by the `ListView` + stable row identity (a
`BlockId`-keyed model means a re-render of one row does not reflow the list);
the "preserve contentY" bandaid was retired once row identity was stable
(see project memory `project_tier3_completion`).

**Styled.** 🟡 **PARTIAL** — in-place edits preserve scroll
(`StyleApplier::captureScrollBeforeEdit` + deferred restore; documented in
`markoff-styled/CLAUDE.md` §v0.1). Structural edits defer to Qt's
ensure-cursor-visible, which is only as correct as the caret is (so this
improves automatically when §B.1 lands). Source view: inherits the same
binding; not separately audited.

**Citations.** `markoff-styled/CLAUDE.md` §scroll-position-preserve;
binding-robustness spec §reverse-path.

---

## §E. View-shape-specific concerns

These do **not** apply to every view — they arise only for *per-block
delegate* views (live). A single-widget flat view (styled/source) is exempt,
and it is worth knowing *why*, so a future hybrid view knows when it re-enters
this territory.

### E.1 — Per-block delegate focus hand-off

**Applies to:** per-block delegate views only.

**Problem.** When each block is its own focusable item (a QML delegate, a
sub-widget), moving the caret across a block boundary is a *focus* change
between items, not just a cursor move within one text field. Arrow-down at
the end of block N must move focus to block N+1's delegate and place the
caret at the visual-column-appropriate offset.

**Contract.** A single chokepoint owns "which block has focus and where the
caret is in it," and resolves pending focus when the target delegate becomes
available (delegates are created lazily by the view).

**Live.** `LiveCursorState` + `establishFocus` + `delegateAvailable` /
`delegateGoingAway`. The pending-focus mechanism exists *because* the target
delegate may not exist yet when the structural op runs.

**Flat views.** **N/A.** There is one `QTextEdit`/`QPlainTextEdit`; crossing a
block boundary is an ordinary cursor move within a single focusable widget.
This is the single biggest simplification the flat-text shape buys — and the
reason flat views still need §B (the caret-*position* problem remains) but not
§E (the caret-*ownership-across-widgets* problem disappears).

**Citations.** `LiveCursorState::delegateAvailable/delegateGoingAway`;
`markoff-live/CLAUDE.md` §cursor-delivery.

### E.2 — Visual-line cursor hints across wrapped lines

**Applies to:** per-block delegate views only.

**Problem.** Arrow-up/down should move by *visual* line (respecting soft
wrap), which in a per-block view may mean predicting which visual row of an
adjacent delegate the caret should land on before that delegate has laid out.

**Contract.** Carry a visual-line/column hint with the focus request;
resolve it against the target delegate's layout when it becomes available.

**Live.** Historically `pendingVisualLineHint` (see INVARIANTS §5's cautionary
tale — a C++ test that called the slot directly did *not* protect the
QML-reached production path).

**Flat views.** **N/A** — a single `QTextEdit` does visual-line navigation
natively across the whole document; there is no cross-delegate prediction.

**Citations.** INVARIANTS §5; developmental-history doc.

---

## §F. Scope boundary — what this guide does not cover

- **Rendering and styling** — how a view paints bold, headings, tables,
  math. That is per-view and not a shared-seam concern.
- **Parser internals** — how `inlineSpansFor` / `iterateBlocks` compute
  spans. The guide consumes the parser's output; it does not document it.
- **The CRDT itself** — collabtext's merge semantics. Views hold
  `TextAnchor`s by value and never see `Crdt::Anchor`.
- **Engineering discipline** — [`INVARIANTS.md`](INVARIANTS.md) owns the
  eight rules for *how to work* in this seam. This guide owns *what the
  problems are*.

The guide is strictly about the **view ↔ model seam**: how edits flow in,
how changes flow back, and how the caret stays meaningful across both.

---

## Appendix: concern status at a glance

| # | Concern | live | source | styled |
|---|---------|------|--------|--------|
| A.1 | Forward boundary attribution | N/A | ✅ | ✅ |
| A.2 | Reverse sync, no wipe | N/A | ✅ | ✅ |
| A.3 | Canonical structure on edit | ✅ (opt-out) | ✅ | ✅ |
| A.4 | Cross-block delete → merge | ✅ | ✅ | ✅ |
| B.1 | Caret re-assert after structural edit | ✅ | ✅ | ✅ |
| B.2 | Caret survives model rebuild | ✅ | 🟡 | 🟡 |
| B.3 | Multi-block selection-delete caret | ✅ | ✅ | ✅ |
| B.4 | Undo/redo restores caret | 🟡 | 🟡 | 🟡 |
| C.2 | Kind transition without re-entrancy | ✅ | N/A | ✅ |
| D.1 | Scroll preserved across edit | ✅ | 🟡 | 🟡 |
| E.1 | Per-block delegate focus hand-off | ✅ | N/A | N/A |
| E.2 | Visual-line cursor hints | 🟡 | N/A | N/A |

✅ solved · 🟡 partial / known edge cases · ❌ open · N/A not applicable to
this view shape.

**B.1 and B.3 are closed for the flat-text leaves as of 2026-05-27** (spec
`specs/2026-05-27-flat-view-enter-and-caret-authority-design.md`). B.2 and B.4
remain partials — the mechanism is wired (`syncFromSession` resolves an inbound
Session selection through the same `caretResolved` chokepoint), but full parity
awaits two follow-ups: the local caret being pushed *to* the Session (so a
collaborator can see it) and `undoD2`/`redoD2` repopulating the Session
selection (so undo restores the precise pre-edit caret).
