# Plan — markoff-canvas spike

**Spec:** [`../specs/2026-08-13-markoff-canvas-spike-design.md`](../specs/2026-08-13-markoff-canvas-spike-design.md)
(read it before Task 0; it is short and normative).
**Decision record:** [`../specs/2026-08-13-view-authority-direction-decision.md`](../specs/2026-08-13-view-authority-direction-decision.md)
(background; read only if you need the *why*).
**Timebox:** ~3 weeks from the Task 0 commit.

This plan is written for **consecutive fresh agent sessions**. Each
task is sized for one session. Do the topmost unchecked task. Do not
skip ahead, do not batch tasks, do not "while I'm here" anything
outside `libs/markoff-canvas/`.

---

## Session protocol (every session, every model)

**Start:**
1. Read this file top to bottom. Read the spec's §6 (constitution)
   and §7 (exit criteria).
2. `git pull`, then build and run the canvas tests:
   ```bash
   cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
   cmake --build build-dev -j 4          # never more than -j 4
   scripts/run-tests.sh -R canvas        # offscreen by default; keep it that way
   ```
3. Confirm the previous task's checklist state matches reality
   (its tests pass). If it doesn't, fixing that IS your session.

**During:**
- All work stays inside `libs/markoff-canvas/` (plus its CMake
  registration and this plan/spec). The rest of the tree is
  **bug-fix-only standstill**. If you believe you need to change
  `markoff-core`, stop and re-read the task's "If blocked" note.
- Before every commit: `libs/markoff-canvas/tests/check-constitution.sh`
  must pass (created in Task 0). If you are tempted to add a
  re-entrance guard, a `QTimer::singleShot(0, …)`, a `Qt.callLater`,
  a queued invoke, or any flat/global byte offset — **stop**. That is
  the fail condition of the whole spike, not an implementation detail.
  Write what happened into spec §9 and end the session reporting it.

**Falsification protocol** (spec §7 requires it for every E-test):
1. Write the test; make it pass.
2. In a throwaway commit, break the exact seam the test guards
   (e.g. skip the caret reposition, off-by-one the byte mapping).
3. Run the test; it MUST fail. If it still passes, the test is too
   lenient — fix the test, not the code.
4. `git revert` the throwaway commit. Record both SHAs in the
   checklist row below.

**End:**
- Tick your task's checkbox and fill its SHA columns below.
- Append anything surprising to spec §9 (findings log). One line
  each; surprises are the spike's product.
- Commit: `canvas(T<n>): <summary>`. Push.

**Decision rules for the unexpected:**
- **Decide yourself and log in spec §9:** rendering details, internal
  class shape, test mechanics, anything invisible outside the leaf.
- **Stop, write spec §9, end session:** anything requiring a
  constitution (C1–C4) violation; anything requiring changes to
  `markoff-core`/other leaves beyond an additive test fixture;
  anything that would weaken an exit criterion.
- **Ask the user:** scope changes, timebox concerns, accessibility,
  editing spec §7.

---

## API cheat sheet (verified against headers 2026-08-13)

All under `libs/markoff-core/include/markoff/core/`. The document is
the only authority; the view never stores editable text or cursor
state that the document doesn't know about.

```cpp
// Reading (MarkoffDocument.h)
std::vector<BlockId> iterateBlocks() const;          // document order
BlockKind  blockKind(BlockId) const;
QByteArray blockText(BlockId) const;                 // UTF-8, content-only
quint64    blockEditSequence(BlockId) const;         // cache key with BlockId
quint64    structuralEditSequence() const noexcept;  // bumps on kind/structure
QList<SourceSpan> inlineSpansFor(BlockId) const;     // cached, parses on demand
void       loadFromMarkdown(const QByteArray &src);

// Writing — always inside a transaction:
//   UndoLog::Transaction t(doc.undoLog());   // see LiveEditBinding.cpp:185 for the idiom
void d2ApplyBufferEdit(BlockId, uint32_t byteOffset, uint32_t removedBytes,
                       const QByteArray &insert, UndoLog::Transaction &t);
BlockId d2InsertBlock(BlockId afterBlock, BlockKind, UndoLog::Transaction &t);
void d2RemoveBlock(BlockId, UndoLog::Transaction &t);
void d2SetBlockKind(BlockId, BlockKind, UndoLog::Transaction &t);
void undoD2();  void redoD2();
void flushPendingD2Changed();                        // synchronous flush, for tests

// Signals
d2DocumentChanged();      // debounced, one per event-loop spin — the main feed
documentReloaded();       // after loadFromMarkdown/resetContent
blocksChanged(QList<BlockId>); blockInserted(BlockId,int); blockRemoved(BlockId,int);
    // ^ targeted signals: INCOMPLETE surface (not emitted from kind/attr/undo
    //   paths). Subscribe to d2DocumentChanged as truth; targeted signals are
    //   an optimization hint only. Do NOT rely on them alone.

// Structural keys (StructuralKeyHandler.h) — pure, reuse as-is:
StructuralResult r = StructuralKeyHandler::handle(doc, block, byteOffset, key, mods);
// r.handled == false → fall through to plain text editing.
// r.caretBlock / r.caretByte → where the caret goes afterwards.

// Kind inference: KindTransition::inferBlockKind (markoff-live/src/KindTransition.h)
// is currently live-leaf-internal. Task 6 says how to consume it WITHOUT
// modifying markoff-live.

// Theme: markoff/core/Theme.h — Theme::Slot::… color slots.
```

Layout boundary: `QTextLayout` uses UTF-16 `QChar` indices; the CRDT
uses UTF-8 bytes. The leaf owns ONE conversion helper (byte↔QChar
within a single block's text) in one file, used only at the layout
boundary. Model it on `libs/markoff-live/src/Coordinates.cpp`
(`qtPosToByte`) — copy the logic into the canvas leaf, do not link
markoff-live.

---

## Qt upstream reference (`~/src/qtbase`, if present on this machine)

Verified 2026-08-13 (post-T1): Qt's own editors are built exactly the
way this spike is building — **one `QTextLayout` per block, laid out
lazily on demand** (`QPlainTextDocumentLayout::blockBoundingRect` →
`layoutBlock` when `lineCount()==0`, in
`src/widgets/widgets/qplaintextedit.cpp`). T1 independently converged
on Qt's internal architecture; that is confirmation, not
wheel-reinvention. The wheel Qt has is welded to `QTextDocument`'s
axle: `QWidgetTextControl` (the shared input engine behind
QTextEdit/QPlainTextEdit) speaks `QTextCursor`/`QTextBlock` on nearly
every line and **cannot be extracted without taking the second
document model that C3 forbids**. Do not try to lift it wholesale.

What you SHOULD do with qtbase, per task:

- **Read as reference, always.** When unsure how a real editor
  handles an input edge case, the answer is in
  `src/widgets/widgets/qwidgettextcontrol.cpp` — read it, then
  implement against `MarkoffDocument`.
- **T2:** copy the printable-key predicate from
  `QInputControl::isAcceptableInput`
  (`src/gui/text/qinputcontrol.cpp`, ~30 lines, self-contained: it
  handles ZWJ/format chars, the Ctrl/Ctrl+Shift rejection with the
  AltGr exception (QTBUG-35734), surrogate pairs, private-use).
  A naive `!text.isEmpty() && !ctrl` check gets German AltGr wrong.
  Also: use `event->matches(QKeySequence::MoveToNextChar)` etc. for
  navigation keys instead of raw key codes — platform conventions
  (macOS Home/End, Ctrl+arrows) come free, and it's how
  `QWidgetTextControl::keyPressEvent` dispatches.
- **T5:** `QWidgetTextControl` mouse handlers are the reference for
  drag-selection edge cases (extend direction, autoscroll during
  drag).
- **T8:** `QWidgetTextControlPrivate::inputMethodEvent`
  (`qwidgettextcontrol.cpp:2026` at tag ~v6.12) is the canonical
  sequence and confirms this plan's shape: preedit goes into the
  block layout via `setPreeditArea` (never the document),
  `replacementStart/Length` are relative to the cursor, and the
  event's `Cursor`/`TextFormat`/`Selection` attributes map to
  preedit-cursor position and `FormatRange` overrides. Mirror that
  ordering.

**License rule for copying:** qtbase is dual-licensed
`LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only`; Markoff is
`GPL-3.0-or-later`. Copying is **legally fine**. Any file containing
copied Qt code keeps The Qt Company's copyright line and gets
`SPDX-License-Identifier: GPL-3.0-only` (drop our `-or-later` for
that file) plus a comment naming the source file. Consequence: the
combined binary is effectively GPLv3-only, which is acceptable —
noted here so it is a decision, not an accident. Copy small,
self-contained logic (predicates, event-ordering) only; anything
touching `QTextCursor`/`QTextDocument`/`QTextBlock` is
architecturally uncopyable here (C3).

---

## Task checklist

Fill SHAs as you go. "Fals." = falsification throwaway commit SHA
(the revert immediately follows it).

| Task | Done | Commit | Fals. |
|---|---|---|---|
| T0 scaffold + constitution gate | ☑ | `a543e6e3` | n/a (gate proven by planted violation, not committed — spec §9) |
| T1 read-only render + scroll | ☑ | `556008c5` | n/a |
| T2 caret + hit-test + typing (E1) | ☑ | `9d11880d` | `b9aa326a` / `cfb5bbf3` |
| T3 structural keys: split/merge (E2) | ☑ | `a798c10e` | `81c18bbb` / `20bdec6a` |
| T4 undo/redo caret survival (E3) | ☑ | `93ac3ff4` | `e44d9583` / `193de3fe` |
| T5 selection + clipboard (E4) | ☑ | `9eb842d6` | `a12ffcfe` / `69a63d91` |
| T6 kind transitions (E5) | ☑ | `b0e030f3` | `9eb50708` / `72f9d34a` |
| T7 inline spans + delimiter visibility (E7) | ☑ | `645b23ab` | `c3f2fb82` / `52656a65` |
| T8 IME (E6) | ☑ | `fd7b71d0` | `6771103f` / `8f04a5ae` |
| T9 minimal table (E8) | ☐ | — | — |
| T10 perf harness (E9) | ☐ | — | — |
| T11 constitution audit + verdict (E10) | ☐ | — | n/a |

---

### T0 — scaffold + constitution gate

Create `libs/markoff-canvas/` (CMake target `markoff-canvas`, links
`markoff-core` + `markoff-parser` + Qt6::Widgets only — NOT Quick,
NOT markoff-live/styled/source), `include/markoff/canvas/View.h`
(`Markoff::Canvas::View : QAbstractScrollArea`, compiles, paints
nothing), empty test binary `tst_canvas_render` registered with
ctest, and `tests/check-constitution.sh`:

- grep `libs/markoff-canvas` for: `m_applying|isApplying|m_inSet|
  m_updating` (C1); `singleShot\(0|callLater|QueuedConnection` (C2);
  `QTextDocument|QTextEdit|QPlainTextEdit|QQuick|import QtQuick` (C3);
  `applyFlatEdit|flatView\(|widgetFlatView` (C4). Any hit = exit 1
  with the offending line printed.
- Wire it as a ctest test so `scripts/run-tests.sh -R canvas` runs it.

Also: root `CLAUDE.md` and `libs/markoff-canvas/CLAUDE.md` already
point here — verify, don't rewrite them.
**Done when:** empty widget shows in a trivial manual `main.cpp`
(offscreen ok), ctest green, constitution script green and failing
correctly when fed a planted violation (test this once, then remove
the plant).

### T1 — read-only render + lazy layout + scroll

`BlockLayoutCache`: map `BlockId → {QTextLayout, quint64 seq, qreal y,
qreal height, bool realized}`. On `documentReloaded`/`d2DocumentChanged`:
drop entries whose `blockEditSequence` changed; recompute y-positions.
Lay out ONLY blocks intersecting viewport ± one viewport height;
unrealized blocks use an estimated height (font line height × a cheap
newline count); correct total scroll range as blocks realize.
`paintEvent`: walk visible realized blocks, `layout.draw(&p, pos)`.
Per-kind presentation lives in one switch: heading font sizes, code
block monospace + background rect, list-item marker painted as
decoration text left of the content (get the marker text from
`MarkoffDocument::listItemDisplayMarker(BlockId)` — single source of
truth, do not re-derive from attrs). Colors from `Theme`.
Wheel + PageUp/PageDown/Ctrl+Home/End scrolling via the scroll area's
scrollbar.
**Test:** load a fixture markdown (headings, paragraphs, list, code
block), assert cache realizes <100% of a 200-block doc after first
paint, assert heights/y monotonic.
**Done when:** fixture renders correctly offscreen (grab via
`QWidget::grab()` and assert non-empty painted rows if you want a
cheap smoke), tests green.

### T2 — caret, hit-test, typing (exit E1)

> **T1 trap (spec §9):** the layout string is NOT the block's text —
> `\n` is substituted with `QChar::LineSeparator` so QTextLayout breaks
> lines at all. Indices align 1:1, but bytes do not. Convert against
> `doc.blockText(id)`, never `layout.text()`.

`CanvasCursor { BlockId block; int byteOffset; }` owned by the view.
Mouse press → block by y-lookup → `QTextLayout::lineAt/xToCursor` →
QChar → byte helper → caret. Paint caret via `layout.drawCursor()`.
`keyPressEvent` for printable text — decide "is this a printable
key?" with the copied `QInputControl::isAcceptableInput` predicate
(see "Qt upstream reference" above; `event->text()` non-empty +
no-Ctrl is NOT sufficient), then: `UndoLog::Transaction t(...);
doc.d2ApplyBufferEdit(block, caretByte, 0, utf8, t);` advance caret
by the inserted byte length.
Plain Backspace/Delete *within* a block: same call with
`removedBytes` = size of the QChar-cluster left/right of the caret
(use `QTextLayout::previousCursorPosition/nextCursorPosition` for
cluster boundaries, then convert — this is what makes emoji work).
Arrow keys Left/Right/Home/End within block; Up/Down across lines
and across block boundaries (enter previous/next block at the
nearest x — keep it simple, exact column affinity is not a
criterion).
On `d2DocumentChanged`: if the caret's block vanished, clamp to the
nearest surviving block (this becomes load-bearing in T4).
**Test (E1)** `typing_updates_buffer_and_caret`: real
`QTest::keyClicks` on the shown widget; after each key assert
`blockText()` and caret byte. Include a multi-byte char and an emoji.
**Falsify:** stub out the caret advance; test must fail; revert;
record SHAs.

### T3 — structural keys: split and merge (exit E2)

In `keyPressEvent`, BEFORE plain-text handling, call
`StructuralKeyHandler::handle(doc, block, caretByte, key, mods)`.
If `handled`, apply `r.caretBlock/r.caretByte` as the new caret and
return. This one call is expected to cover Enter-split, Backspace at
byte 0 (merge with previous), Delete at end (merge next), Tab/
Shift+Tab on list items. **Do not reimplement the semantics** — the
handler is the authority; the canvas only routes keys and places the
caret it is told to place.
**Test (E2)** `enter_splits_backspace_merges_caret_at_join`: split
mid-paragraph → assert block count via `iterateBlocks()`, caret at
byte 0 of new block; merge back → assert caret at the join byte.
**Falsify** per protocol. **If blocked:** if `StructuralKeyHandler`
turns out to need a caller-side behavior the flat leaves get from
their binding, log it in spec §9 with the exact gap — do not patch
markoff-core this session.

### T4 — undo/redo caret survival (exit E3)

Wire Ctrl+Z/Ctrl+Shift+Z (and Ctrl+Y) → `doc.undoD2()/redoD2()`.
The T2 clamp ("caret block vanished → nearest surviving block") is
the whole mechanism — there is no UndoLog selection state, by
decision (see queue #10 item 2's record). Exact position restoration
is NOT a criterion; never-stranded is.
**Test (E3)** `undo_redo_never_strand_caret`: type, split, type,
then undo ×3 / redo ×3 via real key events; after every step assert
the caret's `BlockId` is in `iterateBlocks()` and byteOffset ≤ block
size. **Falsify:** disable the clamp.

### T5 — selection + clipboard (exit E4)

Selection = `optional<{BlockId,int}> anchor` + caret. Mouse drag
extends; Shift+arrows extend; Ctrl+A selects all. Paint via
`QTextLayout::FormatRange` background over the selected byte range
per block (Theme selection color). Ctrl+C: join selected blocks'
selected sub-ranges with `"\n\n"` onto the clipboard (plain text
only — structured MIME is out of spike scope). Ctrl+X = copy +
delete. A printable key / Backspace / Delete / Enter on a non-empty
selection first collapses it: delete the selected range using
per-block `d2ApplyBufferEdit` calls for the partial end blocks +
`d2RemoveBlock` for whole middle blocks + a merge of the two
boundary blocks via the structural path, ALL in one
`UndoLog::Transaction`, caret at the first corner. No global byte
math (C4): iterate the selected blocks and operate per-block.
**Test (E4)** `drag_selection_both_directions_copy_and_collapse`:
real mouse press/move/release downward across 3 blocks → Ctrl+C →
assert clipboard has all three; repeat dragging upward (the
2026-05-21 asymmetry class); then press a printable key → assert
collapse + insert at first corner. **Falsify:** invert anchor/caret
ordering for the upward case.

### T6 — kind transitions (exit E5)

> **Read spec §9's T1 entry before starting: "do not strip" now
> overrides the prefix-strip below.** T1 established empirically that a
> *loaded* Heading's buffer keeps its `# ` prefix (and a CodeBlock keeps
> its fences), while ListItem and BlockQuote are narrowed to content.
> Stripping on promotion would make a typed heading differ from a loaded
> one — the exact divergence this leaf exists to avoid. Change the kind,
> leave the bytes, and record which convention the canvas should adopt.

On `d2DocumentChanged`, for the caret's block only (spike scope):
infer kind from text and compare to `blockKind()`; on mismatch issue
`d2SetBlockKind` (+ prefix strip via `d2ApplyBufferEdit` — **but see the
note above; T1 says do not strip**) in one
transaction — the `# ` → Heading path. Reuse the *rules* of
`KindTransition::inferBlockKind` by **copying the function pair into
the canvas leaf** (`libs/markoff-live/src/KindTransition.{h,cpp}` is
leaf-internal; do not link markoff-live, do not move the file —
that's a standstill violation. The copy is spike-throwaway; note the
duplication in spec §9 so the real leaf resolves it by promoting the
helper into markoff-core).
Guard: only promote FROM Paragraph (same rule as live — a structural
kind's buffer is content-only and would always re-infer Paragraph).
**Test (E5)** `hash_space_promotes_heading_caret_survives`: type
`# ` at byte 0 of a paragraph via key events → assert kind ==
Heading, prefix stripped, caret at byte 0 of content, widget still
`hasFocus()`. **Falsify:** skip the caret fix-up after the strip.

### T7 — inline spans + delimiter visibility (exit E7)

> **T1 finding (spec §9): three delimiter classes, not one.** Heading
> `# ` prefixes and code-block ``` ``` ``` fences live in the buffer too
> and currently render verbatim. Whatever mechanism you choose here
> (elide + remap, or invisible-but-present) should be the answer for
> those as well — decide once, state the scope in spec §9.

`QTextLayout::setFormats()` from `inlineSpansFor(id)` (convert span
byte ranges → QChar ranges with the one helper): bold/italic/inline-
code styling. Delimiter visibility: emphasis/strong delimiter spans
get `QTextCharFormat` with near-zero-visibility treatment
(font size 1 is a hack — instead give delimiters the block background
as foreground = invisible but occupying width, OR rebuild the layout
text with delimiters elided and maintain a per-block byte remap).
**Decide which and log it in spec §9** — elision is prettier but the
remap must stay inside the one conversion helper (C4 discipline);
invisible-but-present is simpler and acceptable for the spike.
Caret-in-span (compare caret byte to span parent range) → delimiters
render normally. Recompute on caret move for the affected block(s)
only.
**Test (E7)** `delimiter_visibility_follows_caret`: load `a **b** c`;
assert the layout's formats hide delimiters with caret outside,
reveal with caret inside; type inside the revealed span and assert
buffer round-trip. **Falsify:** pin visibility on.

### T8 — IME (exit E6)

> Reference implementation: `QWidgetTextControlPrivate::inputMethodEvent`
> — see "Qt upstream reference" above for the exact event-ordering to
> mirror.

Implement `inputMethodQuery` (ImCursorRectangle, ImSurroundingText =
current block's text as QString, ImCursorPosition in QChars,
ImHints) and `inputMethodEvent`: preedit string rendered via
`QTextLayout::setPreeditArea` on the caret block's layout (this is
what it exists for — no document write during preedit);
`commitString()` → one `d2ApplyBufferEdit` at the caret (+
`replacementStart/Length` handling relative to the caret, converted
to bytes); empty commit + empty preedit = cancelled composition,
clear preedit area, no document change. Set
`Qt::WA_InputMethodEnabled`.
**Test (E6)** port the five audit-L7 scenarios
(`docs/specs/2026-05-21-audit-L7-ime-composition.md`, harness shape
in `tst_live_render_ime_composition_qml`) by constructing
`QInputMethodEvent`s directly: commit-after-preedit, preedit-replace-
commit, cancelled composition, commit into non-empty block,
lifecycle probe. Assert preedit is IN the layout (formats/preedit
area) but NOT in `blockText()` until commit. **Falsify:** write
preedit into the document.

### T9 — minimal table (exit E8)

A Table block renders as a grid of per-cell `QTextLayout`s laid out
by the canvas (column width = max cell natural width, capped;
uniform row height per row). Cell text via the core's table cell
access — look at how `StyledTableRenderer` / `TableEditBinding` read
cells (`parsedTable`/cell char ranges) and consume the same core
surface; if that surface turns out to be leaf-private, log the gap
in spec §9 and render cells by parsing `blockText()`'s pipe source
with `inlineSpansForCell`-adjacent core API — whatever is reachable
from markoff-core alone. Caret enters a cell by click; typing edits
that cell's bytes via the same core primitive the styled/live table
paths use. No row/col ops, no alignment UI.
**Test (E8)** `table_cell_edit_isolated`: click cell (1,1), type,
assert only that cell's content changed and the block after the
table is untouched; repaint after edit (the QML-Repeater UAF class
must have no analogue — the test passing without crash under ASAN
if available, else plain run, is the assertion). **Falsify:**
misroute the edit to the neighboring cell.

### T10 — perf harness (exit E9)

New binary `tst_canvas_perf_500` (model on
`tst_live_render_table_typing_perf`): generate a 500-block synthetic
doc (mixed kinds). Measure: load→first `paintEvent` < 500 ms;
p95 keystroke→paint < 16 ms over 200 keystrokes mid-document (drive
key events, time to the next paint via a paint counter); scroll
start→end and assert `BlockLayoutCache` realized < 30% of blocks;
RSS delta < 100 MB (`/proc/self/status` VmRSS before/after widget).
Release-ish build (`-DCMAKE_BUILD_TYPE=RelWithDebInfo`) for the
numbers; record them in spec §9 either way.
**Done when:** all four budgets green in the test, numbers logged.

### T11 — constitution audit + verdict (exit E10)

Run the full suite (`scripts/run-tests.sh`, expect 277 + canvas
additions, all green). Run `check-constitution.sh`. Manually read
every canvas source file once looking for renamed guards (C1 says
honest review beats grep). Fill the spec's §7 table with test names
+ falsification SHAs, §9 final findings (include: the KindTransition
duplication, any core API gaps found in T3/T9, the a11y cost note),
and §10 verdict + recommendation to the D5 design. Update
`docs/STATUS.md`. **Then stop — the pass/fail consequence (retirement
plan vs archive) is the user's call per the decision record §6.**

---

## If the timebox expires mid-list

Do not silently continue. Fill spec §10 with the honest state
(criteria met / unmet, constitution intact or not) and end the
session asking the user: extend once, or archive.
