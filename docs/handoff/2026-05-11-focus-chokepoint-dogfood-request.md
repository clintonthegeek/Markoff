# Focus-chokepoint refactor — interactive dogfood request

**Date:** 2026-05-11
**Tag candidate:** `v0.8.0-focus-chokepoint`
**Status:** held pending interactive dogfood (user's local desktop)
**Spec:** `docs/specs/2026-05-11-focus-chokepoint-design.md`
**Plan:** `docs/plans/2026-05-11-focus-chokepoint.md`

## What landed

Tier 1 of the focus-chokepoint refactor. `LiveCursorState` now owns
focus delivery on every structural event through a single
`establishFocus(BlockAnchor, qtPos)` API and a per-delegate
registration mechanism (`delegateAvailable` /
`delegateGoingAway` / `tryResolvePending`). The two originally
reported bugs and Bug C (discovered during the revert) are now
covered by dedicated regression tests on the `LiveRealisticInputHarness`.

Tip-of-branch:

- 187/198 fast tests pass (`-E 'realistic|benchmark'`).
- `tst_live_render_focus_chokepoint_invariant`: **18 of 19 pass.**
- 8 preexisting failures (`cursor`, `structural`, `setext_e2e`,
  `e2_nav_*`, `qml_integration`) predate this work and are
  unaffected by today's commits.
- `tst_live_render_focus_after_enter_at_paragraph_end` (Bug A)
  passes for the first time since the chokepoint plan began.

The chokepoint is **falsifiability-proven on both ends**, with the
audit trail preserved in history as commit pairs:

| Stub                                          | Revert      | Proves                                          |
| --------------------------------------------- | ----------- | ----------------------------------------------- |
| `2d609ba` — `takeFocus` empty body            | `2ab52aa`   | Focus *delivery* runs through the chokepoint.   |
| `20dcaee` — `establishFocus` no-op            | `79fcedc`   | Focus *requests* run through the chokepoint.   |

Together these close the Bug C blind spot. The original Task 15
stub alone couldn't have caught a click-path bypass of
`establishFocus` (the actual root cause of Bug C), because
`takeFocus` would still have fired from non-click gestures. The
second stub catches exactly that case: stubbing `establishFocus`
turns the chokepoint test from 18/19 green to 2/19 green (all 17
substantive tests red).

## What today's bug shake-out revealed

The revert in commit `2ab52aa` exposed three QML-specific defects
in the Task 12 migration of `LiveView.qml` that meant the
chokepoint was being silently bypassed by the most common UI
gesture — clicks. All three are fixed in `ad88089`:

1. **`item.model.blockAnchor` from outside the delegate is
   `undefined`.** QML context properties don't propagate as root
   properties. `LiveView.qml`'s MouseArea fell through to
   `forceActiveFocus()` on the ListView (not on any TextEdit), so
   clicks didn't plant a caret and typing was completely broken
   after a click. Fixed by exposing `blockAnchor` as a root
   property on every delegate and reading `item.blockAnchor`.
2. **`takeFocus(qtPos)` compiled as `takeFocus(QVariant)`.** QML
   couldn't infer `int` from the body's `Math.min`/`Math.max`
   usage, so `Q_ARG(int, qtPos)` from `tryResolvePending` silently
   mismatched. Fixed by typed parameter `function takeFocus(qtPos: int)`.
3. **`Component.onDestruction` threw a TypeError on
   `model.blockAnchor`** (the model row is gone at destruction
   time). `delegateGoingAway` was never reached and stale entries
   lingered in `m_delegates`. Fixed by caching `blockAnchor` to a
   non-binding property at `Component.onCompleted` and reading the
   cached value at destruction.

The premise of the plan — *"all focus flows through
`establishFocus`"* — was correct. The implementation hadn't
achieved it.

## Dogfood checklist

In `markoff-live-app` against a real markdown document
(`docs/specs/2026-04-28-foundation-design.md` is a reasonable
choice — what the user tested today):

1. Place cursor at end of any paragraph; press Enter; immediately
   type — typing should continue in the new block without
   re-clicking. (**Bug A.**)
2. Place cursor at end of a heading; press Enter; immediately type
   — same.
3. Place cursor inside a heading; delete all leading `#`s; the
   block should transition Heading→Paragraph and immediately
   accept typing without re-clicking. (**Bug B.**)
4. Place cursor at start of a paragraph; type `#` then space; the
   block should transition Paragraph→Heading and immediately
   accept typing.
5. Click on any block; immediately type — the typed text should
   appear at the click position. (**Bug C.**)
6. Right-click on any block — context menu should show with the
   correct block anchor wired (the same `item.blockAnchor`
   plumbing as the left-click fix).
7. Cross-block selection via Shift+Click; trigger an editing
   command (Ctrl+B etc. if any are wired); focus should be visible
   afterward.
8. In a 200-block document, repeat steps 1–6 with scrolling —
   focus should not get lost when delegate incubation is under
   stress.
9. After ~5 minutes of typical editing: no observable focus loss,
   no need to click-to-recover. No `QMetaObject::invokeMethod` /
   `TypeError` chatter in the terminal.

Today's interactive sample (user, 2026-05-11): items 1–5 all
verified working. Item 6 not specifically exercised. Items 7–8
not exercised. **Item 9 surfaced two new findings (below).**

## Findings from item 9 (2026-05-11)

### D-fc-1 — Focus lost after typing `---` to create a horizontal rule

**Repro:** On a new line, type `---` (or more dashes). The
Paragraph→HorizontalRule kind transition fires, but focus is lost
afterward — typing does not continue.

**Hypothesis:** HR is a non-text block (no `TextEdit`). The
chokepoint's `tryResolvePending` finds the new
`HorizontalRuleDelegate`, kind matches, calls `takeFocus(qtPos)`
on it — but HR's `takeFocus` can't plant a caret because there is
no `TextEdit` to focus. The chokepoint correctly delivers focus
to the registered delegate, but for non-text kinds that delivery
is semantically meaningless. The kind-transition path needs to
migrate the caret to the *next* block (or create a fresh empty
paragraph after the HR) instead of trying to land it on the HR
itself.

**Likely site:** `LiveStructuralKeyHandler` HR-promotion path
(wherever `Paragraph→HorizontalRule` is decided), and the
`onD2Changed` kind-transition branch in `LiveListModelBinding`
for HR.

### D-fc-2 — Newly-created heading is impermeable to arrow keys

**Repro:** Type `# ` at the start of a paragraph (promote to
heading). Navigate caret away with arrow keys. Try to return —
the new heading row is skipped over; the caret jumps past it as
if the heading row doesn't exist for navigation purposes.
Headings *loaded* with the document are navigable normally; only
*newly-typed* headings exhibit this.

**Hypothesis (revised after a brief read):** The navigation
controller doesn't keep its own row→delegate map — it asks
`m_model->recordAt(row).kind` live and consults
`BlockKindRegistry` for whether a kind supports `TextCaret`
(`LiveNavigationController.cpp:34-41`). So "is this row a
navigation target?" should self-correct as soon as the model
record's kind flips to `heading`. Yet the chokepoint test
`paragraph_promote_via_hash_typing` already exercises the
post-promotion focus-delivery path and passes — which means
**focus does land** on a freshly-promoted heading via chokepoint.

What likely differs is the *cross-block navigation* approach
back into the heading from above/below. That path goes:
delegate `Keys.onPressed` → `nh.tryHandle(Up/Down, ...)` →
`previousNavigableRow` / `nextNavigableRow` → `cursorState->
requestTextCaretAtRow(targetRow, …)` → `establishFocus` →
`tryResolvePending` → `takeFocus`. Each step needs verification
on a runtime-created heading; the most plausible failure modes
are:

1. The buffer text on a newly-promoted heading retains the `# `
   prefix (kind transition stores content-only on parse, but
   may store prefixed text on promotion — see the
   "heading-prefix-doubling" fix referenced in
   `CLAUDE.md` 2026-05-09 entry). If `model.text.length()`
   counts the prefix but the delegate's `TextEdit` doesn't,
   `requestTextCaretAtRow(row, model.text.length())` from
   the up-arrow path could pass an out-of-range `qtPos` and
   the takeFocus clamp lands on end-of-text — focus *does*
   land, but the visible caret is at the wrong position. The
   user might perceive this as "didn't enter the row."
2. The new `HeadingDelegate` may not have finished `Component.
   onCompleted` registration by the time navigation tries to
   target it. `tryResolvePending`'s pending-focus timeout
   would silently expire.

**Investigate first:** print `model.text.length()` on a
runtime-promoted heading vs. a load-time heading, and check the
content-only/prefix-stored convention at L4 (`model.text`
authority) for `Heading`.

## What this implies for the tag

Tier 1 was scoped to "structural-event focus delivery." Both
findings are in that scope (HR creation = structural event;
kind-transitioned heading = post-structural-event navigation
target). The `v0.8.0-focus-chokepoint` tag should remain held
until at least D-fc-1 and D-fc-2 are either fixed or
deliberately deferred with rationale.

## Known follow-ups

- **`heading_demote_via_hash_deletion`** (chokepoint test) still
  fails. The user verified the scenario works interactively —
  this is a test-only inconsistency between `m_cursor.row` and
  the focused delegate row, not a user-visible regression. Worth
  investigating but not a blocker for the tier-1 tag.
- **Re-entrance guards still multiply.** `wasRegistered` in
  `delegateAvailable` and `request(tc)` in `tryResolvePending`
  are defensive guards added during today's shake-out. With the
  destruction TypeError fixed, `wasRegistered` is rarely active.
  Worth re-evaluating whether either can retire after a soak
  period. `docs/INVARIANTS.md` §6/§7 flags them as smells.
- **8 preexisting test failures** unchanged. Those tests
  predate the chokepoint plan's start and are independent work.

## Sign-off

Reply on this document or in chat once dogfood completes. Tag
`v0.8.0-focus-chokepoint` lands only after sign-off.

## What's deferred (tier 2/3/4)

Per spec §10 — typing-cursor authority (queue #2 #1 full / #2 / #6
full / #9), API consolidation (#3 / #4 / #5 / #12), selection/cursor
unification (#10). Each tier dogfoods before the next is planned.
