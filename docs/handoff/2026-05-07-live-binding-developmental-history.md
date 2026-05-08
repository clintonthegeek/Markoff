# Live-binding pipeline — developmental history

**Date:** 2026-05-07
**Branch:** `exploration/new-foundation`
**Working tree:** `.worktrees/foundation-exploration/`
**Audience:** the user, evaluating the v1.0 Part 3 plan
(`docs/plans/2026-05-07-markoff-v1.0-part3-live-facade-perf.md`) against
the developmental record. Descriptive, not prescriptive.

This document traces where each piece of the current
`LiveListModelBinding` ↔ `AstBlockDiff` ↔ `LiveBlockModel` ↔
`MarkoffDocument` pipeline came from, what it replaced, what it solved,
and what (if anything) about that problem has since changed. Section A
is the meat; Section B sketches the surrounding arc context.

The proposed v1.0 Part 3 rewrite — Phase 3 in particular — replaces the
Myers full-walk in `onD2Changed` with a targeted handler driven by
Part 2's new `MarkoffDocument::blocksChanged` /
`blockInserted` / `blockRemoved` signals (commit `00c78d2`). The history
below is aimed at making it possible to read that plan and tell which of
the pipeline's pieces it would correctly retire, which it would silently
break, and which it leaves untouched.

---

## Section A — Pipeline narrative

### 1. `LiveListModelBinding::onD2Changed` — the full-walk on every debounced edit

**Current shape.** `onD2Changed` (lines 127–308 of
`libs/markoff-live/src/LiveListModelBinding.cpp`) does the following on
every `documentLoaded` and every `d2DocumentChanged` fire:

1. `doc->iterateBlocks()` enumerates every block in the document.
2. For each id: read kind, `blockText`, `inlineSpansFor`, `blockAttrs`,
   trim trailing newline, populate `BlockRecord`.
3. Build a `QList<BlockKey>` of `(kind, BlockId)` pairs.
4. Run `AstBlockDiff::diff(d->lastKeys, nextKeys)` (Myers/LCS).
5. For each `Equal` op, run `inferBlockKind` against the block's text
   and, on mismatch, fire `Cmd::changeKind` and *return* — relying on
   the next `d2DocumentChanged` to re-enter and converge.
6. `applyOps(ops, records)` against the model.
7. Emit `structuralRowsInserted` / `structuralRowRemoved` for each
   non-Equal op so `LiveCursorState` can resolve pending cursors.

**Where it came from.** The function was introduced in commit `30660a8`
("d2(view): LiveListModelBinding D2 drive + LiveSelectionView + cursor
test", 2026-05-04) as part of D2 Phase 11 (the in-tree migration of
`markoff-live-render` onto the per-block CRDT foundation). The commit
message:

> "Wire model from documentLoaded (synchronous) + d2DocumentChanged
> (debounced) instead of idListProxy::structureChanged. Implement
> onD2Changed() iterating d2 block list, converting BlockKind enum,
> trimming trailing newlines."

The function it replaced was `onParseUpdated`, the C-restoration's R2
slot, which subscribed to `MarkoffDocument::parseUpdated(quint64
parseSequence, …)` and ran the same Myers diff but against
`BlockWalker`'s output (`Markoff::Document` → `QList<BlockRecord>` —
parser-derived, async). See commit `5a0dae7` ("R2 — read-only render
with diff-driven model", 2026-05-02) for the original shape.

**What design problem was it solving.** Three layered problems
collapsed into one D2 entry point:

1. *Decouple the view from the parser cycle.* In C-restoration, every
   structural change required waiting for `parseUpdated` to arrive
   (async, `~30–100 ms`), then diffing the new parser-derived block
   list against the previous one. D2 made the per-block CRDT canonical;
   `d2DocumentChanged` fires from the CRDT side, not the parser side.
2. *Honor a uniform "rebuild from authoritative state" idiom.* D2
   Phase 11's migration was deliberately mechanical: replace one signal
   source with another, keep the same diff machinery. The full-walk
   shape was inherited from R2, not redesigned for D2.
3. *Tolerate the foundation's coarse `d2DocumentChanged`.* The signal
   is debounced (one per event-loop spin) and carries no "what
   changed" payload; the binding has to rediscover changes by walking.

**What's changed since.** D3 (commit `efbd4f9` and successors) loaded
the per-block extraction step with attrs reads, kind-specific extra
fields (`headingLevel`, `codeLanguage`), and inline-span population —
heavy work per block. D3 also inserted the kind-transition detection
loop inside `onD2Changed` (item 3 below). The full-walk's per-keystroke
cost grew accordingly. The v1.0 Part 3 plan calls this out as the
performance pathology and proposes targeted handlers.

Note: the full-walk shape predates D2 by two days. It is *not* a
D2-specific design decision — it was a property of the C-restoration's
R2 read-only path that D2 chose not to rewrite when re-pointing the
signal source. R2 itself ported the diff machinery over from
`markoff-view-qml` (per the R2 plan, "Read the existing ports we're
basing this on").

---

### 2. The Myers diff over `(kind, BlockId)` keys (`AstBlockDiff`)

**Current shape.** `AstBlockDiff::diff` (in
`libs/markoff-live/src/AstBlockDiff.cpp`) is an LCS table O(m·n) over
two `QList<BlockKey>` where `BlockKey = (kind, BlockId)`. With identity
fast-path, then backtrack, then a *post-pass that collapses adjacent
`Delete[i]+Insert[i]` (or `Insert[j]+Delete[j]` reversed) where the
collapsed pair shares the same `kind`* into a single `Equal` op.

**Where it came from.** Landed with R2 (commit `5a0dae7`,
"feat(live-render): R2 — read-only render with diff-driven model",
2026-05-02). The R2 plan
(`docs/plans/2026-05-02-live-render-r2-read-only-render.md`) ports it
intact from the older `markoff-view-qml/src/AstBlockDiff.{h,cpp}`. The
header comment on `BlockKey` is "diff identity key."

**Why this shape.** The C-restoration design
(`docs/specs/2026-05-02-live-render-restoration-design.md` §2 layer
diagram, line 69) puts the diff at L2: "L2 Diff-driven model (Myers
over `BlockKey`; per-row sequence)." The premise is that the parser
emits a snapshot list of `(kind, BlockAnchor)` pairs and the view's
job is to compute the minimum edit script between successive
snapshots. Identity is `(kind, BlockAnchor)`: a kind change
*should* manifest as Delete+Insert (different keys → different
identity), and an in-place text edit *should* manifest as Equal
(same kind, same anchor → same identity).

**The Delete+Insert collapse post-pass — a cycle-guard from a retired
arc.** The post-pass (`AstBlockDiff.cpp` lines 53–85, with the verbose
explanatory comment) was *not* part of the original R2 design. It was
added in commit `e94553d` — "wip: R5.5 dogfood iteration snapshot
(retired by marker design)", 2026-05-04. The same commit added
`anchorRenumbered` (item 4 below); both are sibling cycle-guards for
the same root cause. The commit message itself says these patches are
"about to be deleted or rewritten by the marker-paragraph plan" and
preserves them for history.

**What it solved.** As the post-pass comment in the file states
verbatim:

> "the foundation's BlockAnchor-instability case — typing at qtPos 0 of
> a block changes the byte-0 character's CRDT identity, so
> computeBlockAnchors hands out a 'new' anchor for the same logical
> block. Without this collapse the model emits Delete+Insert per
> keystroke and the QML delegate is destroyed and recreated, mid-typing
> focus is lost."

The architectural review at
`docs/handoff/2026-05-04-r5.5-dogfood-architectural-review.md` §1.4
(Bug D) gives the long form. Live-render assumed `BlockAnchor` was
stable across in-place edits; the foundation's `textAnchorAt(startByte,
leftBias=true)` only guaranteed stability under bytes-after-the-anchor
change, not bytes-at-the-anchor change.

**Was the underlying problem fixed?** D2 changed `BlockId`'s identity
from "first-byte text anchor" (the C-restoration's parser-side
`BlockAnchor`) to "structural-CRDT element id" — a `CollabText::Crdt::IdList`
element id, which by construction is *stable across all in-block edits*.
After the D2 migration the Bug-D class of identity flips should not
occur. (The D-arc roadmap §3.2 records this carry-forward decision: the
discriminated cursor model uses `BlockId` "rebound to an `IdList`
element id" — *more* stable than the C-restoration's `BlockAnchor`.)

**Yet the post-pass survives in tree.** It was preserved through the
D2 Phase 11 migration without re-evaluation, and through the rename of
the library (`markoff-live-render` → `markoff-live`, commit `4711a92`)
and the include-path move (commit `5f5c1f2`). The record does not show
a deliberate "verify this is still needed under D2" step; it shows the
file being mechanically forwarded as the libraries reorganized. The
companion `anchorRenumbered` machinery (item 4) likewise survives.

**What this means in practice.** Under D2's `BlockId == IdList element
id`, the keystroke-at-qtPos-0 path produces `Equal` ops (same kind,
same id → same key) directly from the LCS step; the collapse post-pass
should be a no-op in the common case. It is not load-bearing under D2,
but it is also not provably dead — it would still fire if for any
reason the diff produced a `Delete[i]+Insert[i]` pair with matching
kind, e.g. during certain structural-CRDT sequences the developmental
record does not enumerate.

---

### 3. Kind-transition detection inline in `onD2Changed`

**Current shape.** `onD2Changed` (lines 182–291) iterates the diff's
`Equal` ops and, for each, runs `inferBlockKind(rec.text, &displayMode)`
(a hardcoded prefix-rule classifier in `KindTransition.cpp`). On
mismatch, the binding fires `Cmd::changeKind` (or, for the ListItem
promotion path, an explicit transaction that strips the marker, sets
`MarkerStyle/MarkerNumber/IndentLevel/LooseRun` attrs, and calls
`Cmd::renumberRunStartingAt`) and *returns* from `onD2Changed`. The
follow-up `d2DocumentChanged` re-enters the function with the corrected
kind in the CRDT.

There is a "promote FROM Paragraph only" guard (line 212): once a block
has a structural kind (Heading, ListItem, …) its buffer holds
content-only text, so re-running `inferBlockKind` on that text would
always infer Paragraph and demote it.

**Where it came from.** Commit `efbd4f9`, "feat(live-render):
onD2Changed populates inline spans, attrs, headingLevel, codeLanguage;
kind-transition detection", 2026-05-05. This is D3's work
(`docs/specs/2026-05-05-d3-view-layer-adaptation-design.md` §5).

**What was the earlier design.** Two-step lineage:

1. *C-restoration era (parser-driven).* Under the C spec, the parser
   was the kind authority: tree-sitter would re-classify on every
   re-parse, and the parser-derived `BlockRecord`s would carry the
   correct kind. View code did not infer kind at all.
2. *D2 (CRDT-only, Phase 11).* D2 Phase 11 retired async parse for
   text edits — the parser is only invoked at load time and per-block
   on demand for inline spans. With no per-edit parser run, the
   foundation has no automatic "this paragraph just gained a `# `
   prefix; it's now a heading" mechanism. After D2 Phase 11 landed,
   typing `# ` into a paragraph did *not* promote it; the kind sat
   wherever it was last set.

D3's spec (§5, "Kind-transition detection") explicitly resolved this:
"View-driven, in `KindTransition.cpp`'s `inferBlockKind`. Runs against
each Equal-op block's text in `LiveListModelBinding::onD2Changed`;
calls `Cmd::changeKind` on mismatch."

**Why view-layer.** D3 §5's "Why this and not the alternatives"
sub-section (line 332) is explicit:

> "No `LiveSpeculationLayer`: under D2's per-block CRDT, kind-transition
> detection in `onD2Changed` fires one event-loop spin after the
> triggering edit. The QML model update follows immediately on the
> same spin's queued events. This is fast enough that speculative
> pre-painting produces no user-visible benefit."

The C-restoration's L4 had a `LiveSpeculationLayer` for paragraph
typing; D3 replaced it with view-layer kind inference because D2's
single-spin debounce makes speculation unnecessary.

**What's changed since.** The D3-corrective spec (commit `5da92dc`,
"D3-correction complete", 2026-05-06) tightened the ListItem path: the
inline kind-transition logic in `onD2Changed` was extended to parse the
list-marker prefix, strip it from the buffer in a transaction, and set
the `MarkerStyle/MarkerNumber/IndentLevel/LooseRun` attrs. That work
is what now lives in lines 225–273 of the current file. So the inline
kind-transition path has grown (not shrunk) since D3 first landed.

The Part 3 plan's Phase 4 ("move kind-transition out of the hot path")
is a real reduction — but the architectural decision that made
"hot path" mean "inside `onD2Changed`" was D3's, taken with awareness
that no perf complaint had yet been observed. The relocation is a
cost-reduction move, not a correctness fix.

---

### 4. `anchorRenumbered` from `LiveBlockModel::applyOps`

**Current shape.** `LiveBlockModel.cpp` lines 119–132: when an `Equal`
op carries a different `BlockAnchor` than the row currently at that
index (because `AstBlockDiff` collapsed a `Delete+Insert` into an
`Equal`), the model emits
`anchorRenumbered(row, oldAnchor, newAnchor)`. `LiveCursorState`
listens (`LiveCursorState.cpp` lines 36–62) and silently swaps both its
current `TextCaret`'s anchor *and* any pending anchor-keyed request,
without firing `cursorChanged` (the QML delegate is the same; only the
identity changes).

**Where it came from.** Same commit as the diff post-pass —
`e94553d` ("wip: R5.5 dogfood iteration snapshot (retired by marker
design)", 2026-05-04). The two pieces are siblings: the post-pass
detects the renumbering at the diff layer, the signal propagates it to
the cursor layer.

**What problem in the foundation does it work around.** Bug D from the
R5.5 dogfood architectural review:

> "the architecture's identity model — 'block = stable BlockAnchor
> across parses' — is not actually held by the foundation. The
> foundation gives block anchors stable *under bytes-after-the-anchor
> change*, but unstable *under bytes-at-the-anchor change*. Live-render
> assumed the stronger guarantee everywhere and built cursor identity,
> diff identity, and renumber semantics on it."

So this was a workaround for a C-spec-era foundation guarantee that
didn't hold.

**What's changed since.** D2's `BlockId == IdList element id` removes
the underlying instability — the structural CRDT id is independent of
any byte content. The R5.5 architectural review explicitly identifies
the "foundation amendment to stable block IDs" path (its candidate b)
as the right architectural fix. D2 *is* that amendment, in effect —
but written as a "replace the foundation" project rather than a
"patch `BlockAnchor`" project.

**Yet the cycle-guard survives.** As with item 2's diff post-pass, the
record does not show a deliberate "this is now dead under D2" pass
through the live-render code. The comment on `LiveCursorState`'s
listener (line 29) still reads as if D2's foundation has the same
instability: "Survive the foundation's per-keystroke `BlockAnchor`
renumbering at qtPos 0 of a block." The renumbering is no longer
expected to happen under D2; the listener will simply never fire. The
code is benign-but-vestigial.

(The current `LiveListModelBinding::onD2Changed` writes
`r.blockAnchor = id` directly from the iterated `BlockId` — not from a
parser-derived computation — so any anchor difference between
prev/next would have to come from `BlockId` re-emission, which under
D2's IdList model does not happen for in-block edits.)

---

### 5. `structuralRowsInserted` / `structuralRowRemoved` from `LiveListModelBinding`

**Current shape.** After `applyOps`, `onD2Changed` (lines 300–306)
walks the diff a second time and emits
`structuralRowsInserted(first, last)` for each `Insert` op and
`structuralRowRemoved(row)` for each `Delete` op. `LiveCursorState`
subscribes to these signals (lines 23–26) to resolve pending
row-keyed and anchor-keyed cursor requests — the path that lets a
mid-block split's "wait for the new row to appear" complete.

**Where it came from.** Commit `3421bdd`, "feat(live-render):
LiveListModelBinding emits structuralRowsInserted/Removed from
onD2Changed", 2026-05-05. Two follow-up commits in the same lineage:
`b44d925` ("LiveCursorState subscribes to structural signals; retire
parse-cycle cursor path") and `1427e61` ("remove noteParseArrived call
from onD2Changed").

**When did the parse-cycle path get retired.** That trio. Before
`b44d925`, `LiveCursorState` had a different shape: it tracked
`parseCyclesSeen` per pending request and required a parse to "arrive"
(via `noteParseArrived`) before resolving. That was the C-restoration's
mechanism — it depended on the parser being the boundary authority for
"a new row exists now." Under D2 the parser doesn't run on edits, so
the parse-cycle delivery path was meaningless.

The replacement was a direct hop from `applyOps` to the cursor: the
binding tells the cursor exactly which rows just appeared/disappeared,
and the cursor matches that against its pending anchor/row requests.

**What problem does it solve.** Cursor survival across structural
edits. The R5.5 architectural review's Bug B (line 47) showed how
fragile the conflated `requestTextCaretAtRow(row, qtPos)` was when
"resolve immediately if `row < signalModel.rowCount()` else wait" had
to choose between two semantics. The split into two methods
(`requestTextCaretAtRow` for "row already exists, just move" and
`requestTextCaretAtNewRow` for "row will be born here, wait for it")
was made inside the C-restoration; the *signal* from the model that
satisfies "wait for it" was redesigned in D3 (the structural signals).

**What's changed since.** Nothing on this path. It's the canonical D3
delivery mechanism. The Part 3 plan's Phase 3 is silent on whether the
new targeted-handler design preserves these structural signals. If the
new `onBlockInserted(BlockId, int row)` slot does not also emit
`structuralRowsInserted`, `LiveCursorState`'s pending-cursor resolution
breaks. (The plan's draft uses `model->insertRow(row, …)` and emits
"the corresponding incremental signal" but does not enumerate the
binding-level signal.)

---

### 6. `d2DocumentChanged` debounced signal (`scheduleD2Changed`)

**Current shape.** `MarkoffDocument::scheduleD2Changed`
(`MarkoffDocument.cpp` line 397) uses `QTimer::singleShot(0, …)` with a
re-entry guard (`d->d2ChangedScheduled`) to coalesce all D2 mutations
within a single event-loop iteration into one `d2DocumentChanged`
emission. Every CRDT mutation path
(`applyBlockEdit`, `applyStructural`, `d2ApplyBufferEdit`,
`d2InsertBlock`, `d2RemoveBlock`, `d2SetBlockKind`, `d2SetBlockAttr`,
`undoD2`, `redoD2`, the various `Cmd::*`) calls `scheduleD2Changed()`
at its tail.

**Where it came from.** Commit `6c5c710`, "d2(foundation):
MarkoffDocument undoD2/redoD2/undoForBlock + signals", 2026-05-04.
Tasks 4.5/4.6 of the D2 plan.

**Why a single-event-loop debounce.** D2's plan
(`docs/plans/2026-05-04-d2-foundation-reshape.md`) does not fully
spell out the rationale, but the shape is consistent with two
constraints:

1. *A single user keystroke can produce multiple CRDT ops.* For
   example, `Cmd::enterAtEnd` can issue an `applyBlockEdit` (trim
   trailing spaces) plus `applyStructural` (insert new block) plus a
   kind set, all inside one transaction. The view should update once,
   not three times.
2. *The view does a full walk on every fire.* Coalescing to one fire
   per event-loop iteration bounds the cost. (This was the assumption
   under which the full-walk shape was tolerable.)

**What was before.** The C-restoration era used the two-arg signal
`MarkoffDocument::parseUpdated(parseSequence, parseInputEditSequence)`,
fired when an async parse completed. That signal was deprecated in D2
Phase 14 (commit `6b45b4a`) and deleted in D4 Phase 9 (commit
`3478132`).

**What's changed since.** Nothing structural; the Part 2 work added
`documentChanged()` (no-arg, commit `65c6397`) and the targeted block
signals (commit `00c78d2`) but did not retire `d2DocumentChanged`. The
two coexist. The Part 3 plan would route the binding off
`d2DocumentChanged` and onto the targeted signals; the existing
debounce remains an authority for `documentChanged()` consumers
(dirty-tracking) and for the `documentLoaded`/full-rebuild path.

---

### 7. `inlineSpansFor` call inside per-block extraction

**Current shape.** `onD2Changed` line 150:
`r.inlineSpans = doc->inlineSpansFor(id);`. The result populates
`BlockRecord::inlineSpans`, exposed by `LiveBlockModel::spansAtRow(int)`
(line 169 of `LiveBlockModel.cpp`).

**What consumes it (or used to).** A grep across the live library is
unambiguous: the only consumers are the field declaration, the
populator (`onD2Changed`), the storage (`BlockRecord`), and the getter
(`spansAtRow`). No QML, no test, no other C++ caller. Under v1.0 there
is no live consumer of the data.

**When was it added.** Commit `efbd4f9` ("D3 Phase 4 work":
"onD2Changed populates inline spans, attrs, headingLevel,
codeLanguage; kind-transition detection", 2026-05-05). The
`BlockRecord::inlineSpans` field itself was older — it dates back to R2
(commit `5a0dae7`, R1B's pre-bake), where `BlockWalker` populated it
from the parser's `TopLevelBlock::inlineSpans`. R1B's purpose was the
C-restoration's "inline span pre-bake" carry-forward (D-arc roadmap
§3.2).

**Why is it still there.** The D3 spec
(`docs/specs/2026-05-05-d3-view-layer-adaptation-design.md` §4.1, line
160) prescribed populating it: "`rec.inlineSpans = doc->inlineSpansFor(id);`."
The intent at D3 design time was that L6 delegates (Heading,
ParagraphDelegate's `InlineFormatHighlighter`, etc.) would consume the
spans for inline styling. That consumption never landed: D3 §8.1
discusses inline formatting under "bold/italic spans highlighted within
the heading text" but the implementation didn't materialize before D3
was declared complete (commit `5da92dc`). Inline rendering is
explicitly deferred to v1.1 in the v1.0 design spec
(`docs/specs/2026-05-07-markoff-v1.0-design.md` §2.2).

**State of the world.** Populated, stored, never read. The Part 3
plan's Phase 5 ("Drop unused `inlineSpansFor` from per-block
extraction") is correct on the developmental record: there is no
v1.0 consumer.

---

### 8. `MarkoffDocument::blocksChanged` / `blockInserted` / `blockRemoved` — Part 2's targeted signals

**Current shape.** Three new signals on `MarkoffDocument`, declared in
the header alongside `documentChanged()` / `documentReloaded()` and
emitted from a small set of mutation paths in
`MarkoffDocument.cpp`:

| Site | Signal | How |
|---|---|---|
| `applyBlockEdit` (line 432) | `blocksChanged({edit.blockId})` | Synchronous before `scheduleD2Changed`. |
| `d2ApplyBufferEdit` (line 620) | `blocksChanged({block})` | Synchronous before `scheduleD2Changed`. |
| `d2InsertBlock` (lines 645–656) | `blockInserted(newId, row)` | Computes the post-insertion row by walking `iterateBlocks()`. |
| `d2RemoveBlock` (lines 829–848) | `blockRemoved(block, formerRow)` | Captures the pre-removal row by walking `iterateBlocks()` *before* the IdList mutation. |

**Where they came from.** Commit `00c78d2` ("markoff-core: add targeted
block-update signals (blocksChanged, blockInserted, blockRemoved)",
2026-05-07). This was Part 2 Phase 2 of the v1.0 plan, landed before
Part 3 was started.

**What motivated the addition.** Direct quote from the v1.0 design
spec (`docs/specs/2026-05-07-markoff-v1.0-design.md` §5.1, line 159):

> "Targeted updates from the CRDT. `MarkoffDocument` already routes
> `Cmd::*` through known `BlockId`s and knows which blocks were
> touched. Replace the full-walk handler with a signal that carries
> touched block IDs (e.g. `blocksChanged(QList<BlockId>)`,
> `blockInserted(BlockId, int row)`, `blockRemoved(BlockId, int row)`).
> The model emits `dataChanged(index)` for the touched blocks and
> `rowsInserted/Removed` for structural changes. No Myers diff in the
> common keystroke case."

So the signals exist *for* Part 3; their consumer is the new
`onBlocksChanged` / `onBlockInserted` / `onBlockRemoved` slots that
Part 3 will introduce on `LiveListModelBinding`.

**Why aren't they emitted from `d2SetBlockKind`, `d2SetBlockAttr`,
undo, redo, `applyStructural`?** This is the gap the user flagged. The
record shows:

- `d2SetBlockKind` (`MarkoffDocument.cpp` line 852–859): calls
  `scheduleD2Changed()` only. No `blocksChanged({block})` emit.
- `d2SetBlockAttr` (line 861–869): same.
- `undoD2` / `redoD2` (lines 492–493): single-line bodies that call
  `d->undoLog.{undo,redo}()` then `scheduleD2Changed()`. No targeted
  emit of any kind.
- `undoForBlock` (line 494): same, but does not even call
  `scheduleD2Changed()` (it only schedules through the underlying
  `Cmd::*` paths invoked by the undo-log dispatcher).
- `applyStructural` (lines 440–485): calls `scheduleD2Changed()` only.

The `00c78d2` commit message is silent on the omission. It says only
that the three signals are "alongside the existing
`documentChanged()`/`documentReloaded()` signals" and that
`blocksChanged` "fires synchronously from both `applyBlockEdit` and
`d2ApplyBufferEdit`" and the block insert/remove signals fire "from
`d2InsertBlock`" / "from `d2RemoveBlock`." There is no record of a
positive decision to *exclude* the kind/attr/undo/structural paths.
They simply weren't wired up.

The test suite covering the signals
(`tst_v10_targeted_block_signals.cpp`) exercises only the four
emitting paths (the v1.0 plan Part 2 Phase 2, lines 184–223). It does
not assert presence-of-emission for the missing paths and it does not
assert absence-of-emission either; the contract is undefined.

**Implications, in the descriptive sense (not prescriptive).** Two
shape mismatches are visible in the record:

1. *Kind changes.* `Cmd::changeKind` (used in the live binding's
   kind-transition path, item 3 above) eventually calls
   `d2SetBlockKind`, which currently emits no targeted signal. Under
   the proposed Part 3 binding, a kind change reaches the view only via
   the `documentChanged` / `d2DocumentChanged` debounced path — which
   under the new design (Part 3 Phase 3 step 4) the binding has
   *disconnected* (the new `setDocument` shows
   `disconnect(d->document, nullptr, this, nullptr)` followed by
   connections only to `blocksChanged` / `blockInserted` /
   `blockRemoved` / `documentReloaded`). Without the
   `d2DocumentChanged` connection and without a `blocksChanged` emit
   from `d2SetBlockKind`, a kind transition would not refresh the view.
2. *Undo / redo.* `undoD2` / `redoD2` traverse multiple CRDT primitives
   for a single user-visible undo (the reason the cross-CRDT undo log
   exists at all). The reverted ops go to the underlying `IdList` /
   `Buffer` / `CausalLwwMap` directly; they do *not* go back through
   the `Cmd::*` paths and so do *not* hit the
   `applyBlockEdit` / `d2ApplyBufferEdit` emission sites that wire up
   `blocksChanged`. After an undo the new state of the document is
   reachable only via the debounced `d2DocumentChanged` and a full
   walk. Under the proposed Part 3 binding (no `d2DocumentChanged`
   subscription) this state would not propagate to the view.

The Part 3 plan's Phase 3 sample code does subscribe to
`documentReloaded` for the full-content-swap path, but `documentReloaded`
is fired only from `loadFromMarkdown` / `resetContent`, not from undo or
kind-change.

The record is silent on whether the missing emissions are intentional
(to be added incrementally), an oversight, or a placeholder for a
broader signal discipline. The commit message's wording — "alongside
the existing `documentChanged()` / `documentReloaded()` signals" —
suggests Part 2's author treated `blocksChanged` as a *narrow add*
rather than a complete replacement of the rebuild surface.

---

## Section B — Arc context

This section is compressed. The full record lives in
`docs/d-arc/d-arc-status.md`, the spec / plan tree, and the named
handoff docs.

### B.1 Master shape (incoming, retired)

`master`'s Markoff family was four leaf libraries plus a parser: `markoff-core`
(now repurposed in-tree), `markoff-live` (originally a different
QGraphicsView-based editor), `markoff-source` (Qutepart-based), and
`markoff-reading` (Obsidian-compatible reading view), all sitting on
`MarkoffParser::MarkoffParser`. The master tree was mid-Phase-C of the
Phase A/B/C migration plan — Phase A landed the tri-view API, Phase B
bridged to host-provided `Corbomite::Core` / `mmdr` types via a CMake
option, Phase C was meant to retire the bridge. None of that
work-in-progress matters for foundation-exploration; the branch
deliberately diverges (worktree CLAUDE.md banner).

### B.2 The C-restoration / R-arc on foundation-exploration

The foundation-exploration branch began with a "live-render
restoration" (C-restoration) arc: rebuild the live preview side-by-side
from the bottom up as a layered library. Spec:
`docs/specs/2026-05-02-live-render-restoration-design.md`. Roadmap:
R1 (foundation surfaces), R2 (read-only render + diff-driven model),
R3 (cursor + selection), R4 (paragraph editing), R5 (structural keys),
R6–R10 (other block kinds, lists, math, context menus, hardening).

The R-arc operated under "sequence-tagged reconciliation" — the parser
was an asynchronous boundary authority and the view computed per-row
staleness from `editSequence`/`parseSequence`. R5.5 introduced the
*marker paragraph* design (commit `bc4dee5` etc.,
`docs/specs/2026-05-03-marker-paragraph-design.md`): EOB-Enter inserted
a `\n\n​` (with ZWSP) so the parser produced a real block, eliminating
the v2 "hole" indirection the Q4 architectural review had blocked on.

The R5.5 dogfood pass surfaced six layered bugs (Bug A through Bug F,
plus a latent async-commit-window race) — see
`docs/handoff/2026-05-04-r5.5-dogfood-architectural-review.md`. The
review's verdict: each fix exposed the next bug in the layer beneath
it; the v0 cycle-guard pattern was recurring under different names; the
"if 3+ fixes failed, question the architecture" criterion was met three
times over inside one dogfood iteration. Continuing to iterate was
declared uneconomic.

### B.3 The D-evolution pivot (2026-05-04)

Triggered by the R5.5 review. The user elected to halt C-restoration
and cut to **D — per-block CRDT**. The pivot is documented at
`docs/handoff/2026-05-04-c-restoration-bookend-d-pivot.md`. The
`exploration/new-foundation` branch was created precisely to nail down
the right architecture; piling cycle-guards on cycle-guards defeated
its purpose.

The D-evolution proposal was authored in-tree on 2026-05-02
(`docs/specs/2026-05-02-d-evolution-proposal.md`); the collabtext
maintainers responded on 2026-05-04 (their `2026-05-04-d-evolution-response.md`)
approving Option β: a single new `CollabText::Crdt::IdList` primitive,
4–8 weeks of focused work, forward-compatible wire-schema bump. The
maintainers also drew six explicit "won't do" lines, recorded verbatim
at `docs/d-arc/collabtext-scope-line.md`. **No D-arc spec may take a
design decision that depends on collabtext shipping any of the six
refused items.**

The cut from C-restoration cancelled (not paused) R5.5 Bug 3, R5.5
Task 18 dogfood gate, R5 Tasks 12–17, R6, R7, R8, R9, R10, the
marker-paragraph design as active, and "sequence-tagged staleness
reconciliation" as the canonical primitive.

### B.4 D0 through D4

| Phase | Date | Summary |
|---|---|---|
| **D0** | 2026-05-02 → 2026-05-04 | Joint design with collabtext maintainers. Proposal landed 05-02; response delivered 05-04, accepting Option β with six "won't do" lines. |
| **D1** | 2026-05-04 (external) | `CollabText::Crdt::IdList` v1 shipped by collabtext maintainers. |
| **D2** | 2026-05-04 → 2026-05-05 | Foundation reshape — `Markoff::MarkoffDocument` rebuilt on `IdList` + per-block `Buffer`s + sibling causal-LWW maps. 15 phases (Phase 0 setup → Phase 14 deprecation → Phase 15 dogfood). User signed off after dogfood pass on 2026-05-05. |
| **D3** | 2026-05-05 → 2026-05-06 | View-layer adaptation. Original 27-task plan landed; per-item ListItem corrective spec landed 2026-05-06 (commit `5da92dc`) after dogfood revealed the all-list-as-one-block premise was wrong. 146/146 tests pass. |
| **D4** | 2026-05-07 | Parser scope reduction. Retired `ParsePool`, `IncrementalParseSession`, `parseUpdated`, `parseSequence`, `MarkoffEdit`, `applyLocalEdit`. Source-widget migrated to D2 via new `applyFlatEdit` primitive. `markoff-bench` and `view-qml`'s live mode retired. Dead `Cmd::*` legacy + `CommandFacade` + `ReplaceController` deleted (zero external consumers). 14 phases. 103/103 tests pass. Final commit `22ea352`. |

### B.5 D5

`docs/specs/2026-05-04-d5-collab-activation-STUB.md` — collab
activation (wire format, transport, presence, conflict UI). Stub only;
substantive design pending.

### B.6 The v1.0 plan (written 2026-05-07, same day D4 completed)

The v1.0 design spec (`docs/specs/2026-05-07-markoff-v1.0-design.md`)
and the five-part plan series (`docs/plans/2026-05-07-markoff-v1.0-*.md`)
were authored on 2026-05-07. The same day, D4 concluded with commit
`22ea352`. **D5 had no substantive design at the time.** The v1.0 plan
explicitly removes D5 / collab activation from scope (§2.2: "D5 / collab
activation. Markoff v1.0 ships single-user local. The D5 stub spec
stays in `docs/specs/2026-05-04-d5-collab-activation-STUB.md`").

The Part 3 plan
(`docs/plans/2026-05-07-markoff-v1.0-part3-live-facade-perf.md`) — the
proposal under evaluation here — frames itself as a perf fix:

> "The pathology in `LiveListModelBinding::onD2Changed()` … is a
> full-document walk on every keystroke … This is O(N) per keystroke
> with high constants, and combines with eager TextEdit instantiation,
> synchronous startup parse, and synchronous Image decoding to produce
> the observed 1-minute / 2GB / pegged-CPU symptoms."

The plan's stated v1.0-floor budget is "≤500 ms / ≤300 MB / ≤2 % idle
CPU on a 500-line document" (`tst_v10_perf_500_line` in Phase 7).

---

## Cross-cutting findings

A few patterns the record makes visible that don't fit cleanly into any
single subsection above:

1. **Two arcs invented the same machinery for different reasons.**
   - `AstBlockDiff`'s Myers diff was C-restoration's L2 (R2): the
     view's job was to compute minimum edits between successive parser
     snapshots. *The diff source was the parser.*
   - In D2 Phase 11 the same diff machinery was kept but the source
     was rebound to `iterateBlocks()` — *the diff source became the
     CRDT.* The diff is now diffing the document against itself
     (specifically, against the binding's cached `lastKeys`), one
     event-loop spin apart. Under D2 the diff is structurally a
     *change-detector* over the CRDT's authoritative state, not a
     *reconciliation* between two independent producers.
   - The Part 3 plan reads as if it noticed this rebinding's
     consequence. Its premise — "no Myers diff in the common keystroke
     case" — works *because* the new targeted signals have already
     localized the change. That logic was always available under D2;
     it just wasn't taken.

2. **The `anchorRenumbered` / Delete+Insert collapse pair is
   architecturally vestigial.** Both were written for a foundation
   guarantee (`BlockAnchor` stable across edits) that no longer applies
   under D2 (`BlockId` is an `IdList` element id). Neither was retired.
   Comments on the collapse and on `LiveCursorState`'s listener still
   describe the C-era foundation behavior. The Part 3 plan's Phase 3
   draft preserves both implicitly (the `LiveBlockModel::updateRow`
   path is described as "emits `dataChanged(index(row), index(row))`,"
   not "and may also emit `anchorRenumbered`").

3. **The kind-transition path was a D3 design decision, not an
   accidental hot-path inhabitant.** The decision (`docs/specs/2026-05-05-d3-view-layer-adaptation-design.md`
   §5, "Why this and not the alternatives") was that single-spin
   debounce makes the cost acceptable. The Part 3 plan's Phase 4
   doesn't engage with that argument; it treats kind-transition as
   incidental work to be moved off the hot path. The record contains
   no benchmark or measurement that overturned the D3 §5 reasoning;
   the plan's Phase 7 benchmark is the first quantitative budget the
   tree carries.

4. **Part 2's targeted signals are an incomplete surface.**
   `d2SetBlockKind`, `d2SetBlockAttr`, `applyStructural`, `undoD2`,
   `redoD2`, and `undoForBlock` do not emit `blocksChanged` /
   `blockInserted` / `blockRemoved`. Under the current binding (which
   is still on `d2DocumentChanged`) this doesn't matter — the
   debounced path catches everything. Under the *proposed* Part 3
   binding (which disconnects `d2DocumentChanged` and listens only to
   the targeted signals plus `documentReloaded`), state mutations
   produced by these six paths would not reach the view. The record
   contains no decision document for this gap. The Part 3 plan does
   not flag it.

5. **`structuralRowsInserted` / `structuralRowRemoved` are emitted
   *from the binding*, not from the document.** They are intra-leaf
   plumbing for the cursor-survival path. The Part 3 plan's Phase 3
   replacement does not preserve their emission contract; the existing
   cursor-survival path runs through them. If the Part 3 phase 3
   refactor renames or eliminates `onD2Changed`'s tail loop without
   re-emitting these signals from the new `onBlockInserted` /
   `onBlockRemoved` slots, the cursor-resolution path breaks silently.

6. **The "full walk per keystroke" framing in §5.1 of the v1.0 design
   is accurate; the framing is also incomplete.** The full walk *is*
   per-keystroke under D2's debounce. But it is not per-keystroke
   because of D2 — D2's debounce coalesces. It is per-keystroke
   because the diff source has no "what changed" payload, which forces
   the walk. Adding the targeted signals (Part 2) addresses the
   payload, but the underlying authority (the `IdList` + sibling
   maps + per-block `Buffer`s) is already a structured representation
   from which targeted notifications were always derivable. The v1.0
   design treats the full walk as the pathology and the targeted
   signals as the fix; the developmental record treats the full walk
   as a property the view kept inheriting (R2 → D2 Phase 11 → D3 →
   D3-corrective) without re-evaluation.

The user is the one writing the prescription. This document tries to
make sure the prescription is written against an accurate diagnosis.
