# Focus-chokepoint refactor — design

**Date:** 2026-05-11
**Branch:** `exploration/new-foundation`
**Tier:** 1 of an explicit 4-tier programme (§10)
**Invariants invoked:** 1, 2, 3, 4, 5 per `docs/INVARIANTS.md`

## 0. One-paragraph summary

`LiveCursorState` becomes the **single owner of focus delivery on
structural events**. It exposes `establishFocus(BlockId, qtPos)` and a
delegate-registration slot `delegateAvailable(BlockId, QQuickItem *)`.
Each text-bearing QML delegate exposes one method `takeFocus(qtPos)`
and registers itself on `Component.onCompleted`. The chokepoint
matches available delegates against a single-slot pending-focus
queue and dispatches `takeFocus`. The scattered focus logic in
delegates, `LiveView.qml`, and `LiveListModelBinding::onD2Changed`
is retired in the same commit chain as a precondition (invariant 3).
Typing-led cursor sync (`syncFromTextEdit` → `m_cursor`) is
unchanged.

## 1. Motivation — two reported bugs and one regression pattern

### 1.1 Reported bugs

- **Bug A.** Cursor at end of paragraph; press Enter. Focus is lost;
  typing produces nothing until the user clicks back into the
  editor.
- **Bug B.** Cursor in a heading; delete the leading `#`s, causing
  Heading→Paragraph kind transition. Focus is lost intermittently;
  same symptom.

### 1.2 The regression pattern (recurrent on this branch)

Per `docs/2026-05-02-live-view-architectural-audit.md` and the
2026-05-11 refactor-regression history audit: every cursor/focus
regression on this branch has the same shape and the same cause
class — **a new authority over "what is in block N right now" is
added without retiring the old one; cycle-guards multiply at the
seams; focus loss is the visible symptom.** The bandage history
includes 11 `Qt.callLater` sites across 8 delegate files, two
re-entrance guards (`m_applyingTextUpdate`,
`m_applyingSessionSelection`), and the partially-reverted Tier 2
cursor pivot (commits `463fc36..6c44a07`).

This refactor exists to **change the shape**, not to add fix #6
to the bandage pile.

## 2. Scope and explicit non-goals

### 2.1 In scope (this spec / tier 1)

- Single chokepoint for focus delivery on structural events.
- Single chokepoint for cursor position on structural events.
- Retirement of the scattered focus paths in delegates, `LiveView`,
  and `LiveListModelBinding`'s re-anchor blocks.
- Falsifiability-proven invariant tests on
  `LiveRealisticInputHarness`.
- Resolution of queue #2 concerns **#1 (partial — structural side
  only)**, **#7**, **#8**, **#11**.

### 2.2 Explicit non-goals (deferred to tier 2 / 3 / 4)

- **Typing-cursor authority.** `TextEdit → syncFromTextEdit →
  m_cursor` stays delegate-led during keystrokes. Queue #2 concerns
  **#1 (full)**, **#2**, **#6 (full)**, **#9** stay open. Tier 2.
- **`requestTextCaretAt*` API consolidation.** The three existing
  APIs continue to exist as thin wrappers around `establishFocus`.
  Queue #2 concerns **#3**, **#4**, **#5**, **#12**. Tier 3.
- **Selection/cursor unification.** `LiveSelectionView` and
  `LiveCursorState` remain independent canonical stores. Queue #2
  concern **#10**. Tier 4.
- **`pendingVisualLineHint`** behaviour. Working as intended.
- **Math delegate's `latexEdit.forceActiveFocus` inside
  `Qt.callLater`.** Distinct seam (`BlockInternalEdit` sub-cursor).

## 3. The L4 / ownership decision (per invariant 2)

> **Model wins on structural events; delegate leads during
> keystrokes.**

Specifically:

- For **focus delivery**, the model side (`LiveCursorState`) is
  the single authority. No focus call goes around it.
- For **cursor position** on structural events (Enter, kind-change,
  paste, undo, click-to-focus, programmatic moves), the model side
  is the single authority via `establishFocus`.
- For **cursor position during typing** (per-keystroke), the
  delegate's `TextEdit` is allowed to lead;
  `LiveEditBinding::syncFromTextEdit` propagates that back to
  `m_cursor`. This is the explicit deferral of the typing-side
  dual-authority problem to tier 2.

This decision is the **precondition** for the rest of the spec.
Future agents touching this seam must cite this section by name
or explicitly supersede it.

## 4. Architecture

### 4.1 Seam location in the layer stack

```
L8  Interactive blocks
L7  Structured text
L6  Other text blocks
L5  Structural keys
L4  Block editing                            ← typing-led cursor sync (unchanged)
L3  Cursor + selection   ← FOCUS CHOKEPOINT  (LiveCursorState owns focus delivery)
L2  Diff-driven model
L1  Read-only render
L0  Coordinate primitives
```

`LiveCursorState` is promoted to "owns focus delivery on structural
events" — the truthful version of its current docstring's
"single canonical cursor value" claim (queue #2 concern #1).

### 4.2 Why `LiveCursorState`, not a new class

`LiveCursorState` already holds `m_cursor`, already has a
pending-request structure, already emits `cursorChanged`. Promoting
it is the minimum-diff structural change. A new `FocusController`
would be `LiveCursorState` with a different filename.

### 4.3 The six structural-event call sites that funnel through the chokepoint

1. `LiveStructuralKeyHandler::tryHandle` — Enter, Backspace, Tab,
   arrow-key-cross-block (currently `requestTextCaretAtNewRow` etc).
2. `LiveListModelBinding::onD2Changed` — kind-transition
   demote/promote (lines 441–452, 556–569). The duplicated qtPos
   clamp at lines 405–406, 522–523 folds into the chokepoint.
3. `LiveView.qml` — MouseArea click-to-focus
   (`onPressed:302–305`, `onReleased:331–333`).
4. `LiveSelectionView` — selection-driven focus migration.
5. `LiveNavigationController` — programmatic moves.
6. Paste path (through `onD2Changed`'s structural emit).

Typing's `syncFromTextEdit` path is **not** on this list and is
explicitly not migrated.

## 5. Components

### 5.1 `LiveCursorState` — new API

```cpp
// Structural-event sites call this. Always stores as pending;
// resolution is attempted at safe points (see below). Never
// dispatches synchronously inside establishFocus itself —
// rationale in §5.1.1.
void establishFocus(BlockId blockId, int qtPos);

// Called by LiveListModelBinding at the top of onD2Changed,
// before any structural mutation. Suppresses resolution
// attempts during the cascade so that stale m_delegates
// entries (delegates about to be destroyed) can't receive
// takeFocus dispatches.
void beginStructuralCascade();

// Called by LiveListModelBinding at the bottom of onD2Changed,
// after applyOps has run and structuralRowsInserted/Removed
// have emitted. Triggers a pending-resolution attempt with
// the now-current m_delegates.
void endStructuralCascade();

// Each text-bearing delegate calls this from Component.onCompleted.
// The QQuickItem* points to the delegate root; the chokepoint
// dispatches via QMetaObject::invokeMethod("takeFocus", qtPos).
// `kind` is the model.kind string the DelegateChooser used to
// pick this delegate — the chokepoint validates it against the
// model's current kind on every resolution attempt (see §5.1.2).
void delegateAvailable(BlockId blockId, QString kind, QQuickItem *delegateRoot);

// Called from Component.onDestruction so the chokepoint can clear
// its delegate registry. QPointer also guards against destruction-
// without-notification.
void delegateGoingAway(BlockId blockId);
```

Internal state:

```cpp
struct PendingFocus {
    BlockId target;
    int qtPos;
    qint64 enqueuedMs;
};
struct DelegateRecord {
    QString kind;                  // kind this delegate was constructed for
    QPointer<QQuickItem> root;     // QPointer guards against destruction-without-notification
};

std::optional<PendingFocus> m_pendingFocus;
QHash<BlockId, DelegateRecord> m_delegates;
bool m_inStructuralCascade = false;

static constexpr qint64 kPendingFocusTimeoutMs = 500;
```

Internal helper:

```cpp
// Called from delegateAvailable (when not in cascade) and
// endStructuralCascade. Looks up m_pendingFocus->target in
// m_delegates; if live, invokes takeFocus and clears pending.
// Expires pending requests older than kPendingFocusTimeoutMs.
void tryResolvePending();
```

Resolution rule:

- `establishFocus` always stores as pending. If
  `m_inStructuralCascade == false`, it calls `tryResolvePending()`
  immediately afterward.
- `delegateAvailable` registers the delegate in `m_delegates`. If
  `m_inStructuralCascade == false`, it calls `tryResolvePending()`
  immediately afterward.
- `endStructuralCascade` clears the flag and calls
  `tryResolvePending()`.

This means: during a structural cascade (e.g. kind-change), all
focus intents and all delegate registrations are accumulated and
the actual `takeFocus` dispatch happens once, after `applyOps`
has settled `m_delegates`.

The three existing `requestTextCaretAt*` APIs continue to exist
for tier-1 backwards compatibility, internally converting to
`establishFocus` once they've resolved row→BlockId. They become
thin wrappers and are removed in tier 3.

### 5.1.1 Stale-registration check on resolution

At the end of the **first** `onD2Changed` pass of a kind-change,
`m_delegates[X]` still points to the old `HeadingDelegate` (which
will be destroyed in the second pass). Without a check, the
`endStructuralCascade` → `tryResolvePending` sequence at the end
of the first pass would dispatch `takeFocus` on the dying
delegate.

`tryResolvePending` therefore performs a **kind validity check**
before dispatching:

```cpp
void LiveCursorState::tryResolvePending() {
    if (!m_pendingFocus) return;
    expireIfTimedOut(*m_pendingFocus);
    if (!m_pendingFocus) return;

    const auto id = m_pendingFocus->target;
    const auto it = m_delegates.find(id);
    if (it == m_delegates.end() || !it->root) return;

    // Stale-registration check: does the registered delegate's
    // kind still match the model's current kind for this BlockId?
    const QString currentKind = m_model->kindFor(id);   // model accessor
    if (it->kind != currentKind) return;   // wait for new delegate

    QMetaObject::invokeMethod(it->root, "takeFocus",
                              Q_ARG(int, m_pendingFocus->qtPos));
    m_pendingFocus.reset();
}
```

When the second pass runs, the old delegate emits
`Component.onDestruction` → `delegateGoingAway(X)` (clears
`m_delegates[X]`), then the new delegate emits
`Component.onCompleted` → `delegateAvailable(X, "paragraph", root)`,
and `tryResolvePending` matches.

This shifts the chokepoint's correctness from "track cascade depth
across multiple `onD2Changed` passes" (fragile) to "validate the
registered delegate against the model on every dispatch attempt"
(self-checking). The chokepoint owns one fact (focus intent); the
model owns one fact (current kind per block); the dispatch happens
when they agree.

### 5.1.2 Why no synchronous dispatch from `establishFocus`

At the moment `LiveListModelBinding::onD2Changed` detects a
kind-transition and wants to set focus, the **old** delegate (e.g.
`HeadingDelegate`) is still registered in `m_delegates`. The new
delegate (`ParagraphDelegate`) does not yet exist. A synchronous
dispatch from `establishFocus` would call `takeFocus` on a
delegate about to be destroyed by the next `applyOps` pass — a
focus event delivered to a dying item.

Making `establishFocus` purely intent-recording, with resolution
gated by the cascade boundary, eliminates this class of bug. It
costs one event-loop tick of latency on click-to-focus when no
cascade is in flight (because resolution still happens via
`tryResolvePending` after `establishFocus` returns — but in
that case, `m_inStructuralCascade == false` and resolution is
immediate, no tick).

This is **not** a `Qt::QueuedConnection`-style deferral
(invariant 6) — there is no timer, no queued slot. Resolution
is synchronous to the cascade-end signal that already exists.

### 5.2 Text-bearing QML delegates — uniform block added

Six delegates (`Paragraph`, `Heading`, `Blockquote`, `CodeBlock`,
`ListItem`, `Math`) gain the same block:

```qml
function takeFocus(qtPos) {
    edit.cursorPosition = Math.min(qtPos, edit.length);
    edit.forceActiveFocus();
}

Component.onCompleted: {
    cursorState.delegateAvailable(model.blockId, model.kind, root);
}

Component.onDestruction: {
    cursorState.delegateGoingAway(model.blockId);
}
```

`model.kind` is the same string the `DelegateChooser` in
`LiveView.qml` used to pick this delegate component. Passing it
gives the chokepoint a self-validating registration (see §5.1.1).

~10 lines × 6 delegates = ~60 lines added. The block is uniform —
candidate for a QML mixin / attached property in a follow-up.
Kept inline here so per-delegate behaviour stays grep-able.

### 5.3 Retirements (the §3-invariant naming)

The following are **deleted as part of the same commit chain**.
Not as a follow-up, not as a queue item.

From each text-bearing delegate (six delegates × N lines):

- The existing `Component.onCompleted { if (cs.focusedAnchorRow
  === root.modelIndex) Qt.callLater(focusEditAt) }` block.
- The existing `Connections { target: cursorState; onCursorChanged:
  { edit.cursorPosition = ... } }` block.
- The existing `focusEditAt(qtPos)` function (collapsed into
  `takeFocus`).

From `LiveView.qml`:

- `Connections { target: cursorState; onCursorChanged }` block at
  lines 110–127.
- `focusEditAt(...)` calls in the MouseArea `onPressed:302–305` and
  `onReleased:331–333` — replaced by
  `cursorState.establishFocus(blockId, qtPos)`.
- The "re-confirm focus on `onReleased` because press may have been
  pre-empted" comment at line 328 and its retry logic.

From `LiveListModelBinding.cpp`:

- The duplicated qtPos clamp at lines 405–406, 522–523 (queue #2
  concern #11). Clamp lives once inside `establishFocus`.

From `LiveStructuralKeyHandler.cpp`:

- Call sites using `requestTextCaretAtNewRow` etc. for focus —
  switch to `establishFocus`. The legacy APIs remain available
  for non-focus uses through tier 1.

### 5.4 What stays exactly as it is

- `LiveEditBinding`'s typing path (`onContentsChange` →
  `d2ApplyBufferEdit` → `syncFromTextEdit` → `m_cursor`).
- `LiveCursorState`'s `cursorChanged` signal — kept for non-focus
  consumers. New rule: emitting `cursorChanged` does **not**
  establish focus; only `establishFocus` does.
- `m_cursor` variant semantics (`TextCaret | BlockSelected |
  BlockInternalEdit`).

### 5.5 Code-size accounting

- Added: ~140 lines (`establishFocus` machinery + per-delegate
  registration + `takeFocus`).
- Removed: ~200 lines (scattered focus logic in delegates +
  LiveView + duplicated clamp).
- **Net: negative.**

## 6. Data flow

### 6.1 Bug A — Enter at end of paragraph (new path)

```
LiveStructuralKeyHandler::tryHandle
   cursorState.establishFocus(newBlockId, 0)
      [stores pending; tryResolvePending() runs, finds no delegate
       for newBlockId, leaves pending]
   issues Cmd::enterAtEnd → scheduleD2Changed()
   returns

         next event-loop iteration:

         d2DocumentChanged → onD2Changed
            cursorState.beginStructuralCascade()  [top of onD2Changed]
            applyOps inserts row
            structuralRowsInserted emitted
               ListView incubates new delegate
                  Component.onCompleted →
                     cursorState.delegateAvailable(newBlockId, root)
                        [m_delegates[newBlockId] = root;
                         no dispatch — in cascade]
            cursorState.endStructuralCascade()
               → tryResolvePending()
                  m_pendingFocus.target == newBlockId; delegate live →
                     root.takeFocus(0) → forceActiveFocus + cursorPosition
                     clear m_pendingFocus
```

`LiveStructuralKeyHandler::tryHandle` does **not** bracket with
`beginStructuralCascade`/`endStructuralCascade` — those brackets
are exclusively for `LiveListModelBinding::onD2Changed`. Call sites
that emit a structural intent without doing the structural mutation
themselves (Enter, click-to-focus, programmatic moves) just call
`establishFocus`; the cascade brackets fire later when
`onD2Changed` runs in response.

### 6.2 Bug B — Heading → Paragraph kind change (new path)

```
keystroke (backspace on leading '#') → onContentsChange →
   d2ApplyBufferEdit → d2DocumentChanged → onD2Changed (first pass)
      cursorState.beginStructuralCascade()
      prefix-rule mismatch: demote Heading → Paragraph
      cursorState.establishFocus(sameBlockId, qtPosClamped)
         [stores pending; m_delegates still has old HeadingDelegate,
          but in-cascade flag prevents dispatch to it]
      Cmd::changeKind(Heading → Paragraph)
      cursorState.endStructuralCascade()
         → tryResolvePending():
            m_pendingFocus.target = blockId; m_delegates[blockId] is the
            old HeadingDelegate, but its kind is "heading" while the model
            now reports kind "paragraph" — stale-registration check fails
            (§5.1.1); leave pending, do not dispatch
   d2DocumentChanged → onD2Changed (second pass)
      cursorState.beginStructuralCascade()
      applyOps: structuralRowRemoved (old) + structuralRowsInserted (new)
         HeadingDelegate.Component.onDestruction →
            cursorState.delegateGoingAway(blockId)
               [m_delegates[blockId] cleared]
         ListView creates ParagraphDelegate
            Component.onCompleted →
               cursorState.delegateAvailable(blockId, root)
                  [m_delegates[blockId] = root; no dispatch — in cascade]
      cursorState.endStructuralCascade()
         → tryResolvePending()
            m_pendingFocus.target == blockId; delegate live (the new one) →
               ParagraphDelegate.takeFocus(qtPosClamped)
               clear m_pendingFocus
```

The pending request survives one or more cascade passes; the
in-cascade flag suppresses dispatch to a delegate about to be
destroyed; the cascade-end signal triggers the resolution with
the now-current `m_delegates`.

### 6.3 Pre-condition the design rests on

**`BlockId` survives every structural mutation that preserves the
same logical block.** Per D-pivot (`memory/project_d_pivot.md`) and
`docs/handoff/2026-05-07-live-binding-developmental-history.md`,
this is already true: `BlockId` is the `IdList` element id and is
preserved through `Cmd::changeKind` (only `kind` changes). If a
future change breaks this, the chokepoint's correctness breaks.
This invariant is documented on `LiveCursorState`'s API.

## 7. Error handling and edge cases

| § | Scenario | Policy |
|---|----------|--------|
| 7.1 | Pending request superseded by newer pending request | Latest wins. Single-slot `std::optional`. |
| 7.2 | Delegate destroyed while a request is pending for it | Clear `m_delegates[id]`; **do not** clear `m_pendingFocus`. Common case is kind-change cascade — exactly Bug B. |
| 7.3 | `establishFocus` for a BlockId not in the model | Drop silently; append a Discipline Log entry. Silent because benign races exist; logged because if frequent, caller is buggy. |
| 7.4 | Pending request never resolves | Expire after `kPendingFocusTimeoutMs` (500 ms). Checked on every `delegateAvailable` and on every `cursorChanged` emission. |
| 7.5 | Delegate registers without a pending request | Register in `m_delegates`; no focus dispatch. Common path; no log. |
| 7.6 | Re-entrance: `establishFocus` from inside `takeFocus` | Reentrant by design; no guard. (Contrast `m_applyingTextUpdate` — a tier-4 retirement candidate.) |
| 7.7 | `forceActiveFocus` fails (item disabled/invisible) | Chokepoint does **not** retry. Surfaces as test failure per §8. No runtime fallback (anti-goal: the audit's "five concurrent retry-loops"). |
| 7.8 | BlockId reuse across documents | Out of scope; `LiveCursorState` is per-document. |
| 7.9 | Concurrent `establishFocus` from threads | Main thread only, documented. Out of scope. |

General posture: **fail visibly, don't retry, log discipline
violations to the queue rather than running silent fallbacks.**

## 8. Testing

Discipline per invariants 4 and 5: tests land first, on
`LiveRealisticInputHarness`, with proven falsifiability against a
deliberately-broken stub.

### 8.1 Core invariant test

`tst_live_render_focus_chokepoint_invariant`, parameterised over
~25–30 structural-event scenarios:

```
after every structural event:
    REQUIRE(focusedDelegate->blockId() == cursorState.currentBlockId())
    REQUIRE(focusedDelegate.edit.cursorPosition == cursorState.currentQtPos())
```

Scenarios cover Enter at start/middle/end per block kind, Backspace
at start, kind-transitions both directions, paste at each position,
undo of each, click-to-focus on each kind.

### 8.2 Reported-bug regression tests

- `tst_live_render_focus_after_enter_at_paragraph_end` — Bug A.
- `tst_live_render_focus_after_heading_demote_via_hash_deletion`
  — Bug B.

Named after the user-visible symptom so failures point at it.

### 8.3 Falsifiability proof (invariant 4 enforcement)

The commit chain for this refactor takes a specific shape:

1. **Test-first commit.** Land §8.1 (core invariant) and §8.2
   (named regressions). The scenarios that exercise paths Bugs A
   and B affect land with `QEXPECT_FAIL` markers citing this
   spec. Other §8.1 scenarios pass against tier-zero (they
   exercise structural events the existing code happens to handle
   correctly today). CI green.

2. **Production refactor commit.** Land the `LiveCursorState`
   API additions, the per-delegate `takeFocus` block, and the
   retirements named in §5.3. **In the same commit**, remove the
   `QEXPECT_FAIL` markers from §8.1/§8.2 scenarios that the
   refactor newly fixes. CI green.

3. **Falsifiability-stub commit (throwaway).** Stub every
   delegate's `takeFocus(qtPos)` to be empty. Run the test suite.
   Confirm: **all** §8.1 scenarios fail, plus §8.2. Record
   failure counts in the commit message. CI red, expected.

4. **Revert-stub commit.** Immediately revert step 3. CI green
   again.

The stub commit and its revert are kept in history (not
rebased away) — they are the audit trail that this spec
honoured invariant 4. The commit message of step 3 names the
failure counts so a future agent can verify against current
behaviour.

**If the tests do not fail on the stub, the tests are too
lenient.** Fix the tests before going further. This is the
R5-holes §6.2 prescription enforced literally for the first
time on this branch.

### 8.4 Edge-case unit tests (C++, no QML — policy tests)

- `tst_live_cursor_state_pending_supersession` (§7.1)
- `tst_live_cursor_state_pending_survives_delegate_destruction` (§7.2)
- `tst_live_cursor_state_bad_blockid_drops_silently` (§7.3)
- `tst_live_cursor_state_pending_times_out_after_500ms` (§7.4)
- `tst_live_cursor_state_delegate_arrives_without_pending` (§7.5)
- `tst_live_cursor_state_stale_registration_holds_pending` (§5.1.1)
  — register a delegate with kind "heading"; change model's kind
  to "paragraph"; call `establishFocus`; verify no dispatch.
  Register a new delegate with kind "paragraph"; verify dispatch.

### 8.5 Retirement-evidence tests

Enforce the retirement (per §3-invariant naming) at the CI level so
future agents can't quietly reintroduce the smells:

- `tst_live_render_no_qt_calllater_in_focus_path` — scans delegate
  QML for `Qt.callLater` sites in focus-related blocks. Threshold:
  zero.
- `tst_live_render_focus_path_exits_through_chokepoint` — scans QML
  for `forceActiveFocus()` outside `takeFocus()`. Threshold: zero.

These are unusual tests. They are the *teeth* on invariants 6 and 8.
Without them, the next refactor reintroduces a `Qt.callLater` "just
for one case" and the Discipline Log gains an entry instead of the
commit failing CI.

### 8.6 Dogfood gate (per regression-history audit)

No tag ships without interactive dogfood. The plan's final task
produces
`docs/handoff/2026-05-11-focus-chokepoint-dogfood-request.md`
with checklist:

1. Type at end of paragraph, hit Enter — typing continues without
   re-clicking. (Bug A.)
2. Type at end of a heading, hit Enter — same.
3. Position cursor on a heading, delete all leading `#`s — typing
   continues without re-clicking. (Bug B.)
4. Same as 3 but kind-promote (type `#` on a paragraph).
5. Click between two arbitrary blocks; immediately type — focus
   migrated.
6. Cross-block selection via Shift+Click; editing command; focus
   survives.
7. Repeat 1–6 in a 200-block document. Stress incubation.

Tag held until this signs off. Same pattern as `v0.7.0-e2.5` /
`v0.7.0-e2.6`.

### 8.7 What this testing plan does *not* cover

- Per-keystroke cursor invariant (tier 2's domain).
- Visual cursor blink/render (Qt internals).
- IME compositions during structural events.

## 9. Definition of done

1. Tests in §8.1, §8.2, §8.4, §8.5 land first; §8.3 falsifiability
   proof recorded in commit chain.
2. `establishFocus` / `delegateAvailable` / `delegateGoingAway`
   API on `LiveCursorState` lands; per-delegate `takeFocus` +
   registration lands.
3. Retirements in §5.3 all land in the same commit chain.
4. Bugs A and B reproduce as `PASS` in the regression tests.
5. Full test suite green; build cap `-j 8`.
6. Dogfood request §8.6 drafted; tag candidate
   `v0.8.0-focus-chokepoint` held pending interactive dogfood.
7. `docs/queue.md`'s §#2 cross-references this spec for concerns
   #1 (partial), #7, #8, #11 marked resolved.

## 10. Future work — tier 2, 3, 4 (committed scope, not vague)

### Tier 2 — typing-cursor authority

Closes the dual-store problem during typing. Queue #2 concerns:
**#1 (full)**, **#2** (TextCaret cachedByteOffset bytes-vs-qtPos
rename or convert), **#6 (full)** (invariant test extends to every
keystroke), **#9** (validateVariant on write).

Decision shape: either TextEdit becomes a pure renderer (model
leads typing too) or `syncFromTextEdit` stays but the
reconciliation rule is hardened. Brainstorm scoped to tier 2.

### Tier 3 — API consolidation

Mechanical after tier 1. Queue #2 concerns **#3** (collapse three
`requestTextCaretAt*` to one), **#4** (single-slot pending lifted
or kept), **#5** (`flushPendingD2Changed` leaking through cursor
API), **#12** (typed `currentTextCaret()` accessor).

### Tier 4 — selection/cursor unification

Bigger structural decision. Queue #2 concern **#10**.
`LiveSelectionView` and `LiveCursorState` become one component or
share a typed store. Audit's "re-entrance guards"
(`m_applyingTextUpdate`, `m_applyingSessionSelection`) likely
retire here.

### Out of scope indefinitely (unless evidence demands)

- `pendingVisualLineHint` — separate concern, working.
- Math delegate `latexEdit.forceActiveFocus` inside `Qt.callLater`
  — sub-cursor (`BlockInternalEdit`), distinct seam.

### Tier-between-tier discipline

**Interactive dogfood between each tier, with a tag held until
dogfood signs off.** No tier 2 plan is queued until tier 1 has
dogfooded clean. Pattern matches `v0.7.0-e2.5` / `v0.7.0-e2.6`.
The point isn't ceremony — it's that real input keeps surfacing
things the harness misses, and stacking tiers without dogfood is
what made R5.5 wipe out an arc.

## 11. Citations

Required reading before executing this spec:

- `docs/INVARIANTS.md` — the eight invariants enforced here
  (specifically 1, 2, 3, 4, 5).
- `docs/2026-05-02-live-view-architectural-audit.md` §c — names
  the L4-implicit fault this spec makes explicit.
- `docs/handoff/2026-05-07-live-binding-developmental-history.md`
  — developmental record; cited per invariant 1.
- `docs/handoff/2026-05-09-setext-dogfood-findings.md` — most
  recent instance of the regression pattern; the Tier 2 cursor
  pivot (`463fc36..6c44a07`) is what this spec is engineered not
  to repeat.
- `docs/archive/c-restoration-arc/2026-05-04-r5.5-dogfood-architectural-review.md`
  §3.6/§3.7 — discipline prescriptions enforced here for the
  first time (falsifiability proof, retirement-evidence tests).
- `docs/queue.md` §#2 — twelve cursor-architecture concerns; this
  spec resolves #1 (partial), #7, #8, #11.

## 12. Open questions deferred to the plan

- Exact CMake target wiring for the `git grep`-style retirement-
  evidence tests (§8.5). Probably a small `add_test` calling a
  script in `tests/scripts/`.
- Whether to keep the three `requestTextCaretAt*` APIs as
  deprecated-but-functional wrappers for the duration of tier 1,
  or fold them away immediately. Default: keep through tier 1, fold
  in tier 3.
- Naming: `takeFocus` vs `acceptFocus` vs `setFocusForChokepoint`.
  Default: `takeFocus` (active voice, matches QML convention).
- Exact name of the model accessor `m_model->kindFor(BlockId)`
  used by `tryResolvePending` (§5.1.1). May already exist on
  `LiveBlockModel` (it exposes a `KindRole`); if so, the
  chokepoint reads it through `data(rowFor(id), KindRole)`. The
  plan resolves the exact surface.
