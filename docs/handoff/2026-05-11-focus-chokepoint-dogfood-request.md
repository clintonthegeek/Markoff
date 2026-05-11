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
verified working. Item 6 not specifically exercised. Items 7–9
not exercised.

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
