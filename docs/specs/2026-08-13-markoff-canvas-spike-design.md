# markoff-canvas spike — projection view leaf on QTextLayout

**Date:** 2026-08-13
**Status:** authorized, not started
**Decision record:** [`2026-08-13-view-authority-direction-decision.md`](2026-08-13-view-authority-direction-decision.md)
**Implementation plan (task sequence + session protocol):**
[`../plans/2026-08-13-markoff-canvas-spike.md`](../plans/2026-08-13-markoff-canvas-spike.md)
**Timebox:** ~3 weeks of working sessions from first commit. At expiry:
explicit user decision (extend once / archive), per decision record §6.
**Cites (invariant 1):**
`docs/handoff/2026-05-07-live-binding-developmental-history.md` (what
the live pipeline's machinery exists to work around — none of it may
be needed here; if it is, that's a finding),
`docs/specs/2026-05-22-cursor-authority-decision.md` (the chokepoint
contract this leaf must not need),
`docs/specs/2026-05-21-audit-L7-ime-composition.md` (IME scenarios),
`docs/INVARIANTS.md`.

---

## 1. Premise under test

A view leaf that renders `MarkoffDocument` directly — one
`QTextLayout` per block, custom input pipeline, **no `QTextDocument`,
no QML `TextEdit`, no widget-private text model of any kind** — needs
none of the arbitration machinery the existing leaves accumulated:
no re-entrance guards, no zero-timer deferrals, no focus chokepoint,
no cursor round-trip translation, exactly one coordinate space.

The spike exists to falsify this. It is an experiment, not a product
increment: the deliverable is the verdict, the criteria table in §7
filled in, and (on pass) enough of a skeleton to cost the real leaf.

## 2. L4 authority decision (invariant 2, in writing, first)

**Model wins, totally.** The view holds **zero** editable text state.

- Text authority: `MarkoffDocument` per-block CRDT buffers. The
  view's `QTextLayout` objects are a derived cache keyed by
  `(BlockId, per-block edit sequence)`; a stale entry is relaid out
  from `blockText()`, never patched in place from view-side state.
- Cursor/selection authority: a single `CanvasCursor` value —
  `{BlockId block, int byteOffset}` for the caret, plus an optional
  `{BlockId, int}` anchor for selection. Owned by the view leaf's one
  controller object. There is no second cursor to reconcile with:
  `QTextLayout` has no cursor, it only answers
  `lineForTextPosition`/`cursorToX` queries at paint/hit-test time.
- Undo authority: `UndoLog`, via the existing `Cmd::*` paths. The
  view never records undo state.
- Kind authority: the document (`Cmd::changeKind` via the same
  prefix-rule inference the other leaves use — reuse
  `KindTransition::inferBlockKind`, called from the leaf's
  `d2DocumentChanged` handler exactly as markoff-live does).

**Retiring authority (invariant 3):** none inside the spike — the
spike is additive (a fourth leaf beside the three canonical ones) and
retires nothing. The contingent retirement (markoff-live, possibly
markoff-styled) is named in the decision record §5.3 and is written
as its own plan only on a pass verdict, inside the D5 design.

## 3. Architecture sketch

```
Markoff::Canvas::View : QAbstractScrollArea
 ├─ BlockLayoutCache      QTextLayout per block, keyed (BlockId, seq);
 │                        lazy: layout only viewport ± margin, estimated
 │                        heights elsewhere, scrollbar corrected on
 │                        realization
 ├─ CanvasCursor          {block, byteOffset} caret + optional anchor
 ├─ CanvasInputRouter     keyPressEvent → StructuralKeyHandler (reused,
 │                        pure) or d2ApplyBufferEdit for printables;
 │                        inputMethodEvent/inputMethodQuery for IME;
 │                        mouse → hit-test → caret/selection
 └─ paintEvent            walk visible blocks, layout.draw(); caret via
                          drawCursor(); selection via FormatRange;
                          inline spans via QTextLayout::setFormats()
                          from inlineSpansFor(id)
```

Data flow is one-directional: input → `Cmd::*` /
`d2ApplyBufferEdit` / `applyFlatEdit`-free (see §4) → document emits
`d2DocumentChanged` (debounced) + targeted block signals → leaf
invalidates affected cache entries + repositions cursor → repaint.
There is no path by which the view writes view-side text state that
later flows back into the document — that path not existing is the
whole point.

Reused as-is from the existing stack: `StructuralKeyHandler`,
`KindTransition`, `Cmd::*`/`UndoLog`, `Theme`,
`inlineSpansFor` + `InlineParseCache`, `MarkdownView` base contract
(as far as practical; full contract-v2 conformance is **not** an exit
criterion — see §5).

## 4. Coordinate-space rule

One space: **UTF-8 byte offsets within a block**, the CRDT's native
space. `QTextLayout` works in UTF-16 `QChar` indices; the leaf owns
exactly one conversion helper (byte↔QChar within a single block's
text, the moral equivalent of `Coordinates::qtPosToByte`) and it is
used only at the layout boundary (hit-testing, caret x-position,
setFormats ranges). **No global/flat coordinate space is introduced
or used** — no `flatView()`, no `widgetFlatView()`, no
`applyFlatEdit`. Cross-block operations (merge, cross-block selection
delete, paste) go through `Cmd::*` / direct merge primitives with
`(BlockId, byte)` pairs. Any commit that adds a summed-across-blocks
byte offset is a spike finding to log, and a strong fail signal
(it's the Discipline Log's most-repeated bug class).

## 5. Scope

**In scope** (what the exit criteria exercise): paragraphs, headings,
list items (marker painted as decoration, content-only buffer, same
convention as the other leaves), code blocks (monospace, no token
highlighting), inline emphasis/strong/code rendering with
cursor-aware delimiter visibility for emphasis/strong, a minimal
read-write table (grid of per-cell layouts, caret enters cell, typing
edits the cell; **no** row/col ops, alignment, or resize), light/dark
`Theme`, wheel + keyboard scrolling.

**Out of scope, explicitly** (listing per invariant-2 discipline so
their absence is a decision, not an oversight): accessibility
(`QAccessibleTextInterface` — a pass verdict must record it as a
known cost of the real leaf, order weeks; whether it's required is a
user decision at that point), math/mermaid/embeds/footnotes/
frontmatter rendering, find integration, full `MarkdownView`
contract-v2 conformance, drag-drop, middle-click paste, kinetic/
smooth scrolling polish, printing, session/multi-view.

## 6. Constitutional constraints (violating any one = premise falsified)

These are grep-gated by a script committed with the spike
(`libs/markoff-canvas/tests/check-constitution.sh`), run in CI with
the leaf's tests. The gate scans `libs/markoff-canvas/` only.

- **C1 — zero re-entrance guards.** No member or local whose role is
  "suppress reaction to our own write" (`m_applying*`, `isApplying*`,
  boolean set-around-call patterns). Grep pattern plus honest
  review — renaming the guard doesn't dodge the constraint.
- **C2 — zero deferrals.** No `QTimer::singleShot(0, …)`,
  `Qt.callLater`, `QMetaObject::invokeMethod(..., QueuedConnection)`
  used to escape an ordering problem. (Consuming the document's
  existing debounced `d2DocumentChanged` is fine — that debounce is
  the document's, already on file; adding a *view-side* defer is not.)
- **C3 — zero QTextDocument / QML text instances.** The leaf links
  no `QTextDocument`, `QPlainTextEdit`, `QTextEdit`, or Quick text
  types.
- **C4 — one coordinate space** per §4: no cross-block byte
  arithmetic, no `applyFlatEdit`/`flatView` callers.

If satisfying an exit criterion appears to *require* violating C1–C4,
stop, write the finding into this spec's §9, and take the fail exit.
That is a successful spike — it answered the question.

## 7. Exit criteria

Every functional criterion is a QtTest slot on the **production
widget** via real events (`QTest::keyClick`/`mouseClick` on the
window — invariant 5: production callsite, not a synonym), running
offscreen via `scripts/run-tests.sh`. Every slot must be **proven
falsifiable** (invariant 4): break the target seam in a throwaway
stub commit, watch the test fail, revert. Record the falsification
commit SHA in the table on completion.

| # | Criterion | Falsifiable test |
|---|---|---|
| E1 | Typing printable chars at an arbitrary caret position updates `blockText()` and advances the caret; rendered text matches the buffer after each keystroke. | `typing_updates_buffer_and_caret` |
| E2 | Enter mid-block splits the block; caret lands at byte 0 of the new block. Backspace at byte 0 merges with the previous block; caret lands at the join point. Block count and content verified via `iterateBlocks()`. | `enter_splits_backspace_merges_caret_at_join` |
| E3 | Undo after E2's split restores the merged block **and** the caret references a block that exists (never a vanished `BlockId`); redo likewise. This is the queue-#10 invariant, natively. | `undo_redo_never_strand_caret` |
| E4 | Cross-block drag selection with real mouse events works in **both directions** (the 2026-05-21 asymmetry class); Ctrl+C then puts all selected blocks' text on the clipboard; a printable key on the selection collapses it and inserts at the first corner. | `drag_selection_both_directions_copy_and_collapse` |
| E5 | Kind transition: typing `# ` at byte 0 of a paragraph promotes it to Heading without losing focus or caret; the block re-renders in heading style. | `hash_space_promotes_heading_caret_survives` |
| E6 | IME: the five audit-L7 scenarios (commit-after-preedit, preedit-replace-commit, cancelled composition, commit into non-empty block, lifecycle probe) pass via `QInputMethodEvent`, with preedit visibly rendered (asserted via the layout's format ranges, not a screenshot). | `ime_l7_scenarios` (5 slots) |
| E7 | Delimiter visibility: `**bold**` renders with delimiters hidden and content styled when the caret is outside the span; moving the caret into the span reveals the delimiters; editing while revealed round-trips correctly. | `delimiter_visibility_follows_caret` |
| E8 | Minimal table: caret can enter a cell by mouse, typing edits that cell's buffer only, adjacent cells and the following block are unaffected, and no crash on cell edit + repaint (the QML-Repeater UAF class must have no analogue). | `table_cell_edit_isolated` |
| E9 | **Perf** (offscreen, release build, `-j 4` build cap per standing rule): on a 500-block synthetic document — load-to-first-paint < 500 ms; p95 keystroke→paint < 16 ms over a 200-keystroke run mid-document; scroll through the full document without layout of all blocks (assert the cache realized < 30% of blocks); RSS delta for the widget < 100 MB. Bench binary modeled on `tst_live_render_table_typing_perf`. | `tst_canvas_perf_500` |
| E10 | **Constitution**: `check-constitution.sh` passes on the final spike tree (C1–C4). | grep gate in CI |

Pass = all ten. Anything less at timebox = the §6/§8 fail-or-extend
path; there is no partial credit that quietly becomes "good enough."

## 8. Execution shape

1. **Week 1:** skeleton — `View` + `BlockLayoutCache` + paint +
   caret + hit-test; E1, E2 green (with falsification proofs).
2. **Week 2:** selection + clipboard + undo + kind transitions +
   inline spans; E3, E4, E5, E7.
3. **Week 3:** IME, table, perf harness; E6, E8, E9, E10; fill in
   §9; verdict.

Per-criterion commits, tests-first where the seam allows (invariant
4's break-the-stub proof makes "first" meaningful even when the
widget must exist for the test to compile). The leaf lives at
`libs/markoff-canvas/` with its own `CLAUDE.md` stub pointing here.
No changes to the other leaves except additive test fixtures; the
standstill rule from the decision record §5.2 applies to the rest of
the tree.

## 9. Findings log (fill during the spike)

> Append findings here as they surface: constraints that bit,
> Qt-layer surprises (QTextLayout quirks, IME event ordering),
> anything that changes the cost estimate for the real leaf. On the
> fail path this section *is* the deliverable.

**T0 (2026-08-13) — scaffold + constitution gate**

- **C2 bit within the first hour, in the demo app.** The idiomatic way
  to make `app/main.cpp` construct-paint-exit under offscreen is
  `QTimer::singleShot(0, &app, &QCoreApplication::quit)`. Wrote it
  reflexively; the gate caught it. Replaced with
  `QCoreApplication::processEvents(); return 0;`. Not a real ordering
  defer, but the gate makes no exception for demo code and shouldn't:
  the value of C2 is that the reflex gets interrupted every time. Cost
  of compliance here: zero. Noting it because "how often does the
  constitution bite, and how expensive is each bite" is the thing the
  spike is actually measuring.
- **The gate must ignore whole-line comments.** First run failed on the
  leaf's own rationale prose — a comment explaining "C2 forbids
  `singleShot(0)` here" reads as a `singleShot(0)`, and the CMake
  comment naming `Qt6::Quick` as forbidden reads as a link to it. A
  gate that punishes documenting its own rules trains agents to stop
  documenting them. `check-constitution.sh` now blanks whole-line
  comments before matching (line numbers preserved). Deliberate
  asymmetry: C++ preprocessor lines are **not** stripped, so a real
  `#include <QQuickItem>` is still caught; only CMake treats `#` as a
  comment marker. A violation can never live on a pure comment line —
  it is code or a build directive — so no coverage is lost. Hiding a
  violation from the gate by commenting it out disables the code too.
- **Grep is not the constraint** (spec §6, C1). The gate is a reflex
  interrupt, not a proof. T11's honest read of every canvas file is
  what actually decides C1 — a renamed guard passes this script.
- **Gate proven falsifiable at T0**, ahead of the per-test protocol:
  planted one non-comment violation of each of C1/C2/C3/C4 plus a
  `Qt6::Quick` link line, confirmed all five reported with correct
  file:line, removed the plant, re-ran clean. The plant was never
  committed (it does not compile — `applyFlatEdit` has no declaration
  here — which is itself a small assurance).
- **Baseline after T0: 279/279** (the 277 standstill baseline + 2
  canvas tests). No file outside `libs/markoff-canvas/` changed except
  the root `CMakeLists.txt` registration line and its comment block.
- Deferred to T1, flagged so it is a decision and not an oversight:
  `View::paintEvent` is empty, so the widget does not yet paint its own
  background. Theme-driven background fill lands with the paint path.

**T1 (2026-08-13) — read-only render, lazy layout, scroll**

- **`blockText()`'s marker convention is inconsistent across kinds, and
  it is load-bearing for T6/T7.** Verified empirically against
  `loadFromMarkdown` (kind → buffer):
  `ListItem` → `one QTextLayout per block` (marker stripped),
  `BlockQuote` → `A block quote…` (`> ` stripped),
  but `Heading` → `# Canvas spike` (prefix **kept**) and
  `CodeBlock` → ` ```cpp\nint main…\n``` ` (fences **kept**).
  So two kinds are narrowed to content and two are not. The core's own
  `listItemDisplayMarker()` doc comment asserts the opposite ("unlike
  every other BlockKind, whose markers stay inline") — it is right about
  ListItem, wrong about BlockQuote.
  **Consequence for T6:** the plan's step "promote to Heading + strip the
  `# ` prefix via `d2ApplyBufferEdit`" would leave a *typed* heading's
  buffer without the prefix while a *loaded* heading's buffer has one —
  two representations of the same block, which is the class of divergence
  this whole leaf exists to avoid. **T6 must not strip.** Decide there
  whether the canvas convention is "buffer keeps ATX prefix" (then T6
  only changes kind) or "buffer is content-only" (then the gap is in
  `loadFromMarkdown`, which is core, which is standstill — so it becomes
  a finding, not a fix).
  **Consequence for T7:** hiding `# ` and ``` ``` ``` is the same
  mechanism as hiding `**` delimiters. T1 renders them verbatim rather
  than inventing a byte remap early; T7's elide-vs-invisible decision now
  covers three delimiter classes, not one.
- **`QTextLayout` does not break on `\n`.** It breaks on width, or on
  `QChar::LineSeparator` — nothing else. A code block rendered as one
  run-on line until `BlockLayoutCache` substituted U+2028 at the layout
  boundary. The substitution is 1 QChar → 1 QChar, so QChar indices still
  align with the block's real text. **T2 trap:** the byte↔QChar helper
  must convert against `doc.blockText(id)`, never against the layout
  string — U+2028 is 1 byte as `\n` but 3 as itself, so a naive
  `layoutText.left(i).toUtf8().size()` is wrong by 2 per preceding
  newline. Guarded by `newlines_inside_a_block_break_lines`.
- **`Theme::color()` falls back to `TextDefault` for undefined slots**,
  which is actively harmful for background slots: it returns the *text*
  colour. `QuoteBackground` is defined in neither `defaultLight()` nor
  `defaultDark()`, so blockquotes painted a black slab under black text.
  The leaf now treats "resolves to exactly TextDefault" as "undefined"
  (`backgroundOrNone`). Real Theme sharp edge, not spike-specific — worth
  raising against core once the standstill lifts.
- **Lazy layout needed a fixed-point loop, and the obvious alternatives
  were both constitution violations.** Realizing corrects estimated
  heights → the scroll range moves → a bottom-parked viewport must
  re-pin → different blocks come into view. Ctrl+End first landed 23px
  short of the end for exactly this reason. The reflexes are (a) a
  `singleShot(0)` to "settle next spin" (C2) and (b) a re-entrance guard
  around the scrollbar write, since `setValue` re-enters
  `scrollContentsBy` (C1). Neither was needed: `ensureLayoutForViewport`
  iterates to a fixed point synchronously (realization is monotonic, so
  it terminates; cap 4 passes), and **`scrollContentsBy` deliberately
  does not realize** — `paintEvent` is the single place estimates become
  layouts, so the recursion cannot form. Structuring it away beat
  guarding it. Second bite of the constitution, second time the
  constraint improved the design rather than costing anything.
- **A structural edit invalidates every cached style.** `blockKind`/attr
  changes bump only `structuralEditSequence()` (global), not per-block
  `blockEditSequence()`, so there is no way to tell *which* block's kind
  moved. `sync()` restyles all blocks when the structural sequence
  changes. Content typing — the case that must stay cheap, and the one
  T10 measures — is untouched. If T10 shows this hurting, the fix is a
  targeted core signal, i.e. a finding, not a leaf workaround.
- **Numbers, first light:** the mixed-kind fixture (9 blocks) fully
  realizes; a 200-block document realizes ~11% on first paint in a
  600×400 viewport and stays under 100% after Ctrl+End (the middle is
  never laid out). Real E9 measurement is T10's.
- The manual harness takes `MARKOFF_CANVAS_GRAB=<path.png>` and renders
  to a file, so the leaf can be eyeballed offscreen without `--direct`.
  Both T1 rendering defects above were found by looking at that grab,
  not by a failing test — worth keeping in the loop for later tasks.

**Post-T1 direction review (2026-08-13) — checked against qtbase source**

- User-directed evaluation: is the spike reinventing a wheel that
  GPLv3 lets us simply take from Qt (`~/src/qtbase`, dual-licensed
  LGPLv3/GPLv2/GPLv3 — copying into Markoff is legally permissible)?
  **Answer: no.** Qt's editors internally do exactly what T1 built —
  one lazy `QTextLayout` per block (`QPlainTextDocumentLayout`
  realizes a block's layout only when first asked for its bounding
  rect). The architecture is validated by Qt's own practice. But the
  reusable input engine, `QWidgetTextControl` (~3.5k lines), is
  coupled to `QTextCursor`/`QTextDocument` on ~212 lines — taking it
  means taking the second document model, i.e. the exact two-model
  arbitration problem this spike exists to falsify (C3). The code is
  legally takeable and architecturally untakeable; the plan stands.
- What IS worth taking is small and now indexed in the plan's new
  "Qt upstream reference" section: `QInputControl::isAcceptableInput`
  (T2 — the printable-key predicate has real edge cases: AltGr,
  format chars, surrogates), `QKeySequence` standard-key dispatch
  (T2/T5), and the `inputMethodEvent` ordering in
  `qwidgettextcontrol.cpp` (T8 — confirms the plan's
  `setPreeditArea` approach is what Qt itself does). License rule
  for copied snippets (attribution + GPL-3.0-only pin) recorded
  there too.

**T2 (2026-08-13) — caret, hit-test, typing**

- **`QTest::keyClicks(QWidget*, QString)` cannot drive the E1 test as
  written.** Its internal ASCII/Latin-1 table (`qasciikey.cpp`) covers
  uppercase `É` (0xc9) but not lowercase `é` (0xe9) — `QTEST_ASSERT(false)`
  fatally aborts the test process rather than failing a single check —
  and has no representation at all for a codepoint outside the BMP (an
  emoji's UTF-16 surrogate pair isn't one "key"). Fix: construct the
  `QKeyEvent` directly (`Qt::Key_unknown` + the target `QString` as its
  `text()`) and `QCoreApplication::sendEvent()` it at the widget. This is
  still the real event path — `View::keyPressEvent` reads `event->text()`,
  never the key code, for printable input (invariant 5's "production
  callsite" is about the widget under test, not the stimulus helper) — and
  is the same technique Qt's own test suite uses for non-Latin1 input.
  Plain ASCII keys still go through `QTest::keyClicks` unchanged.
- **`QAbstractScrollArea` needs an explicit `mouseReleaseEvent` override
  or `QTest::mouseClick` warns "Mouse event not accepted."** Harmless
  (release isn't part of E1's contract; T5 will use it for drag-selection
  extension) but silenced by accepting the event, matching the existing
  paintEvent/keyPressEvent override pattern for the viewport-forwarded
  virtuals.
- **`std::unique_ptr<QTextLayout>` does not propagate const.** A `const
  BlockLayoutCache::Entry &` still hands out a non-const `QTextLayout*`
  via `e.layout->...` (unique_ptr's `operator->`/`get()` are const
  member functions returning `T*`, not `const T*`). Caret code reads
  `entries()` (a `const std::vector<Entry>&`) and calls
  `lineForTextPosition`/`lineAt`/`xToCursor`/`cursorToX` — all
  `QTextLine`-returning const methods — directly off that without needing
  a cast. Worth knowing before reaching for `const_cast` here reflexively.
- **The byte↔QChar helper is now duplicated verbatim** between
  `markoff-live/src/Coordinates.cpp` and `markoff-canvas/src/Coordinates.cpp`,
  exactly as the plan's cheat sheet directs ("copy the logic … do not link
  markoff-live"). Flagging for T11: if the spike passes, promoting this
  helper into `markoff-core` (it has zero markoff-live-specific
  dependencies) removes the duplication for the real leaf.
- **Vertical caret motion crossing a block boundary uses a bare x pixel
  value, not per-block-adjusted for `leftIndent`.** A list item's marker
  indent shifts its layout's content start relative to a plain paragraph's;
  landing "at the same x" across that boundary is therefore off by the
  indent delta. Plan T2 explicitly waives exact column affinity as a
  criterion, so left as-is — noting the specific mechanism in case it
  surprises someone at T9 (table cells have their own x origin per column,
  same class of imprecision).
- Structural keys (Enter split, boundary Backspace/Delete merge) are
  explicitly out of scope here — in-block Backspace/Delete no-op at the
  block edge rather than doing anything to a neighbor, verified in
  `backspace_and_delete_remove_clusters`. T3 is where those boundary cases
  get real behavior via `StructuralKeyHandler`.
- Full suite after T2: **280/280** (277 standstill baseline + 3 canvas:
  `tst_canvas_render`, `tst_canvas_typing`, `tst_canvas_constitution`).

## 10. Verdict (fill at close)

- **Result:** —
- **Criteria table with falsification SHAs:** —
- **Recommendation to the D5 design:** —
