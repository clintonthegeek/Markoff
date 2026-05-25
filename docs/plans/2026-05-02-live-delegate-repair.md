# Live Preview — Delegate-Architecture Repair Plan

**Date authored:** 2026-05-02
**Branch:** `exploration/new-foundation` (worktree `.worktrees/foundation-exploration/`)
**Branch tip when authored:** `1ab5fdd`
**Status:** OPEN — for a fresh-context implementer to pick up. The controller of the previous session ran out of useful judgment after a long debugging arc and asked for a fresh take.

---

## TL;DR

The Phase-2 live preview widget (`libs/markoff-view-qml/qml/LiveView.qml` + per-block delegates) is **architecturally correct but functionally janky**. A long debugging arc through 2026-04-30 → 2026-05-02 surfaced and partly fixed a bug class rooted in (a) divergent block-enumeration between foundation and view-qml, (b) a hole feature whose design predates the C-1..C-9 fixes, (c) per-keystroke performance landmines, and (d) ambiguous Enter semantics that don't match real-user mental models for multi-line "paragraphs." The architecture decision has been **explicitly reaffirmed** by the user: per-block delegates stay (see §1). Your job is to finish the repair, not pivot to a single-`QTextDocument` model.

Read this doc, then `git log --oneline -25` to see the recent arc. **Do not assume the existing comments in the code are accurate** — many describe intent that the code doesn't actually deliver, or rationalize prior workarounds that have since been replaced.

---

## 1. Architectural Decision: per-block delegates stay

The user evaluated, on 2026-05-02, two coherent answers to "do we need separate delegates per block kind":

- **Answer A — consolidate text-bearing delegates** into one generic `TextBlockDelegate` parameterized by `kind`.
- **Answer B — drop ListView+delegates entirely**, render the whole document as one `QTextEdit` with `QTextBlockFormat`-driven block styling, mirroring `SourceEditor.qml`'s pattern.

Both were proposed. The user **rejected B** with explicit reasoning:

> Interactive blocks are not a distant idea. They're the immediate next step once this stops being janky. Overlays sound hacky to me — Obsidian and Notion do them because they're written in hypertext. We're writing in QML. I've been fighting against `QTextDocument`'s limitations the whole time, first in QWidgets, now in QML. I expected QML to be **better** at creating bespoke complex forms. We will prove it is. So we will NOT remove the delegate system.

This decision is binding for any agent picking up this plan. **Do not propose a single-`QTextDocument` rewrite.** The forward path is: keep delegates, fix the bug class.

### Implications

- The "interactive block" use case (videos, embedded mermaid, math via `jkqtmathtext`, callouts, drag handles, per-block toolbars) is treated as a **first-class near-term requirement**, not theoretical option value.
- Cross-block focus, edit, and selection coordination must therefore be made *correct*, not eliminated.
- Plugin authors are an audience: the per-delegate model is the extension point.

The decision deliberately accepts the architectural cost of cross-block coordination in exchange for arbitrary per-block QML composability.

---

## 2. Branch context (what just happened)

The 2026-04-30 → 2026-05-02 arc, in commit-graph order (`git log --oneline a463bca..HEAD`):

- **Phase-2 hole feature v0** (`a463bca`, `ef0433b`, `030d581`) — implemented "empty-paragraph hole" with reify-on-first-keystroke pattern. Dogfood surfaced five distinct failure modes (visual double-spacing, character scramble during fast typing, arrow-keys-destroy-hole, focus-goes-nowhere, source-state-leak). **Reverted** at `cfbc30f`.
- **Hole feature v1 spec/plan** (`9fc0b83`, `48c1f3e`, `7c499d3`, `106ad60`) — IME-preedit-pattern redesign. Spec at `docs/specs/2026-05-01-live-projection-layer.md`. Implementation plan at `docs/plans/2026-05-01-live-projection-layer.md`. **Both documents embed assumptions that may no longer be valid post-C-7** (foundation/view-qml block-enum alignment) and post-Answer-B-rejection. Read them critically.
- **Hole feature v1 implementation** (`c96b71d`, `bc0ca6a`, `bcf64cd`) — T17–T23 of the spec. Layer + model + delegate + LiveView wiring + save-flush.
- **Spot-fixes for first dogfood** (`bdae435`, `6d61604`, `7f5d08f`) — focus race for `onHoleCreated`, drop idle-commit timer, preserve focused TextEdit content on parse-back (the **`activeFocus` skip** described in §3.4 below), local TextEdit truncation on mid-block Enter.
- **Cold review** (no commit; output preserved in conversation transcript and §3 of this doc) — independent agent went in blind, found the architecture has structural problems beyond the hole work.
- **C-1: foundation `Document::topLevelBlocks()`** (`742e11b`) — added a structured tree-sitter top-level walker so view-qml can stop using regex.
- **C-2/C-3: view-qml replaces BlockWalker, source-faithful BlockRecord** (`09f860f`, `a71dd40`) — eliminated the regex BlockWalker; `BlockRecord.text` now equals raw block source bytes (was previously display-stripped); heading/code-block delegates got missing `Component.onCompleted` initial-text fix.
- **C-5: focus routing for mid-block Enter** (`0b96be6`) — `LiveStructuralKeyHandler::focusAfterStructuralEdit` signal + QML wiring.
- **C-7: foundation/view-qml block enumeration alignment** (`a7117a0`) — replaced foundation's `scanTopLevelBlockRanges` (regex) with `parsed->topLevelBlocks()` consumption. Eliminated the "typing duplicates into another paragraph" bug.
- **C-8: focus race fix via `rowsInserted` gate** (`30249aa`) — `LiveListModelBinding::requestFocusOnRowInserted` Q_INVOKABLE + `focusRowReady` signal; replaces `Qt.callLater(itemAtIndex)` polling on stale model state.
- **C-9: inline highlighter per-line offset** (`1ab5fdd`) — `highlightBlock` now translates char ranges through `currentBlock().position()`. No more "bold repeats every visual line."

At branch tip (`1ab5fdd`): 107/107 fast-tier tests pass with `ctest -E 'tst_realistic|tst_benchmark|tst_view_qml_live_view_qml'`. The excluded `tst_view_qml_live_view_qml` had a baseline 10 pass / 2 fail / 2 skip — `09f860f` shifted that to 9/3/2 (one test now passes that didn't, two new failures from dropping image-only-paragraph detection). Fixing the excluded baseline is a separate task.

---

## 3. Bug inventory (what's still broken)

Catalog of what's currently wrong, ordered by severity. Each entry is a *hypothesis* rooted in evidence; verify before fixing.

### 3.1. (Critical, may be fixed) Mid-block-Enter caret lands on the wrong row

**Symptom (dogfood, 2026-05-02):** "Pressing Enter in the middle of a paragraph splits the paragraph properly, but moves the caret to the beginning of the *following* paragraph (the one which had immediately followed the one which was split)."

**Hypothesis (pre-C-8):** The QML `Connections` listener for `LiveStructuralKeyHandler::focusAfterStructuralEdit` was polling `listView.itemAtIndex(viewRow)` *before* the parse round-trip had updated the model — so it found the delegate that *was* at that row position (the next paragraph) and focused it.

**Fix attempt (C-8, `30249aa`):** Routed both `holeCreated` and `focusAfterStructuralEdit` through `LiveListModelBinding::requestFocusOnRowInserted`, which gates focus on the model emitting `rowsInserted` for the expected range.

**Status:** Unverified. The user should dogfood after `1ab5fdd` and confirm. If still broken, the hypothesis is wrong and we need a fresh diagnosis — possibly the parse-back's diff produces a different row layout than `expectedRow = blockIndex + 1` predicts.

### 3.2. (Critical, may be fixed) End-of-paragraph Enter lands at start of next paragraph

**Symptom (dogfood, 2026-05-02):** "Pressing Enter at the end of a paragraph puts the caret at the start of the first line of the next paragraph."

**Hypothesis:** Same root cause as 3.1 for the EOB-hole creation path. The hole's `holeCreated(viewRow)` fires synchronously, `LiveProjectionLayer::insertHoleRow` updates the model synchronously via `beginInsertRows`/`endInsertRows`, so `rowsInserted` should fire before the QML listener runs — but if ListView hasn't incubated the new delegate yet, the listener still fires `focusAtPos` on a stale delegate.

**Fix attempt (C-8, `30249aa`):** Same gate. The hole path now also routes through `requestFocusOnRowInserted`. The QML retry loop on top of `focusRowReady` handles incubation lag.

**Status:** Unverified. Dogfood needed.

### 3.3. (Critical, unfixed) Latency ~300ms during typing, variable

**Symptom (dogfood):** "Horribly laggy. Latency is close to 300ms, varies widely."

**Suspected causes** (all pre-existing, unprofiled):

1. **`InlineFormatHighlighter::rebuildSpans` constructs a fresh `Markoff::TreeSitterParser` per delegate per source change** (`libs/markoff-view-qml/src/InlineFormatHighlighter.cpp:99-108`, was identified in cold review finding #12). For a document with N visible blocks, that's N tree-sitter parser instantiations per parse round-trip on top of the foundation's own parse. Each fresh parser does setup + a full block parse on the block's source.
2. **Foundation's `ParsePool` does a whole-document full parse on every change** (CLAUDE.md performance §). Tree-sitter's incremental API exists but only the block tree appears to use it via `parseIncremental`; whatever happens upstream for inline trees may also be re-running. Needs profiling.
3. **`LiveEditBinding::onContentsChange` does `m_document->toMarkdownUtf8()` per keystroke** (`LiveEditBinding.cpp:197-211`, cold review finding #11). Two whole-document allocations per character.
4. **`LiveContextMenuHandler.blockTexts` rebuilds the full block-text array on any `rowCount` change** (`LiveView.qml:130-138`, cold review finding #14).

**Investigation owed:** profile under `perf record` while typing into a long doc. Compare CPU breakdown with a tiny doc to isolate per-character vs. per-document cost. Then fix targeted hotspots; do NOT optimize blindly. See the never-written but referenced `docs/handoff/2026-04-28-post-poc-perf-SESSION-BRIEF.md` (file may not exist; the CLAUDE.md just points to it as the intended next step).

### 3.4. (High, unfixed; latent invariant violation) `activeFocus` skip in `onBlockTextChanged`

**Site:** `libs/markoff-view-qml/qml/delegates/ParagraphDelegate.qml`, line ~248-266.

**What the code does today:**

```qml
onBlockTextChanged: {
    if (textEdit.activeFocus) return  // skip parse-back text replay on focused row
    root.m_applyingModelBuffer = true
    editBinding.setModelText(root.blockText)
    root.m_applyingModelBuffer = false
}
```

**Why it was added (`6d61604`):** Without this skip, when the user types faster than the parse round-trip (~30-100ms), the parse arrives carrying a stale snapshot of the row's text. `setModelText` calls `setPlainText` which clobbers the user's in-flight keystrokes. Symptom was visible character scramble in the *focused* delegate.

**Why it's wrong:** It violates `libs/markoff-view-qml/CLAUDE.md` invariant #1 ("Single source of truth: MarkoffDocument only"). The cold review's finding #10 also flagged this:
> When focus eventually leaves, the comment promises "any drift is reconciled on next entry" — but `onBlockTextChanged` only fires on a *change* of `blockText`. If parse-back set `blockText` to a value the focused TextEdit already happens to display, no later signal fires, and a true drift (e.g. parser-canonicalised whitespace) is never reconciled.

**Proper fix (suggested but unimplemented):** Compare `MarkoffDocument::editSequence()` against `parseSequence()` at the moment the parse-back fires for this row. If `editSequence > parseSequence` (i.e. there are local edits queued that haven't been parsed yet), skip the row's text update — the parse is genuinely stale relative to user intent. If `editSequence == parseSequence`, apply normally — the parse output IS the canonical state. This restores the invariant while still avoiding the scramble.

**Open design question:** Where to surface `editSequence`/`parseSequence` to the delegate? Options:
- Promote both to `Q_PROPERTY` on `EditorBackend` or a new helper.
- Pass an "is-stale-for-row" boolean through the model's TextRole change signal.
- Have the binding suppress the dataChanged emit for stale rows.

### 3.5. (High, unfixed) Inline-format predictions wholesale-cleared on every parse

**Site:** `libs/markoff-view-qml/src/LiveProjectionLayer.cpp:295-318` (`onParseUpdated` does `m_inlinePredictions.clear()`).

**Symptom:** Inline styling visible flicker. While the user is mid-word in `**bo|`, the predicted bold disappears the moment the parser confirms (or doesn't), because the layer wholesale-clears all predictions on parse-back. The producer (`InlineFormatHighlighter::publishInlinePredictions`) only re-runs on `setSource`, which fires when `m_source != src`.

**Why the code is shaped this way:** The wholesale-clear is justified in a code comment as "the next character typed will republish anything still applicable." That's only true during steady-state typing. During pauses, after deletes, after structural edits, the predictions are gone and the text renders unstyled until the next keystroke triggers a republish.

**Open design question:** The fix shape:
- Re-run the source-scan on `parseUpdatedAt` for the current source (publishes predictions then immediately runs the highlighter on the same data).
- Or: don't clear; reconcile per-row (drop predictions whose ranges are covered by parser-confirmed inline nodes; keep the rest).
- Or: producer-side, recompute on every parse cycle without checking `m_source != src`.

### 3.6. (Medium, unfixed) Hole feature's `commitBlockHole` listener leaks across unrelated inserts

**Site:** `libs/markoff-view-qml/src/LiveProjectionLayer.cpp:155-177` (`commitBlockHole` installs a one-shot connection to `LiveBlockModel::rowsInserted` waiting for "the first range that covers `expectedRow`"; cold review finding #8).

**Why it's fragile:** The connection persists until *some* `rowsInserted` covers the expected row — even if that insertion is unrelated to the committed hole (a remote edit, a different local edit, etc.). If a user types again before the parse arrives and commit isn't called, the original connection still sits there, and a subsequent unrelated insert may trigger `holeReified` at a now-stale `expectedRow`.

**Open design question:** Make the listener parse-sequence-tagged. The hole snapshots the `parseSequence` at commit time; the listener only fires on `rowsInserted` that originate from the parse cycle whose sequence is `> commit-time sequence`. Requires the binding to expose "which parseSequence is this `applyOps` from."

### 3.7. (Medium, unfixed) Conceptual: what is a "paragraph" / what does Enter mean

**The ambiguity the user raised:** The user's test files use explicit newlines for visual line breaks within a "paragraph" (consecutive non-blank lines, no blank-line separator). Tree-sitter sees this as **one** block. The current Enter semantics:

- Enter at end of last line of a multi-line block (qtPos == block-source length) → triggers EOB hole (a *paragraph break*).
- Enter elsewhere mid-block → splits into two real paragraphs.
- The user's mental model: Enter at end of any line within a paragraph should be a *soft break* (continue typing on next line within the same paragraph). Only Enter twice should be a paragraph break.

**Open design question (load-bearing):** Two coherent models:

- **Model A (current):** Single Enter creates a paragraph break (with hole feature bridging visual feedback). Matches "outliner" / Notion-style mental model.
- **Model B:** Enter inserts `\n`, period. Two consecutive Enters create `\n\n`, which the parser splits as a new paragraph. Matches plain-text-editor mental model.

Model B is structurally simpler (the hole feature ceases to be needed for *this* purpose, though it might still be useful for other cases like empty list items, empty code-fence interiors, etc.). Model A as currently implemented is what the user has been testing.

**Resolution required from the user, NOT predetermined.** This affects the rest of the plan: if Model B wins, the hole feature gets de-scoped or repurposed for non-paragraph holes only. Do not silently pick a model — surface this as the first question to the user before implementing.

### 3.8. (Medium, unfixed by `30249aa`) ListView delegate incubation timing

The C-8 fix gates focus on `rowsInserted` (model state confirmed) but still uses a bounded retry loop in QML (`Qt.callLater` × 10 attempts) for ListView delegate incubation. This is a known QML pattern but is timing-bet code. On a fast machine the first attempt may already see the right delegate; on a slow machine 10 attempts might still not be enough. The cross-cutting CLAUDE.md invariant says "no QML focus chain workarounds" — `Qt.callLater` polling is exactly that, just dressed differently.

**Open design question:** Is there a deterministic "delegate-for-this-row-is-now-incubated" signal we can listen for in QML? `ListView.itemAdded` / `ListView.onAdd` exists but operates per-delegate, not per-index. May need a custom incubator.

### 3.9. (Low) ListView mismatch fallback in `LiveListModelBinding`

The binding still has the "fall back to default-constructed BlockAnchor when records.size() > anchors.size()" code path (`LiveListModelBinding.cpp` ~line 142, the `if (i < anchors.size())` check). After C-7, this should never fire — both lists come from the same `topLevelBlocks()`. If it ever does fire, it's a real bug, not a coexistence issue. Either log a warning and drop the offending records (they have no valid anchor anyway), or assert. Currently it silently default-constructs which causes diff misalignment (cold review finding #6).

### 3.10. (Low) `BlockKind::Image` retired but image-only paragraphs render as plain text

C-2 dropped image-only-paragraph detection; the image syntax `![alt](url)` now renders as plain markdown text (with `InlineFormatHighlighter` coloring the markers). Two excluded-tier tests now fail because of this: `delegates_consume_theme_colors`, `selection_highlight_appears_on_hr_and_image`. Either:

- Add image-only-paragraph detection back in the converter (`BlockWalker::walk` over `topLevelBlocks()`), re-emit `BlockKind::Image`, restore `ImageDelegate.qml` rendering.
- Delete the two tests (they probe a removed feature) and accept text-rendering of image syntax for v1.

User has expressed a strong preference for delegate-based rendering of complex content; per §1, image rendering should likely come back as a delegate, but that's a feature decision separable from this plan's bug-fix scope.

### 3.11. (Low) `LiveContextMenuHandler.blockTexts` O(N) rebuild

Cold review finding #14. Not blocking. Worth a one-shot cleanup pass during perf work.

---

## 4. Open design questions (need user / fresh-context resolution)

Listed in approximate order of how upstream they are. Several block large amounts of downstream work.

### Q1. Enter semantics — Model A vs Model B (see §3.7)

This must be resolved first. It changes:
- Whether the hole feature stays as paragraph-break bridge (Model A) or is de-scoped (Model B).
- How `LiveStructuralKeyHandler::tryHandle` shapes the Enter code path.
- The user-visible test plan for "press Enter, what happens."

If Model B wins: dropping the hole feature from the EOB Enter path removes a large amount of code (`LiveProjectionLayer`'s hole API, `ParagraphDelegate.qml`'s hole branch, `LiveView.qml`'s `onHoleCreated`/`onHoleDropped` Connections, the `requestFocusOnRowInserted` path becomes used only for mid-block Enter). The projection layer keeps its prediction half (inline-format speculation, fence speculation).

### Q2. Cycle-guard correctness for fast typing (see §3.4)

How to fix the `activeFocus` skip without re-introducing the visible scramble. The proposed `editSequence vs parseSequence` solution needs to be designed concretely:
- Where to expose the sequences (Q_PROPERTY on EditorBackend? signal argument?).
- Per-row staleness vs. document-wide staleness.
- Whether to use the same mechanism for kind-changes (e.g. `# ` typed at start of paragraph → heading kind change while user is typing). Currently kind-changes are model-level and trigger `applyOps` which destroys+recreates the delegate. The per-keystroke kind flip is its own race.

### Q3. Per-block parser cost — what replaces `InlineFormatHighlighter`'s fresh `TreeSitterParser`?

Profiling required. Plausible designs:
- Single document-wide tree-sitter parser owned by the binding; per-delegate highlighter queries it for spans intersecting its block range.
- Spans pre-computed by `Markoff::Document::topLevelBlocks()` (extend it) and made available per-block.
- Cache parsers per delegate, only rebuild on source change *and* with incremental edits.

### Q4. Selection / hit-testing across delegate boundaries

Currently uses the `MouseArea.hit()` pipeline in `LiveView.qml` (lines ~190-263) which the cold review noted has a "DO NOT modify the math" comment. That's a smell — load-bearing math should have a test, not a warning. Verify the math has spec-pinned tests; if not, add them. Especially needed before the per-block-interactivity work in Q6.

### Q5. Per-delegate focus protocol design

Currently a tangle of `Qt.callLater` retries, `Connections { onFocusRestoreRequested }`, `notifyFocused`, `focusRowReady`, `forceActiveFocus`, `m_firstInsertPending`, etc. Each was added for a specific symptom. The cold review noted (finding #9) this is exactly the "QML focus chain workaround" CLAUDE.md says not to do.

A coherent design would have one canonical "focus is on block N at qtPos M" representation (probably backed by `Session::primarySelection`, per CLAUDE.md cross-cutting invariant #4) and one way to set it. Today there are multiple paths and they coordinate by accident.

### Q6. Interactive blocks — what's the per-delegate API contract?

Per §1, this is the immediate next feature category once the bug class is closed. Open sub-questions:
- Embedded video / web-view blocks: how does focus interact (a video player has its own focus model)?
- Drag handles for block reordering — does the model support it? (Today `LiveBlockModel::applyOps` consumes parser-output diffs; user-initiated reordering would need to translate the move into a CRDT operation that produces the same diff.)
- Per-block toolbars on focus — is this a delegate concern or a `LiveView.qml` overlay concern?
- How does the hole feature (if it survives Q1) interact with non-paragraph block kinds?

### Q7. The `tst_view_qml_live_view_qml` baseline

10 pass / 2 fail / 2 skip pre-C-2; 9/3/2 post-C-2 (one new pass, two new fails from the dropped Image kind, see §3.10). Either accept the regression (delete two tests), or restore Image rendering via the converter. Should be resolved alongside Q1/Q6.

---

## 5. Suggested sequencing (non-binding)

The next agent can disagree. This is a starting point.

1. **Verify §3.1 and §3.2 are actually fixed by C-8 (`30249aa`).** Manual dogfood. If yes, mark closed. If no, fresh diagnosis.
2. **Resolve Q1 with the user.** Don't proceed to anything else until Enter semantics are pinned.
3. **Fix §3.4 (`activeFocus` skip → editSequence/parseSequence).** This is a correctness restoration and unblocks the kind-change-during-typing class of bugs.
4. **Fix §3.5 (inline prediction wholesale clear).** Visible flicker; bounded scope.
5. **Profile §3.3 (latency).** Collect data before implementing fixes.
6. **Address §3.3 hotspots one at a time, measuring after each.**
7. **Address §3.6, §3.8, §3.9, §3.10, §3.11** as cleanup once headline issues are resolved.
8. **Then turn to Q5 (focus protocol redesign) and Q6 (interactive blocks).**

---

## 6. References

### Code citations (commit-pinned where possible)

- View-qml live mode root: `libs/markoff-view-qml/qml/LiveView.qml`.
- Per-block delegates: `libs/markoff-view-qml/qml/delegates/{Paragraph,Heading,CodeBlock,Image,HorizontalRule}Delegate.qml`.
- Block model + binding: `libs/markoff-view-qml/{include/markoff/view/qml,src}/LiveBlockModel.{h,cpp}` and `LiveListModelBinding.{h,cpp}`.
- Per-delegate edit binding (cycle guards): `libs/markoff-view-qml/src/LiveEditBinding.cpp`.
- Structural key handler: `libs/markoff-view-qml/src/LiveStructuralKeyHandler.cpp`.
- Inline highlighter: `libs/markoff-view-qml/src/InlineFormatHighlighter.cpp`.
- Speculative fence controller: `libs/markoff-view-qml/src/LiveSpeculativeFenceController.cpp`.
- Projection layer: `libs/markoff-view-qml/{include,src}/markoff/view/qml/LiveProjectionLayer.{h,cpp}`.
- Block converter (post-C-2): `libs/markoff-view-qml/src/BlockWalker.{h,cpp}` (now a thin shim over `Document::topLevelBlocks()`).
- Foundation parse pipeline relay: `libs/markoff-core/src/MarkoffDocument.cpp` lines 25-43 (the `parseReady` lambda).
- Foundation block-anchor computation: `libs/markoff-core/src/BlockAnchorComputation.cpp` (uses `parsed->topLevelBlocks()` post-C-7).
- Parser top-level walker: `libs/markoff-parser/src/TreeSitterParser.cpp` (`collectTopLevelBlocks`, `classifyTopLevelKind`, `fillFencedCodeFields`).
- Parser top-level API: `libs/markoff-parser/include/markoff-parser/Document.h` (`TopLevelBlock` struct + `Document::topLevelBlocks()`).

### Specs and prior plans (read **critically** — they predate C-7 and the Answer-B-rejection)

- `docs/specs/2026-04-29-live-render-design.md` — Phase-2 v0 walking-skeleton design. Original architecture intent.
- `docs/specs/2026-04-30-live-editing-design.md` — ten editing invariants. §4 invariants are referenced repeatedly above.
- `docs/specs/2026-05-01-live-projection-layer.md` — projection-layer + hole spec. The §3 (hole lifecycle) embodies Model A from §3.7. The v1 redesign (§3.2-§3.6) reflects post-revert thinking but predates dogfood-surfaced bugs.
- `docs/plans/2026-05-01-live-projection-layer.md` — plan that produced commits `c96b71d`..`bcf64cd`. Stage 4 marked as work-in-progress in this plan's terminology.
- `docs/handoff/2026-05-01-projection-layer-stage4-redesign-SESSION-BRIEF.md` — useful narrative on the v0 failure modes and v1 design rationale; written before C-1..C-9.
- `libs/markoff-view-qml/CLAUDE.md` — library guide. Editing invariants §; Architectural invariants §. The `activeFocus` skip in §3.4 above violates invariant #1 and #2; flag this discrepancy when fixing.
- `libs/markoff-core/CLAUDE.md` — foundation guide. Documents the `parseUpdated` signal shape and the BlockAnchor staleness-for-one-cycle behavior that's now anchored to `topLevelBlocks()`.

### Cold review (no commit; transcript-only, summary preserved)

A blind code review run on 2026-05-02 found 15 issues. The headline three were:

1. Heading + code-block delegates rendered empty on first incubation (fixed in `a71dd40`).
2. Heading edits land at wrong source bytes because `text` was display-stripped (fixed in `09f860f`).
3. Mid-block Enter caret didn't move into new row (fixed in `0b96be6`, refined in `30249aa`).

The remaining cold-review findings (numbered #4..#15 in the transcript) feed §3 of this doc:
- #4 → §3.5 (inline prediction wholesale clear)
- #6 → §3.9 (BlockWalker enumeration, fixed in `09f860f`/`a7117a0`)
- #8 → §3.6 (commitBlockHole listener fragility)
- #9 → §3.8 + Q5 (focus protocol tangle)
- #10 → §3.4 (activeFocus skip)
- #11, #12, #14 → §3.3 (latency)
- #15 → minor, deferred

### Recent commit graph

```
1ab5fdd fix(view-qml): translate inline highlighter offsets through QTextBlock position    ← C-9
30249aa fix(view-qml): gate post-edit focus routing on rowsInserted                        ← C-8
a7117a0 fix(foundation): align BlockAnchors with parser topLevelBlocks enumeration         ← C-7
0b96be6 fix(view-qml): route caret to new row after mid-block Enter split                  ← C-5
09f860f view-qml: replace regex BlockWalker with foundation topLevelBlocks consumer        ← C-2/C-3
742e11b feat(parser): Document::topLevelBlocks() — linear top-level block iteration        ← C-1
a71dd40 fix(view-qml): initial setModelText for heading + code-block delegates             ← visual recovery
7f5d08f fix(view-qml): locally truncate TextEdit on mid-block Enter (no duplication)
6d61604 fix(view-qml): preserve focused TextEdit content on parse-back; up/down in hole    ← introduced §3.4
bdae435 fix(view-qml): dogfood-surfaced focus race + drop idle-commit (v1)
bcf64cd feat(view-qml): T23 — flush pending projection holes before save
bc0ca6a feat(view-qml): T20-T22 wire hole creation, delegate, focus routing
c96b71d feat(view-qml): T17-T19 v1 hole API (preedit-pattern, one-hole invariant)
106ad60 plan: Stage 4 v1 strengthenings (one-hole invariant, deterministic focus, IME finalize)
7c499d3 docs: Stage 4 v1 redesign + handoff brief
cfbc30f revert: Stage 4 empty-paragraph hole (broken design)
9fc0b83 docs: Live Projection Layer spec + plan (Stages 1-5)
```

---

## 7. Anti-trust list — things in code/comments to be skeptical of

The current arc has produced workarounds, commented-justifications, and stale rationales. Treat these specifically with suspicion:

- **Comments that justify why something is "load-bearing" or "defensible."** Multiple sites (e.g. the `Qt.callLater` retry loops, the `MouseArea.hit()` "DO NOT modify the math" warning, the `activeFocus` skip's rationale block) defend code rather than describe it. Verify the claim against behavior.
- **The projection-layer spec's confident assertion that the IME-preedit pattern eliminates the v0 race.** It eliminates one race; it has its own (idle-commit destroy mid-typing, focus-routing fragility, anchor invalidation under collab edits — see §9 of the spec). Don't take "v1 fixes everything" at face value.
- **Anything in `libs/markoff-view-qml/CLAUDE.md` claiming "load-bearing" status.** The doc was written ahead of the implementation in some places; some invariants are aspirational, not enforced. Cross-check with code.
- **Comments referencing "T<number>" tasks.** Those refer to either the live-render walking-skeleton plan (T0-T23) or the projection-layer plan (T1-T29). They are commit-archeology hints, not specification.
- **`ImageDelegate.qml` and the `BlockKind::Image` constant** — currently dead code paths after C-2. Don't trust their existence as evidence the feature works.

---

## 8. Anti-goals (don't do these)

- **Do NOT propose replacing delegates with a single `QTextDocument`.** The user has explicitly rejected this direction (§1).
- **Do NOT spot-fix without dogfooding.** The arc has shown that test passes ≠ user-visible correctness; `QTest::keyClick` masks the real-world async timing that breaks live editing. Manual dogfood on `markoff-view-qml-app --live` is the gating verification, not ctest.
- **Do NOT trust your inherited mental model from prior sessions.** A cold review found 15 things three implementer-sessions had missed because each was working from inherited assumptions. If you have any doubt, do a cold-review pass yourself.
- **Do NOT delete the projection-layer code preemptively.** Q1 hasn't been resolved. Even if Model B wins, the layer has the prediction half (inline-format speculation, fence speculation) which is independently useful.
- **Do NOT introduce more `Qt.callLater` retry loops.** The existing ones are debt; adding more compounds it. Find a deterministic signal or push the problem down into a binding-side gate (the C-8 pattern).

---

## 9. How to start

1. `cd /home/clinton/dev/Markoff/.worktrees/foundation-exploration/`
2. `git log --oneline -25` and read this doc top to bottom.
3. Run the demo: `cmake --build build-dev -j 8 && ./build-dev/bin/markoff-view-qml-app --live <some-markdown-file>`. Type. Press Enter. Move the cursor. Note what's broken or laggy. **Don't read past dogfood without doing this.**
4. Resolve Q1 with the user before touching code.
5. Pick from §5 sequencing.

You have full authority within the §1/§8 constraints. The user is steering — surface design questions to them, don't pre-resolve them. They will execute manual dogfood; you will execute the implementation work.
