# Decision record — view-layer authority direction

**Date:** 2026-08-13
**Status:** decided (user-directed evaluation, this session)
**Companion spec:** [`2026-08-13-markoff-canvas-spike-design.md`](2026-08-13-markoff-canvas-spike-design.md)
**Cites (invariant 1):**
[`docs/handoff/2026-05-07-live-binding-developmental-history.md`](../handoff/2026-05-07-live-binding-developmental-history.md)
(pipeline provenance; "Cross-cutting findings" §1–§6),
[`docs/handoff/2026-05-07-pivot-to-d5-first.md`](../handoff/2026-05-07-pivot-to-d5-first.md)
(D5-first product posture), `docs/queue.md` § Discipline Log
(the accumulated invariant-3/6/7 evidence base).

---

## 1. The question

Markoff's three view leaves are built by composing Qt's high-level
text widgets (QML `TextEdit` delegates in the live leaf,
`QPlainTextEdit` in source, `QTextEdit` in styled). The original
project ambition was different: build a **new Qt-level text element**
— not assembled *from* Qt widgets but written *at the layer Qt's own
widgets are written at*. Should we pivot to that?

Three interpretations were evaluated against the developmental record
and against `~/src/qtbase`:

- **(a) Fork qtbase** — patch `QTextDocument` / `QTextEdit` /
  `QWidgetTextControl` internals and ship a modified Qt.
- **(b) New projection-view leaf** — a `QAbstractScrollArea` subclass
  that renders `MarkoffDocument` directly via per-block `QTextLayout`
  (public API) and owns its own input pipeline; no `QTextDocument`,
  no QML `TextEdit`, no second document model anywhere.
- **(c) Status quo** — keep composing high-level widgets; keep the
  invariants/discipline-log system as containment.

## 2. Diagnosis the decision rests on

The dominant defect class in this project's history is **dual
authority at the view seam**. `MarkoffDocument` is the authoritative
model, but every Qt widget we embed carries its *own* private
document model, cursor, selection, undo stack, and input pipeline.
Each leaf is therefore a bidirectional sync bridge between two models
that both believe they own the text. Evidence, all already on file:

- Every re-entrance guard in the tree (`m_applyingTextUpdate`,
  `isApplyingSelection`, `m_applyingFormats`, the `TableEditBinding`
  guard whose absence produced 20–40× re-entry and 510 ms
  keystrokes) suppresses an echo loop between the two models.
  Ten files carry guards; nine `Qt.callLater`/`singleShot(0)` sites
  defer across binding cascades. Invariants 6 and 7 exist because of
  this seam.
- The 2026-05-22 ~20-block data-loss incident was the bridge
  misreading a delegate's private cursor reset as user intent
  (`docs/specs/2026-05-22-cursor-authority-decision.md`). The focus
  chokepoint and `syncFromTextEdit` rejection rules are arbitration
  machinery between the two authorities.
- Four incompatible flat-byte coordinate spaces exist to translate
  between model coordinates and Qt-document coordinates; the "one
  flat view changed separator width, a sibling didn't" bug class has
  struck four logged times (Discipline Log, 2026-05-27 → 2026-06-16).
- The styled-table SIGSEGV class (`b1b238f` post-mortem) is
  `QTextTable` frame positions desynchronizing from flat-source
  positions — fighting `QTextDocument` to represent a structure it
  doesn't natively hold.

By contrast `markoff-core` + `markoff-parser` + collabtext are *not*
part of the problem: the CRDT, `Cmd::*`, `UndoLog`,
`StructuralKeyHandler` (pure by design), `FindController`, `Theme`,
and the `inlineSpansFor` cache are view-agnostic and survive any
view-layer decision unchanged.

Also on the record: the current stack **works** — 277/277,
Corbomite adoption complete, ~2.5 ms/keystroke. This decision is not
made from a burning building; it is made because the containment
system (invariants + Discipline Log) manages the disease without
being able to cure it, and because D5 is expected to aggravate it
(§5).

## 3. Decision 1 — option (a) is rejected **permanently**

Forking qtbase is rejected and is not to be re-derived by future
sessions. Reasons, in decreasing order of weight:

1. **It does not fix the diagnosis.** A patched `QTextDocument` is
   still a second document model with its own cursor/undo/input
   authority. The dual-authority seam — the actual source of the
   hack pile — survives the fork intact.
2. **Permanent rebase treadmill.** Qt's text stack
   (`qtextdocument*`, `qtextlayout`, `qtextengine`,
   `qwidgettextcontrol`, `qtextdocumentlayout` — ~29k lines measured
   against the local `~/src/qtbase` checkout) moves every minor
   release; a private fork must track it forever.
3. **Consumer burden.** Corbomite (and any future consumer) would
   have to build and ship a patched Qt instead of linking distro Qt.
4. **The useful part of Qt's text stack is already public.**
   `QTextLayout` exposes the genuinely hard machinery (HarfBuzz
   shaping, bidi, line breaking, glyph runs) as supported API. The
   only private layer of value, `QWidgetTextControl`, is ~3.5k lines
   of control logic we would want to write differently anyway
   (byte-offset cursors, CRDT command dispatch, block granularity).

Any future proposal that involves patching or vendoring modified
qtbase text sources must cite this record and explain what changed.

## 4. Decision 2 — option (b) is authorized **as a bounded spike only**

A fourth leaf (working name **markoff-canvas**) is authorized as a
time-boxed spike with falsifiable exit criteria, specified in
[`2026-08-13-markoff-canvas-spike-design.md`](2026-08-13-markoff-canvas-spike-design.md).

The spike is *not* a commitment to replace anything. This project
has post-mortems specifically about rewrite loops; the mitigation is
that full commitment is gated on the spike's exit criteria, and the
spike carries a constitutional constraint that directly tests the
premise: **if the projection view needs even one re-entrance guard
or one zero-timer deferral, the premise is falsified** and we keep
the current stack (see spec §6).

Why the premise is plausible enough to spend the spike on: the
2026 pivots each moved *model* authority down the stack
(parser-authority → CRDT-authority, per the D-evolution record) and
each stuck. The view is the last layer where a foreign authority
still lives; a projection view is the matching move at the view
layer. Under it the cursor is natively `(BlockId, byteOffset)`, the
four coordinate spaces collapse to one, and there is no second model
to echo into.

## 5. Decision 3 — sequencing and standstill

1. **`markoff-source` stays, indefinitely.** Plain-text editing is
   the one job where `QPlainTextEdit`'s own model *is* the right
   authority shape (flat text ↔ flat text). It is not a rewrite
   target under any spike outcome.
2. **No new features land in the live/styled seam layer while the
   spike is open.** Bug fixes only, per the same standstill rule the
   2026-05-07 pivot doc used (§3 "frozen"). Building more on a seam
   we may retire throws work away.
3. **The spike result feeds the D5 design.** D5-first remains the
   product posture. The audit-L7 record already notes that a remote
   edit arriving mid-IME-composition is clobbered by the wholesale-
   replace path; under the sync-bridge architecture every remote op
   must replay through widget-private models behind guards — the
   echo-loop problem multiplied by concurrency. A projection view
   consumes remote ops identically to local ones. If the spike
   passes, the retirement plan for markoff-live (and the evaluation
   of subsuming markoff-styled) is written *as part of* the D5
   design, honoring invariant 3 (a new authority retires the old one
   in the same plan). If D5/collab is ever dropped as the product,
   this calculus reverts toward status quo and the spike outcome
   becomes optional debt-paydown — that reversal requires an
   explicit successor decision record.

## 6. Outcomes matrix

| Spike outcome | Consequence |
|---|---|
| All exit criteria pass | Write markoff-live retirement plan inside the D5 design; evaluate folding markoff-styled into canvas as a render policy. |
| Any constitutional constraint violated (guard/defer needed) | Premise falsified. Archive the spike under `docs/archive/` with findings; status quo (c) stands; this record gains a closure banner. |
| Timebox expires with criteria unmet but constraints intact | Explicit user decision: extend once, or archive. No silent drift. |

## 7. Authority

Authoritative for view-layer direction as of 2026-08-13. Overridden
only by an explicit user instruction or a successor record that
names this one. Supersedes nothing — the 2026-05-07 pivot doc's
D5-first posture is unchanged and is cited, not replaced.
