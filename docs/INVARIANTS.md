# Engineering discipline — invariants for this codebase

> This file exists because three serious post-mortems on this branch
> each named the failure pattern the **next** refactor went on to
> reproduce. Awareness has not been the bottleneck. Discipline has.
>
> These rules are **responsibilities you accept by working in this
> repository.** They are not optional. You are responsible for
> following them in your own work, *and* for noticing when you see
> them violated in code you pass through — **even if the violation is
> off-topic from your current task.** A smell you leave unmarked is a
> vote for it being normal. The mechanism for noting them without
> derailing your task is invariant 8 below.

## Why these rules exist — cite once, then they're yours

Read these once before your first non-trivial change in this codebase:

- `docs/2026-05-02-live-view-architectural-audit.md` — names the
  pattern: a bidirectional gossip protocol over multiple sources of
  truth, hidden behind unidirectional-data-flow language.
- `docs/handoff/2026-05-07-live-binding-developmental-history.md` —
  the developmental record. Cited by operating principle 4 of the
  pivot doc. The 2026-05-08 erratum on §A.7 is direct evidence the
  practice averts harm.
- `docs/handoff/2026-05-09-setext-dogfood-findings.md` — the most
  recent instance of the failure pattern (the Tier 2 cursor pivot,
  commits `463fc36..6c44a07`).
- `docs/queue.md` §#2 — the twelve cursor-architecture concerns the
  Tier 2 attempt surfaced and the partial revert left standing. The
  pattern's current open surface.

The pattern, in one sentence: **a new authority over "what is in
block N right now" is added without retiring the old one;
cycle-guards multiply at the seams; focus loss or caret jump is the
visible symptom.**

## The invariants

### 1. Cite the developmental record before refactoring the seam

`docs/handoff/2026-05-07-live-binding-developmental-history.md` is
authoritative for *why this code looks like this*. If your refactor
touches subsystems it names, your spec must cite the relevant
section. The 2026-05-08 erratum is direct evidence this rule averts
harm.

### 2. L4 (block-content authority) is decided in writing before the seam is touched

L4 = who is authoritative for block content: **model wins** or
**delegate wins**. Today the decision is implicit. The 2026-05-02
audit named this the foundational fault. Every regression in the
seam since has been a corollary.

If your refactor touches `LiveEditBinding`, `LiveCursorState`,
`LiveListModelBinding::onD2Changed`, or any text-bearing delegate's
`onContentsChange` / `Connections { onCursorChanged }` /
`Component.onCompleted`, your **spec** (not your plan, not your
code) must state in one sentence which side wins and what the
receiving side must do to stay coherent. Pick one. Decide once.

### 3. Don't add a new authority without retiring the old one

Two sources of truth produce pairwise reconciliation, which produces
race windows, which produce focus loss. Every regression in this
seam has come through this mechanism.

When you add a new canonical store, your spec must **name** the old
store you are retiring and your plan must include its deletion as a
work-unit in the **same plan** — not a follow-up, not a queue item.

### 4. Falsifiable invariant tests precede any focus/caret/block-change refactor

When you touch the seam:

- Write the invariant test **first**. Land it before touching
  production code.
- The test runs on `LiveRealisticInputHarness` (the QML integration
  fixture introduced in commit `0c2e72d`). Pure C++ unit tests that
  bypass the QML path do not protect the production path.
- Prove the test is falsifiable: deliberately break the target seam
  in a throwaway stub and confirm the test fails. **If a broken stub
  does not fail the test, the test is too lenient — fix the test
  before touching production code.**
- This prescription is from the R5-holes post-mortem §6.2. It has
  never been enforced. Queue #2 concern #6 names the specific
  invariant the cursor seam needs: *"`cursorState.focusedQtPos`
  matches the focused TextEdit's `cursorPosition` after every
  keystroke."* Had that test existed, the Tier 2 regression chain
  would have failed in the same session it landed.

### 5. Tests must exercise the production callsite, not a synonym

A C++ test that calls a slot directly does not protect that slot if
production reaches it through QML. In E2 dogfood, a
`pendingVisualLineHint` bug shipped because the C++ tests called
with parens (32/32 green) and the QML callsites didn't, so a
function reference compared `!== 0` was unconditionally true.

Before declaring a slot covered: confirm the test invokes it through
the same surface production uses. For QML-facing slots, that means
a QML test, not a C++ test that calls the slot directly.

**Scope note (2026-05-18):** this invariant applies *project-wide*,
not just to the cursor seam. The dark-toggle regression of
2026-05-18 is the canonical case: `tst_live_render_theme_toggle_*`
called `binding.applyDefaultTheme(true)` directly while production
reached it through a QML `Shortcut` → `QAction::trigger()` →
signal → `Component.onCompleted` connection chain. When commit
`36bbbb9` removed `LiveSelectionView::setSession` (its synonym
target), the production chain silently severed because the
`onCompleted` block threw a TypeError before reaching the
`connect()`. Every test stayed green; the dogfood found it.

**Mechanical mitigation in place:** `QmlIntegrationFixture`
installs a `QtMessageHandler` that fails any test on a qWarning
matching `TypeError|ReferenceError|SyntaxError|is not a
function|is not a signal`. This catches the *runtime* drift class
(C++ API removed, QML callsite stale) automatically. It does
**not** replace the discipline of writing a QML-driven test for
QML-reached slots — it only catches the case where the chain
already exists and a refactor breaks it.

### 6. `Qt.callLater` and `QTimer::singleShot(0, ...)` are smells, not solutions

They mean: *"I gave up on understanding the timing and brute-forced
it."* That phrase is from the 2026-05-02 audit. It is still
accurate. The current count: 11 `Qt.callLater` sites across 8
delegate files.

- When **adding** one: stop and ask whether a chokepoint exists.
  If you still add it, write down in the commit message why no
  chokepoint was viable.
- When **seeing** one in passing: add an entry to the Discipline
  Log (see invariant 8). Off-topic is no excuse.

### 7. Re-entrance guards (`m_applyingX`, `isApplyingY()`) are smells

Same rule. They are the visible mark of dual authority sharing a
signal bus. When adding: justify in the commit. When seeing: log
it.

### 8. Notice and note — the Discipline Log

You are responsible for noticing violations of invariants 1–7 in
code you pass through, **even when off-topic from your current
task.** The mechanism keeps the note cheap so the obligation is
real:

> **`docs/queue.md` — section "Discipline Log."** Append a one-line
> entry naming `file:line`, the invariant number, and one phrase of
> context. No fix required in the same session. The point is that
> the smell becomes visible to the next agent who reads the queue.

Walking past an unnamed smell is the only failure mode in this list
that cannot be undone by the next person, because they will not
know to look.

## Scope and exceptions

These rules scope to the **focus/caret/block-change seam** and to
seam-touching refactors specifically, **with one exception:
invariant #5 (production-callsite tests) applies project-wide.**
Any C++ slot reached from QML inherits #5, regardless of which
library it lives in. The seam includes (but is not limited to):

- `libs/markoff-live/src/LiveCursorState.{h,cpp}`
- `libs/markoff-live/src/LiveEditBinding.{h,cpp}`
- `libs/markoff-live/src/LiveListModelBinding.cpp` —
  `onD2Changed`, `applyOps`, `structuralRows*` emission
- `libs/markoff-live/src/LiveStructuralKeyHandler.cpp`
- `libs/markoff-live/src/LiveSelectionView.{h,cpp}`
- `libs/markoff-live/qml/LiveView.qml`
- Any text-bearing delegate's `onContentsChange`,
  `Connections { onCursorChanged }`, `Component.onCompleted`, or
  any `forceActiveFocus()` / `focusEditAt()` call site.

Outside this seam, normal engineering judgement applies. The rules
exist to protect a load-bearing area where awareness has
demonstrably failed to translate into discipline.

If you are about to deliberately violate one of these and have a
real reason: **write the reason in the spec**, cite the rule by
number, and proceed. The rules exist to make deviations visible,
not to forbid them. An undocumented deviation is the failure mode;
a documented one is just engineering.

## What "discipline" means here — and what it doesn't

It means: when the spec contradicts the code, fix one. When you see
two sources of truth, name them both and pick. When you see a
deferred-focus call, treat it as a question, not an answer. When a
test passes but you did not check that it would fail on a broken
stub, you have not yet earned the test's protection.

It does **not** mean: refusing to ship, gold-plating, paralysis on
small changes, or scope creep on bug fixes. The Discipline Log is
the pressure valve — log the smell, finish your task, move on.

## Maintenance

This file is updated when a post-mortem produces a new prescription.
The procedure:

1. Land the post-mortem in `docs/handoff/` or `docs/specs/`.
2. Add an invariant here, citing the post-mortem section by line.
3. Update the summary in `CLAUDE.md` and (if seam-relevant) in
   `libs/markoff-live/CLAUDE.md`.
4. If the new invariant retires an older one, mark the older one
   retired here in place; do not delete it (so historic commit
   references remain interpretable).

## Project-wide invariants beyond the seam

### Block buffer convention (2026-05-18, B1)

Block buffers in `MarkoffDocument` hold **content only**. They carry
no trailing structural delimiter. `blockText(id).endsWith('\n')` is
legitimate only when the user has authored a soft break or pasted
content containing one — in that case the `\n` is content, not a
protocol bit.

Structural newlines (block separators, document terminator) are the
serializer's responsibility:

  * `interBlockSeparator()` returns `"\n\n"` — the full gap between
    two block bodies.
  * `finalDocumentTerminator()` returns `"\n"` — appended after the
    block loop in `serializeForSave`.

Save normalizes runs of 2+ blank lines to one and ensures a single
trailing `\n`.

Spec: `docs/specs/2026-05-18-b1-buffer-convention-design.md`.

Falsifiable test: `tst_block_buffer_invariant` (markoff-core) +
`tst_block_buffer_interactive` (markoff-live). Falsifiability proofs
landed and reverted in the commit chain for the B1 implementation.

This invariant applies to every `BlockKind`. ListItem was the first to
comply (per `37661b5`, 2026-05-06); paragraph/heading/blockquote/code-
block/HR/image/math joined under the B1 spec.
