# Plan — hide a matching first heading as the document title (H arc)

**Consumer:** Hologram (a new `Markoff::Canvas::EditorWidget` consumer —
single-document Markdown wordpad; submodules this repo).
**Consumer handoff (normative-ish ask):**
`/home/clinton/dev/Hologram/docs/handoff/2026-08-27-to-markoff-hide-first-heading-as-title.md`.
**Date:** 2026-08-30.
**Status:** new — no code written yet. Sits alongside the G1 a11y arc
(current workfront); does not depend on it and is not blocked by it.
**Pin at handoff:** `5d885036` (current HEAD). Baselines: default-config
(LIVE-OFF) full suite **208/208** (the handoff's "315/315" is the
pre-G3-retirement live-ON figure; live retired 2026-08-19); canvas-scoped
run 40/40. Both must hold — any drop is a regression.

This plan is written for **consecutive fresh agent sessions** (same
protocol as the G1 plan). Each task is sized for one session. Do the
topmost unchecked task in the current phase. Phase-close tasks (⏸)
are hard stops.

---

## The ask (compressed)

Hologram renders an always-on **inline title band** (canvas P4.9,
already shipped) whose edits rename the file and rewrite the
document's first heading to stay in sync. When a file's first line is
a level-1 heading equal to the file's base name, the body currently
shows the heading **and** the band shows the title — the title is
rendered twice. They ask for an opt-in flag telling the canvas *"this
block is the document title — don't render it as a body block"*,
proposed as either:

- **A:** `View::setHideMatchingFirstHeadingAsTitle(bool)` (+
  `EditorWidget` pass-through), or
- **B:** `View::setHiddenBlockIds(QSet<BlockId>)` (consumer does the
  matching itself).

Both are acceptable to Hologram ("your call"); the flag is "just the
smallest thing that satisfies us". The hidden block must stay in the
document (CRDT buffer, `serializeForSave()`, undo/redo, round-trip
untouched) — only the projection changes. No deadline; Hologram ships
its consumer-side half today and wires the flag on a future re-pin.

## Decisions (verdicts on the handoff's open questions)

### D1 — API shape: the flag (A), not the general set (B)

The matching predicate — *"first block is a level-1 Heading whose
plain text equals `inlineTitle()`"* — is re-derived inside the leaf,
not supplied by the consumer. Reasons:

- The leaf already has every input (`inlineTitle()`, `onDocumentChanged`
  as the one chokepoint for every document edit, `blockKind`/`blockAttrs`/
  `blockText` + core's `countLeadingHashes`). Re-derivation there is free
  and atomic; the consumer has no cheap hook for "the heading was just
  edited and no longer matches" (it would have to watch every document
  change from outside).
- A consumer-supplied set goes stale the moment the title/heading
  relationship changes unless the consumer re-derives it on every edit —
  exactly the race the leaf-side derivation avoids.
- No consumer for B's extra generality exists. Keep the public surface
  minimal; a general internal hidden-block mechanism (D2) is what the
  implementation uses, it just isn't exposed.

**Internally** the hide is implemented as a single derived hidden id
(`m_hiddenTitleBlock`), not a flag-check scattered through call sites.

### D2 — index space: the title stays IN the view's block index (fold-hidden semantics), diverging from the handoff's "excluded from the block-index space" wording

The handoff expects the hidden title to be excluded from
`blockCount`/`blockIdAt`/`blockIndexOf`/`blockRect`. **We do not do
that.** The title block stays in the index space and is hidden via the
existing fold-hidden machinery (`Entry::folded` → zero-height
y-layout, `state().invisible`, caret-motion skip via
`nextVisibleEntryIndex`). Reasons:

1. **`blockIndexOf`'s documented contract is "document-order index"**
   (`View.h:272`). Excluding block 0 would make `blockIndexOf(block1)`
   == 0 — index space no longer document order — and would shift every
   persisted ephemeral-state index (`saveEphemeralState`'s fold +
   scroll-anchor indices, restored through `blockIdAt`) depending on
   title-match state. That is silent corruption surface, not a feature.
2. **The handoff's premise — "index membership implies rendered" — is
   already false in this leaf.** Fold-hidden blocks are in the index
   space and not rendered, and the a11y design already answers exactly
   this with `state().invisible` (G1 spec §4.3), deliberately keeping
   hidden blocks in the child list because *removing them destabilizes
   child indices for AT clients holding references*. The title-match
   state toggles mid-session (the user edits the heading), so that
   stability rationale applies to it verbatim.
3. **The "same view-local index-space principle as ProjectionMap"**
   parallel doesn't hold: ProjectionMap is sanctioned because it is
   confined to one file and never crosses the layout boundary; a
   filtered block index would cross `View.cpp`, `Accessibility.cpp`,
   and `EditorWidget.cpp` simultaneously.
4. **a11y outcome is what the handoff actually wants, achieved the
   existing way:** the container `Name` already announces the title
   once (`inlineTitle()`/`accessibleDocumentName()` resolution); the
   hidden block stays a child reporting `invisible`, which Orca's
   normal navigation skips — the tree truthfully says "this block
   exists and is invisible", identical to a fold body.

**Consequence to record for the consumer:** `blockCount()` includes
the title; `childCount()` includes it; `blockRect(titleId)` is a
zero-height rect at the right y. If Hologram later wants literal
exclusion, that is a separate, larger change — the default shape is
the stable one.

### D3 — match rule: level-1 only, plain-text rule mirrors Hologram exactly

- **Level gate:** first block must be `BlockKind::Heading` **and** its
  `AttrNames::Level` == 1. The handoff's wording says "a Heading"
  without a level; we scope to level 1 so the view's hide predicate can
  never disagree with Hologram's own `firstHeading()` (which requires
  level 1). With any-level matching, editing `# notes` → `## notes`
  would keep the block hidden while Hologram already treats it as body
  content again — a divergence in the wrong direction. If a future
  consumer wants any-level matching, relax it then.
- **Plain text:** mirror `HologramDocument::firstHeading()` exactly, so
  the two predicates agree bit-for-bit:
  - **ATX:** strip the leading `#` run (`countLeadingHashes` — already
    public core, no core change) plus one optional following space.
    Trailing closing hashes (`# notes #`) are **not** stripped —
    Hologram's strip doesn't either, and the view must not "fix" a
    buffer the consumer still reads as `"notes #"`.
  - **Setext:** buffer is already content-only (core buffer convention:
    underline stripped at load, trimmed on promotion) — use as-is.
- `inlineTitle()` empty → never matches (no hide). Requires the flag
  to be on; independent of whether the title band is visible (D5).

### D4 — never hide the document's only block

If hiding the title would leave **zero visible blocks** (a document
whose entire content is a single matching `# title` heading), the
block stays visible. A fully-invisible document has no caret target,
nothing to click, and `moveCaretToDocumentStart/End` would strand —
canvas's whole "never strand the caret" rule argues for keeping one
visible block. Degenerate edge; logged as a decided simplification,
not a bug.

### D5 — independence from band visibility

`setHideMatchingFirstHeadingAsTitle(true)` does not require
`setInlineTitleVisible(true)`. A consumer that hides without showing a
band is choosing to render the title nowhere — its call. Hologram
always shows the band.

### D6 — no core change, no constitution strain

All matching reads existing public core API
(`blockKind`/`blockAttrs`/`blockText`/`countLeadingHashes`); the hide
reuses the leaf's existing fold-hidden projection. **C1–C4 untouched** —
no re-entrance guards, no deferrals, no second document model, no new
coordinate space (per-block byte offsets throughout; the hidden block
is never byte-arithmetic'd across). `check-constitution.sh` must pass
unmodified, and spec §8's "core needs nothing" rule holds.

### D7 — no Corbomite handoff needed

Purely additive: one new opt-in `View` setter + an `EditorWidget`
pass-through (same shape as `setInlineTitle`), behavior gated off by
default. No existing public contract changes — `blockCount`/
`blockIdAt`/`blockIndexOf`/`blockRect` semantics are untouched (D2),
the a11y child list is unchanged in count and order, and default
rendering is byte-for-byte identical to today. Corbomite can re-pin
without reading anything. (A short consumer-answer note is worth
sending to Hologram when the arc closes — see H2.3 — but that is
courtesy, not an API-breakage handoff.)

---

## Session protocol

**Start:**
1. Read this file top to bottom, then the consumer handoff
   (`…/Hologram/docs/handoff/2026-08-27-…`), then the inline-title
   section of `View.h`/`View.cpp` (P4.9) and the folding section
   (`Folding.cpp`, `isBlockHidden`, `nextVisibleEntryIndex`,
   `BlockLayoutCache::setFoldedBlocks`).
2. `git pull`, build, test:
   ```bash
   cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
   cmake --build build-dev -j 4          # never more than -j 4
   scripts/run-tests.sh -R canvas
   ```
3. Confirm the previous task's checklist state matches reality. If it
   doesn't, fixing that IS your session.

**During:**
- Scope: `libs/markoff-canvas/` is open. **`libs/markoff-core/` must
  need nothing** (D6) — if a task seems to need a core change, that is
  a finding to log and end the session on, not a fix to make.
- `tests/check-constitution.sh` must pass before every commit. This
  arc reuses folding machinery on purpose; it must never strain C1–C4.
- **G1 interaction:** the title-hidden block's a11y shape (`invisible`
  child, never foldable — D2 + H1.3) is what G1's pending A4.1 will
  read; do not let either arc regress the other's assumptions. No G1
  task is blocked by this arc and vice versa.
- Falsification protocol for every functional test: make it pass →
  plant a break in a throwaway commit → watch it fail → revert →
  record both SHAs.
- **Test-run tier:** `scripts/run-tests.sh -R canvas` is the default.
  Full suite at every ⏸ close, and at H1.4 (the matching reads
  `countLeadingHashes`, a core helper shared with other leaves — same
  tier logic as G1's A2.1).

**End:** tick the checkbox, fill SHAs, append surprises to the
findings log at the bottom. Commit `canvas(H<n>.<m>): <summary>`.
Push.

**Decision rules:**
- Decide yourself + log: plain-text edge details, test mechanics,
  anything invisible outside the leaf.
- Stop + log + end session: any C1–C4 strain, any core change, any
  weakening of a done-when, anything that would change D1–D7.
- Ask the user: scope changes, anything that would touch the
  handoff's negotiated semantics (D1–D5).

---

## Checklist

| Task | Status | Commit | Falsification (break/revert) |
|---|---|---|---|
| **H1 — hide primitive + public flag** | | | |
| H1.1 `refreshHiddenTitleBlock()` + projection union | ☐ | | |
| H1.2 Public `setHideMatchingFirstHeadingAsTitle` + `EditorWidget` pass-through | ☐ | | |
| H1.3 Foldability guard (`isBlockFoldable`) | ☐ | | |
| H1.4 ⏸ tests for H1 + falsification | ☐ | | |
| **H2 — navigation / find / selection seams** | | | |
| H2.1 `setCaretPosition` redirect + doc start + selectAll | ☐ | | |
| H2.2 a11y-tree + find-navigation tests | ☐ | | |
| H2.3 ⏸ arc close (full suite, docs, consumer answer note) | ☐ | | |

---

## Phase H1 — the hide primitive and the public flag

### H1.1 — `refreshHiddenTitleBlock()` + projection union

New private member `BlockId m_hiddenTitleBlock` (null = nothing
hidden) and private method `void refreshHiddenTitleBlock()` that
derives it:

- `null` unless: `m_doc` set, `m_hideMatchingFirstHeadingAsTitle`
  on, `m_inlineTitle` non-empty, and `m_cache->entries().size() >= 2`
  (D4 — never hide the sole block).
- Then check the **first entry** (`entries().front().id`):
  `blockKind == Heading` **and** `AttrNames::Level` attr == 1 (D3),
  and `headingPlainText(id) == m_inlineTitle` (D3's strip rule).
- Else `null`.

Add an anonymous-namespace helper in `View.cpp`:
`QString headingPlainText(const MarkoffDocument &doc, BlockId id)` —
reads `blockText`, `blockAttrs` (`HeadingForm`); ATX → strip
`countLeadingHashes` hashes + one optional space; setext → buffer
as-is. **Do not touch trailing hashes** (D3).

Wire `refreshHiddenTitleBlock()` to run:
- **`onDocumentChanged()`** — BEFORE `refreshFoldedBlocks()` (the
  union must be current when the cache is told what's folded), and
  clear on `m_doc == nullptr`.
- **`setInlineTitle()`** — after the member updates.
- **`setHideMatchingFirstHeadingAsTitle()`** (H1.2) — after the member
  updates.
- `setDocument()` is covered transitively (it calls
  `onDocumentChanged()`).

Change **`refreshFoldedBlocks()`** to feed the union — every existing
call site (`onDocumentChanged`, `toggleFold`, `setFontScale`,
`setFoldedHeadIndices`) then gets the title hidden for free:

```cpp
QSet<BlockId> hidden = hiddenBlocksFromFolds();
if (!m_hiddenTitleBlock.isNull())
    hidden.insert(m_hiddenTitleBlock);
m_cache->setFoldedBlocks(hidden);
```

**Caret-unstrand** (the fold-path's own "never strand the caret" rule,
`toggleFold`'s precedent): in `onDocumentChanged`, after the existing
Session-resolve/`clampCaret` step, if `m_caret.block ==
m_hiddenTitleBlock`, move the caret to the first visible entry
(`entries().front()` — the title is block 0, so the first visible
entry is entry 1; use `nextVisibleEntryIndex(0, true)`).

**Done when:** with the flag on and a matching `# title` first line, the
heading occupies zero height in `documentHeight()`, is skipped by
arrow/cross-block caret motion (`nextVisibleEntryIndex`), reports
`isBlockHidden()`, and all of it is driven by `refreshFoldedBlocks`'s
union (no scattered flag checks).

### H1.2 — public API + `EditorWidget` pass-through

`View.h` (new subsection beside the inline-title P4.9 block):

```cpp
/// Hologram/H: opt-in "first heading is the document title" projection.
/// When on, if the FIRST block in document order is a level-1 Heading
/// whose plain text equals inlineTitle(), that block is hidden from the
/// body: zero-height y-layout, not a caret target, `isBlockHidden()`
/// true, blockRect() zero-height at its y. It STAYS in the block index
/// space (blockCount/blockIdAt/blockIndexOf unchanged — document order
/// intact) and in the document (serialize/undo/round-trip untouched).
/// Never hides the document's only block. Re-derived continuously:
/// hiding toggles as the heading or the title changes. Off by default.
void setHideMatchingFirstHeadingAsTitle(bool hide);
bool hideMatchingFirstHeadingAsTitle() const { return m_hideMatchingFirstHeadingAsTitle; }
```

No-op if unchanged; on change, `refreshHiddenTitleBlock()` +
`refreshFoldedBlocks()` + `viewport()->update()` (the same
re-derivation `setInlineTitle` gets in H1.1). New member
`bool m_hideMatchingFirstHeadingAsTitle = false;` near the other
inline-title state.

`EditorWidget.h`/`.cpp`: thin pass-through pair, same shape as
`setInlineTitle`/`inlineTitle`.

**Done when:** both layers compile; setter flips the hide on/off live;
getter round-trips; header docs match D2/D3/D4.

### H1.3 — foldability guard

`View::isBlockFoldable(id)`: early-return `false` when
`id == m_hiddenTitleBlock`.

Consequences (all desirable, record at the guard site):
- `toggleFold(title)` becomes a no-op (it gates on `isBlockFoldable`).
- `foldAffordanceRectFor(title)` returns null.
- G1's future A4.1 (expand/collapse a11y state) reads
  `isBlockFoldable` — the title will never report `expandable`.
- `setFoldedHeadIndices` (ephemeral-state restore) drops a fold index
  pointing at the title — correct "restore whatever you can" behavior.

Do **not** touch `Folding::resolveFoldable` itself — `hiddenBlocksFromFolds`
iterates `m_foldedHeads` (never the title), so fold *shape* is
unaffected.

**Done when:** `isBlockFoldable(titleId) == false` while hidden, and
fold behavior for every other block is unchanged.

### H1.4 — ⏸ H1 tests + falsification

Extend `tests/tst_canvas_inline_title.cpp` (thematically adjacent;
keeps the canvas target count stable — same convention G1 used to grow
case counts inside an existing binary). Fixtures via
`loadFromMarkdown` + `setInlineTitle` + the new flag:

1. ATX match (`# notes`, title `notes`) → hidden: `isBlockHidden`,
   zero-height `blockRect`, `documentHeight` shrinks by the heading's
   laid-out height, `blockCount`/`blockIndexOf(block1) == 1` unchanged
   (D2).
2. Setext match → hidden (D3 setext rule).
3. No match (heading text ≠ title) → visible.
4. Level-2 heading with matching text → visible (D3 level gate).
5. Empty `inlineTitle` → visible.
6. Flag off → visible.
7. Single-block document (`# notes` only) → visible (D4).
8. Mid-session re-derivation: heading edited away → un-hides; edited
   back → hides.
9. Caret-unstrand: caret in block 0, then block 0 becomes a matching
   heading → caret lands on block 1.
10. a11y: `QAccessible::queryAccessibleInterface(view)` child 0 is the
    title with `state().invisible` true; `childCount()` ==
    `blockCount()` (title included).

**Runs the full suite** (D6 — `countLeadingHashes` is shared core).

Falsification protocol per group (H1.1's union, D2's
`isBlockHidden`, D3's level gate, D4's sole-block guard, H1.3's
foldability guard). Done-when for the task's own checklist rows also
needs the constitution check.

**Done when:** all 10 groups pass, canvas suite green, constitution
clean, baseline not dropped.

---

## Phase H2 — navigation / find / selection seams

### H2.1 — `setCaretPosition` redirect + doc start + selectAll

- **`setCaretPosition(block, byteOffset)`** (the one chokepoint every
  programmatic caret placement routes through — `EditorWidget::
  setCursorPosition`, a11y `setCursorPosition`, find navigation via
  `onFindNavigationRequested`, `exitTitleEditingToBlockZero`): after
  the existing unknown-block clamp, if `block == m_hiddenTitleBlock`,
  redirect to the first visible entry byte 0 — `entries().front()` is
  the hidden title itself, so land on `nextVisibleEntryIndex(0, true)`.
- **`moveCaretToDocumentStart()`** (Ctrl+Home): if the first entry is
  the hidden title, land on the first visible entry. Keep the existing
  fold-blindness precedent for the rest (do not make it fold-aware in
  this task — logged).
- **`selectAll()`**: when the title is hidden, anchor at the first
  visible entry instead of `entries().front()` (the invisible title's
  text must not leak into a copy the user can't see).
- `moveCaretToDocumentEnd`/`moveCaretVertically`/`moveCaretHorizontally`
  need **no** change: `nextVisibleEntryIndex` already skips the folded
  title, and End lands on the last entry (never the title when a hide
  is active).

**Done when:** every caret ingress lands on the first *visible* block,
never inside the hidden title; Ctrl+Home skips it; Ctrl+A excludes it.

### H2.2 — a11y-tree + find-navigation tests

- Find: `FindController` matches inside the hidden title (needle ==
  title text); `navigationRequested` → `EditorWidget::onFindNavigationRequested`
  → caret lands on the first visible block (not the invisible match).
- Ctrl+Home from a later block skips the title.
- `selectAll` + copy excludes the title's text.
- a11y stability: `childCount()` and `child(i)` index identity are
  unchanged across a hide→unhide→hide toggle (the title's index never
  moves — D2's whole point; assert the title stays child 0 while
  invisible).

Falsification per group (drop the `setCaretPosition` redirect; drop the
`moveCaretToDocumentStart` skip; drop the `selectAll` anchor change).

**Done when:** all groups pass, canvas suite green, constitution clean.

### H2.3 — ⏸ arc close

Full suite (default config) + `check-constitution.sh`. Update:
`docs/STATUS.md` (workfront note + baseline), root `CLAUDE.md`
(current-workfront status), canvas `CLAUDE.md` (status line), and
move this plan's status to CLOSED. **Write the consumer answer note
back to Hologram** (`docs/handoff/2026-08-30-to-hologram-hide-first-heading-as-title.md`):
flag shipped + exact call site (`setHideMatchingFirstHeadingAsTitle(
isFirstHeadingTitleMatch())` in their attach path), D1–D5 verdicts
especially the **D2 divergence** (title stays in the index space /
a11y tree as an `invisible` child — not excluded as their handoff
worded), and the `blockCount`-includes-title consequence. This is the
API surface decision they asked us to make; they must read it before
wiring the flag.

---

## Findings log

(One line minimum per task. Append; never rewrite.)
