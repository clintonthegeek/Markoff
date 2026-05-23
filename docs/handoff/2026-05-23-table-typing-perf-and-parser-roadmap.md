# 2026-05-23 — Table typing perf analysis + parser-responsibility roadmap

> **For future agents asking:**
> - "Why is typing in tables fast/slow?"
> - "How do I measure per-keystroke cost in tables?"
> - "Should we regenerate the tree-sitter grammar to handle pipe_table_cell inline?"
> - "Should we move parseTable into C++?"
> - "What was the cascade fix in TableEditBinding?"
>
> This doc is your single entry point.

**Author context:** Triggered by a session where the user reported "typing inside table cells is still very laggy" despite the two earlier perf commits (`5ca96d5` cross-block highlight latency, `5c67777` parser inline-trigger fast-path). The session did a structural audit, measured, fixed three things, and confronted the deeper question of what the parser ought to own.

**Commits landed in this session:**
- `1568972` perf(live): table typing-perf measurement infrastructure + bench
- `2bd5f35` perf(live): short-circuit no-op TableEditBinding::applyCellEdit
- `2c0d263` perf(live): wire the dormant TableEditBinding re-entrance guard
- `634e813` live: zero stale UTF-8 byte fields in inlineSpansForCell projection

**Pre-existing context:**
- E4 tables spec: `docs/specs/2026-05-22-e4-tables-design.md`
- E4 tables plan: `docs/plans/2026-05-22-e4-tables.md`
- TableDelegate source: `libs/markoff-live/qml/delegates/TableDelegate.qml`
- TableEditBinding: `libs/markoff-live/src/TableEditBinding.cpp`

---

## TL;DR

Per-keystroke wall-clock for typing into a table cell, before vs after this session (offscreen test, 20 chars steady-state on the dogfood `tables_basic.md` fixture):

| Scenario | Before | After |
|---|---|---|
| Small 3×4 table, plain cell | 187 ms | **2.5 ms** |
| Large 4×6 table, bold cell | 510 ms | **9.8 ms** |
| Large 4×6 table, plain cell | 564 ms | **15.9 ms** |

The "hundreds of ms" user-perceived lag had a single primary cause: **a feedback cascade in the typing path** that ran the full edit+parse+rebuild pipeline ~20-40 times per user keystroke instead of once. Two fixes broke the cascade. A third was a correctness improvement that didn't move the bench but removed a latent foot-gun.

---

## The diagnostic story

### How a keystroke actually flowed (pre-fix)

The dispatched footprint of one user keystroke in cell (1, 0):

1. Cell `TextEdit.onTextChanged` fires. `_diffEdit(prev, current)` reconstructs (qtPos, removed, added) by prefix/suffix scan (`TableDelegate.qml:178-195`). Real edit detected, `applyCellEdit` called.
2. `applyCellEdit` runs `d2ApplyBufferEdit` on the table's block buffer, then `flushPendingD2Changed` synchronously.
3. `flushPendingD2Changed` → `onD2Changed` → `buildRecords` → re-fetch `inlineSpansFor(tableBlockId)` (cache miss, full reparse) → model emits `dataChanged` on the table row.
4. In QML, `parsedTable: parseTable(root.blockText)` re-evaluates. New JS object.
5. **Every cell's `cellText` binding re-evaluates** because `root.parsedTable` is a new object identity — 12 fires (small) / 24 fires (large) per keystroke.
6. Each cell's `TextEdit.text` gets re-pushed. Qt fires `onTextChanged` on each — even though string value is unchanged for cells the user didn't type into.
7. The QML guard at `TableDelegate.qml:453-468` checked `tableEditBinding.isApplyingTextUpdate()` to skip these. **But the flag was never set true** — see [Fix #2](#fix-2-wire-the-dormant-re-entrance-guard).
8. So each "no-op" rebind reached `_diffEdit` (returning `{qtPos: len, removed: 0, added: ""}`) and **called `applyCellEdit` anyway**.
9. Each no-op `applyCellEdit` ran `d2ApplyBufferEdit(byteOff, 0, "")` → bumped the block edit sequence → invalidated the inline parse cache → another `flushPendingD2Changed` → **another `onD2Changed`** → another cascade round.
10. Cascade terminated after ~20-40 iterations when Qt's same-string short-circuit or the `_previousText` snapshot finally caught up.

### Smoking gun

The unfinished re-entrance guard in the code. `TableEditBinding::isApplyingTextUpdate()` was declared, exposed to QML, called by the cell handler — but its backing flag `m_applyingTextUpdate` was *never set to true*. The header comment at `TableEditBinding.h:81-84` explicitly noted "C1 only exposes the getter; the value stays false until C2 wires the setter when the model.text → parsedTable rebuild → cell.text re-fire path starts producing onContentsChange echoes that must be filtered out." **C2 never wired it.** That's a dropped task from the E4 C2 work-unit, not a design flaw.

### What the parser was NOT

In my initial diagnosis (before instrumenting), I expected the parser to be the dominant cost — given that D4 deleted incremental parsing and every keystroke reparses the full document. **It wasn't.** Per-keystroke parser cost is 3 ms (small table) to ~14 ms (large), under 5% of the pre-fix total. The cascade was the real problem.

---

## What landed

### Measurement infrastructure (commit `1568972`)

- `libs/markoff-parser/include/markoff/parser/PerfProbe.h` — header-only singleton + scoped RAII helper + `MARKOFF_PERF_SCOPE("name")` macro. Lives in the parser layer because it's the lowest in the dep chain; visible to all higher libs.
- `TableEditBinding::perfTime(name, ms)` and `perfNote(name)` Q_INVOKABLE on `TableEditBinding` — lets QML/JS sites record timings the C++ scoped probe can't reach.
- Probe sites instrumented across `TreeSitterParser`, `InlineParseCache`, `LiveListModelBinding`, `TableEditBinding`, `InlineHighlighter`, `InlineHighlighterAttached`, and the JS `parseTable` + cell binding sites in `TableDelegate.qml`.
- Bench: `libs/markoff-live/tests/tst_live_render_table_typing_perf.cpp`. Loads `tables_basic.md`, focuses a cell, types 20 chars (after a 1-char warm-up), dumps `PerfProbe` accumulators. Asserts nothing about absolute timing.

**Running it:**

```bash
scripts/run-tests.sh --bin tst_live_render_table_typing_perf
```

Three slots: `per_keystroke_attribution_small_table_plain_cell`, `..._large_table_bold_cell`, `..._large_table_plain_cell`. Output includes per-stage cumulative time + call counts. **Timing has 3-5× variance run-to-run** under offscreen (CPU sharing); call counts are stable and are the better signal for cascade-detection.

### Fix #1 — no-op short-circuit in `applyCellEdit` (commit `2bd5f35`)

Drop the call if `removed == 0 && added.isEmpty()`. Five-line fix at the entry point of `TableEditBinding::applyCellEdit`.

**Impact:** per-keystroke `applyCellEdit` calls drop from 20-40 → 2 (1 real edit + 1 no-op caught by short-circuit). Per-keystroke `onD2Changed` cascade drops from 20-40 → 1. Wall-clock 15-30× faster across all three benches.

### Fix #2 — wire the dormant re-entrance guard (commit `2c0d263`)

Set `m_applyingTextUpdate = true` around `flushPendingD2Changed` with a `qScopeGuard` reset on return. Now the QML cell guard at `TableDelegate.qml:453` actually fires.

**Impact:** N cells become 1 — non-focused cells' rebinds now bail at the QML guard before reaching `applyCellEdit` + diff + the noop_skip path entirely. Bench: small table 12 ms → 2.5 ms; large table 17 ms → 9.8 ms.

### Fix #3 — zero stale UTF-8 byte fields in `inlineSpansForCell` (commit `634e813`)

`inlineSpansForCell` re-projected `charOffset` and parent ranges into the cell-local frame but left `utf8Offset`/`utf8Length` at their absolute block-buffer values. Those bytes shift when sibling cells grow/shrink, so unchanged cells received spans that compared unequal under `SourceSpan::operator==` even though their visible content didn't change — defeating `InlineHighlighterAttached::setSpans`'s equality short-circuit.

**Impact on bench: zero.** Some *other* `SourceSpan` field is still varying for unchanged cells across keystrokes. Likely candidates: parent ranges in delimiter spans, or span ordering from tree-sitter's non-incremental reparse. Identifying the remaining differing field is future work. The commit removes one known source of inequality regardless — `highlightBlock` doesn't read utf8 fields, the find-pass adapter never touches `inlineSpansForCell`, so leaving stale absolute byte offsets in cell-frame spans was a latent foot-gun for any future consumer.

---

## What's left after these fixes

Per-keystroke wall-clock floor is now bounded by:

- `onD2Changed` cascade itself (~4-6 ms on large tables) — synchronous full-document model rebuild + parser reparse of the table block + 5 `inlineSpansFor` cache lookups (1 miss = reparse, 4 hits).
- Per-cell binding cascade noise (~3-5 ms aggregate) — `inlineSpansForCell` × N cells, plus the rehighlight cascade for the ~75% of cells whose post-projection spans still compare unequal (per Fix #3 caveat).

Neither yields cleanly to a tactical fix. The architectural fixes that would actually move them further are the subject of the next section.

---

## The deeper question: should the parser take on more?

The user's framing: "is it just a tidying up? will it let us clean and simplify the higher level of the markoff stack if the parser assumes responsibility for what it ought to?"

This is the real architectural question. Three different things "fix it at the parser" could mean, with very different scopes and very different "does this simplify upper layers" answers.

### Option A — Wrap `pipe_table_cell` in an `inline` node in `grammar.js`

The smallest change. Fork tree-sitter-markdown (`libs/markoff-parser/src/vendor/tree-sitter-markdown/tree-sitter-markdown/grammar.js`) so cell content goes through the same `inline` node path that paragraph and heading content go through. The custom `pipe_table_cell` recognition in `collectInlineRanges` (`TreeSitterParser.cpp`) plus the trigger-char fast-path (`5c67777`) would both go away — one inline-parser invocation per block reparse instead of N.

**What it simplifies upstack: nothing.** The parser still emits the same `SourceSpan` flat list. `inlineSpansFor`, `LiveListModelBinding`, `InlineHighlighter`, the QML delegate — all unchanged. Only the parser internals get cleaner.

**Pros:**
- Removes the workaround the code already acknowledges as one.
- Eliminates the false-negative risk: any new inline construct we add (math `$`, comment `%%`, etc.) requires remembering to extend the trigger set. Easy to forget.
- Saves modest per-keystroke parser ms — but after Fixes #1+#2, parser cost is <5% of the per-keystroke total. Invisible in the bench.

**Cons:**
- We become forkers of tree-sitter-markdown. The upstream design choice to keep `pipe_table_cell` as a leaf was deliberate.
- Ongoing maintenance: rebase against upstream, ensure regenerated parser-c output is reproducible.
- Risk of subtle output drift (different span counts/ordering/parent ranges) that breaks existing tests in ways that aren't obviously the right behaviour.

**Verdict:** Tidying, not simplification. Worth doing as a hygiene pass *if* upstream is willing, low priority otherwise.

### Option B — Surface a structured `Table` AST through the public parser API

The parser commits to "tables have rows and columns, here they are" as part of its public contract. `Markoff::Document` (or `MarkoffDocument`) exposes `tableAt(blockId)` returning `{ headers, alignments, body[][], cellCharRanges[][], parseOk }` — already structured, validated, byte-range-mapped.

**What it simplifies upstack — substantially:**

- The 128-line `parseTable()` JS function in `TableDelegate.qml` goes away entirely.
- `cellCharRanges` derivation in QML goes away — the parser owns it.
- `TableEditBinding::inlineSpansForCell` becomes trivial (or vanishes) — if the parser hands back per-cell spans pre-bucketed, no filter/reproject step needed. The "stale `utf8Offset`" Fix #3 becomes moot.
- `TableEditBinding::applyCellEdit`'s `(r, c, cellQtPos) → block-buffer byte offset` translation becomes a parser-side lookup, not delegate-computed.
- The QML cell `cellText` binding reads from the parser's pre-computed array; no JS derivation per keystroke.

This is what the user's question was actually pointing at: **the parser already knows everything we currently re-derive in the delegate.** The duplication is real and B is the way to remove it.

**Pros:**
- Genuine simplification at upper layers. ~200 lines of QML JS + ~50 lines of TableEditBinding C++ collapse to "delegate reads from parser-provided structure."
- **Generalizable to the rest of L8.** Callouts (E3d), frontmatter properties (E4 successor), embeds (E3c) all want structured per-block access. B establishes the pattern for the L8 interactive-block family, not just for tables. Doing it well for tables means callouts inherit the same shape.
- Removes the entire class of "JS parser vs C++ parser disagree about cell shape" bugs.

**Cons:**
- **Expands the public parser contract.** `CLAUDE.md`'s banner explicitly says "don't draft speculative freeze specs — wait for port evidence." Adding `Table`/`TableCell` types to the parser's stable surface is exactly the kind of commitment the freeze plan is trying to avoid prematurely.
- More API surface to deprecate or migrate if parser strategy changes (e.g., swapping out tree-sitter for something else, or D5 collab evolving the per-block model).
- The parser must commit to a parse-error tolerance policy for malformed tables — currently the delegate's `parseTable` quietly degrades to a diagnostic. With parser-side structure, the parser must decide "this is a Table with N rows" vs "this isn't a Table, demote." Moving that policy upstream is a real design decision.
- Requires care at the Source ↔ Live boundary: when Source-mode edits produce an in-flight malformed table mid-keystroke, the parser's structured output must not jitter so much that the rendered grid blinks.

**Verdict:** This is the option that actually answers "yes, the parser ought to assume that responsibility." But it's a forward-looking architectural commitment, not a perf fix. The **right time to do it is when an L8 sibling (callouts, frontmatter properties) hits the same delegate-side re-derivation pattern**, so the cost amortizes across the whole interactive-block family.

### Option C — Reintroduce tree-sitter incremental parsing

Per-block CRDT buffer → per-block TSTree → `ts_tree_edit(blockTree, edit)` instead of full reparse per keystroke. Reverses the D4 decision (`docs/specs/2026-05-07-d4-parser-scope-reduction-design.md`).

**What it simplifies:** nothing structural — changes how the parser layer is shaped internally. Upper layers see the same outputs, produced ~10× faster on edits.

**Pros:**
- Best per-keystroke perf available. Would drop `onD2Changed` cascade's parser cost from ~12 ms (large table) to sub-millisecond.
- Scales to large documents where full reparse becomes noticeable (1000+ blocks).

**Cons:**
- D4 deleted the incremental pipeline deliberately. Reintroducing is a real architectural revisit, not tactical.
- Inline-grammar trees (multiple per block, region-based) make incremental edits messier than block trees.
- After Fixes #1+#2 we're at 2.5-16 ms per keystroke — the ceiling C breaks isn't currently load-bearing.

**Verdict:** Roadmap item. Revisit when per-keystroke cost is the bottleneck again.

---

## Recommendation

**Don't do any of A/B/C reactively.** Priority order if forced to commit:

1. **B becomes the right call** when a second L8 interactive block (callouts or frontmatter properties) reaches the implementation stage and the team is about to redo the "JS-side per-block tokenizer in the delegate" dance a second time. At that point the duplication isn't table-specific anymore — it's a pattern — and the parser is the natural home for it. The cost of expanding the parser's contract is justified by amortization across the family.

2. **A is worth doing opportunistically** if the trigger-char fast-path ever causes a real bug (e.g., we add a new inline construct and forget the trigger set). Otherwise, hygiene.

3. **C is a roadmap item, not a current concern.** Revisit when per-keystroke cost regresses or when document size pressure makes full reparse painful.

The honest framing: **the parser already shoulders the responsibility it ought to have for the contract it currently exposes** (block boundaries + flat inline spans). What the table delegate is doing wrong is re-deriving cell structure that the parser computed and threw away. Fixing that means changing the contract — that's B, and it's a real commitment, worth aligning with broader L8 work rather than a one-off table fix.

---

## Open thread: the remaining `setSpans` non-noop cells

Fix #3 zeroed `utf8Offset`/`utf8Length` in cell-projected spans expecting it would lift the `InlineHighlighterAttached::setSpans` noop short-circuit rate from ~25% to ~100%. It didn't move the needle. The noop count is stable run-to-run (114/304/190 across the three bench slots), meaning ~75% of cells still produce spans that compare unequal across keystrokes even though their visible content didn't change.

**Hypothesis (untested):** parent ranges (`parentCharStart`/`parentCharEnd`) for delimiter spans are subtly off in a way the current re-projection doesn't catch, OR tree-sitter's parse output reorders spans non-deterministically when input bytes shift (`QList<SourceSpan>::operator==` is order-sensitive).

**To investigate:** dump full span lists for an unchanged cell across two consecutive keystrokes; diff. The setSpans probe in `InlineHighlighterAttached.cpp` makes this easy to hook — replace the `note("noop")` line with a per-call dump under a debug flag.

**Why it's not blocking:** the post-fix-#2 per-keystroke cost is acceptable. The remaining cascade is harmless noise (each setSpans is ~10 us; total per-keystroke spans cost is ~3 ms on the large table). Worth doing if a future session is investigating other binding-cascade artifacts and the answer is cheap to find.

---

## Pointers

- Bench: `libs/markoff-live/tests/tst_live_render_table_typing_perf.cpp`
- PerfProbe header: `libs/markoff-parser/include/markoff/parser/PerfProbe.h`
- Probe sites:
  - `libs/markoff-parser/src/TreeSitterParser.cpp` (parse + buildSpanMap + inlineSpansFor)
  - `libs/markoff-core/src/InlineParseCache.cpp` (spansFor + hit/miss counters)
  - `libs/markoff-live/src/LiveListModelBinding.cpp` (onD2Changed + per-row inlineSpansFor)
  - `libs/markoff-live/src/TableEditBinding.cpp` (applyCellEdit + inlineSpansForCell)
  - `libs/markoff-live/src/InlineHighlighter.cpp` (setInlineSpans + highlightBlock)
  - `libs/markoff-live/src/InlineHighlighterAttached.cpp` (setSpans + noop counter)
  - `libs/markoff-live/qml/delegates/TableDelegate.qml` (JS parseTable + cellText.eval)
- Dogfood fixture: `libs/markoff-live/tests/fixtures/tables_basic.md`
- Prior perf commits (symptom-level fixes from earlier session):
  - `5ca96d5` perf(live): cut TableDelegate cross-block highlight latency
  - `5c67777` perf(parser): pipe_table_cell inline-trigger fast-path
- E4 living docs:
  - `docs/specs/2026-05-22-e4-tables-design.md` (the spec)
  - `docs/plans/2026-05-22-e4-tables.md` (the plan)
  - `docs/e-arc/e-arc-status.md` (status board)
