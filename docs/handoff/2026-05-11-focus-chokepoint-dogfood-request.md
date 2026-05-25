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

D-fc-1 and D-fc-2 resolved in commit `4fb711f`. The follow-on
finding D-fc-3 ("arrow keys still won't move past already-existing
HRs") surfaced a related generalisable issue and is fixed in
commit `9b30d75`. All three resolved.

A re-dogfood pass is needed to confirm interactive behaviour
matches.

### D-fc-1 — Focus lost after typing `---` to create a horizontal rule

**Repro:** On a new line, type `---` (or more dashes). The
Paragraph→HorizontalRule kind transition fires, but focus is lost
afterward — typing does not continue.

**Hypothesis:** HR is a non-text block (no `TextEdit`). The
chokepoint's `tryResolvePending` finds the new
`HorizontalRuleDelegate`, kind matches, calls `takeFocus(qtPos)`
on it — but HR's `takeFocus` can't plant a caret because there is
no `TextEdit` to focus. The kind-transition path needs to migrate
the caret to a real text block.

**Confirmed root causes:**
1. The kind-transition branch in `LiveListModelBinding::onD2Changed`
   indiscriminately called `establishFocus(rec.blockAnchor, qtPos)`
   for every promotion, staging a `TextCaret` on a block that can't
   accept it.
2. The HR delegate's `takeFocus` itself called
   `cs.request({variant: "BlockSelected", ...})`. `request` is not
   `Q_INVOKABLE` — the call surfaced as a TypeError at runtime and
   killed focus delivery entirely (we saw it in the test output:
   `Property 'request' of object … is not a function`).

**Fix (commit `4fb711f`):** When the promotion target is
`HorizontalRule` or `Image`, append a fresh empty `Paragraph`
after the new block (`Cmd::enterAtEnd`) and land the caret there.
That's the natural authoring flow ("`---`<Enter> continue
writing"). HR's `takeFocus` no longer poke-calls back through
`request`; it just `forceActiveFocus()`s the delegate root so
the HR's own arrow/Backspace/Delete handler can still run when
the user later navigates into the HR via Up/Down arrows.

### D-fc-2 — Newly-created heading is impermeable to arrow keys

**Repro:** Type `# ` at the start of a paragraph (promote to
heading). Navigate caret away with arrow keys. Try to return —
the new heading row is skipped over; the caret jumps past it as
if the heading row doesn't exist for navigation purposes.
Headings *loaded* with the document are navigable normally; only
*newly-typed* headings exhibit this.

**Confirmed root causes (substantially different from initial
hypothesis):**

1. **`DelegateChooser` does not swap delegates on `dataChanged`.**
   The chooser binds delegate type once at row create-time. Even
   `BlockKey=(kind, anchor)` producing a Delete+Insert diff at
   the same row wasn't enough: ListView's delegate pool *reused*
   the old `ParagraphDelegate` (binding it to a stale
   `modelIndex=-1`) instead of instantiating a fresh
   `HeadingDelegate` template from the chooser. So the "new
   heading" was visually never a heading at all — it was a
   ParagraphDelegate with `# Paragraph one.` text. Inline
   highlighting on the `#` made it look heading-ish, masking
   the architectural failure.

2. **`tryResolvePending`'s stale-registration check was using the
   model's kind, not the document's.** During a kind-transition
   cascade, the model is one applyOps cycle behind: it still
   says "paragraph" while the document has already been
   `changeKind`'d to "heading". The check matched paragraph-
   to-paragraph against the old delegate and resolved focus
   there — or, after fix #1, would have refused to defer
   resolution until the real swap.

**Fix (commit `4fb711f`):**

1. `LiveBlockModel::applyOps` detects the kind-change pattern
   (one Delete immediately followed by one Insert at the same
   row, same `blockAnchor`, different `kind`) and triggers
   `beginResetModel`/`endResetModel` instead of relying on
   row-insert/remove signals. The chooser then creates the new
   delegate type cleanly. (`tst_live_render_focus_after_heading_demote_via_hash_deletion`,
   which had been failing standalone since the chokepoint plan
   landed, passes now as a free side-effect.)
2. `LiveCursorState::tryResolvePending` now queries
   `m_binding->document()->blockKind(...)` for the stale check.
   The model is a fallback used only when no binding is wired
   (unit tests).

**Side-effects:**
- Test fixture activates the window (`requestActivate` +
  `qWaitForWindowActive`) so `hasActiveFocus()` works in headless
  QPA. Without this, several chokepoint tests had been passing
  only via state leakage from earlier tests in the suite.
- Pre-update of `m_cursor` in `tryResolvePending` no longer goes
  through `request()`'s registry-based variant validation —
  the variant is appropriate by construction at that point, and
  bypassing `request` keeps the unit-test fixture (no registry)
  from segfaulting.

### D-fc-3 — Arrow keys won't move the cursor past already-existing horizontal rules

**Repro:** Open a document with HRs already in it (loaded from disk,
not created in-session). Click in or near an HR. Press arrow keys.
The cursor doesn't escape.

**Root cause (the generalisable issue):** The chokepoint's
`tryResolvePending` always staged a `TextCaret` cursor variant,
regardless of the target block's registered capabilities. HR's
`supportedCursorVariants` is `["BlockSelected"]` only. So clicking
on or near an HR (the 20-pixel divider is easy to land on
accidentally) left the cursor in an invalid state: `cursorKind`
was "TextCaret" but the HR delegate's `isSelected` binding
checked for "BlockSelected" and stayed false. The HR's
`Keys.onPressed` guards against non-selected state and returned
NotAccepted, so arrow keys fell through to ListView's default
key handling — which moves `currentIndex` but doesn't touch
`m_cursor`. Net: stuck cursor.

**Fix (commit `9b30d75`):** Make the chokepoint *variant-aware*.
In `tryResolvePending`, consult `BlockKindRegistry` for the target
kind's `supportedCursorVariants` and pick:

  - `TextCaret` if supported (Paragraph, Heading, CodeBlock,
    ListItem, Blockquote — the common case);
  - else `BlockSelected` (HorizontalRule, Image, Math).

This is the **generalisable rule** the user asked about: the
chokepoint never stages a variant the target delegate can't
honour. Click on an HR now puts it in BlockSelected state; arrow
Up/Down then escapes via the existing structural-key-handler
`hrNavigateUp/Down` path. Image blocks gain the same affordance
for free.

Companion: `focusedAnchorRow()` was previously TextCaret-only,
which meant the HR delegate's `isSelected` binding (compares
`focusedAnchorRow` against `modelIndex`) saw -1 and stayed false
even when the cursor *was* BlockSelected on the HR. Now resolves
the row for any variant carrying a block anchor.

## What this implies for the tag

All three findings (D-fc-1, D-fc-2, D-fc-3) fixed in commits
`4fb711f` and `9b30d75`. The `v0.8.0-focus-chokepoint` tag now
waits only on a final interactive dogfood pass confirming:

- Typing `---<Enter>` creates an HR and leaves the caret on a
  fresh empty paragraph after, accepting subsequent typing
  without re-clicking.
- After typing `# Some text` to promote a paragraph to a heading,
  navigating away with arrow keys and then back (Up or Down)
  lands the caret on the new heading.
- In a document with pre-existing horizontal rules, arrow Up/Down
  navigation cleanly crosses through them; click on an HR puts
  it in a selected/highlighted state from which Up/Down escapes
  to the neighbouring paragraph and Backspace/Delete removes
  the rule.
- The interactive run shows no `QMetaObject::invokeMethod`,
  `TypeError`, or `cursor request rejected` chatter in the
  terminal during normal editing.

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

### D-fc-4 + block-only generalisation — resolved 2026-05-13

**Spec:** `docs/specs/2026-05-13-block-only-kinds-design.md`
**Plan:** `docs/plans/2026-05-13-block-only-kinds.md`

D-fc-4 (Backspace after HR orphans cursor at row=-1) is fixed by the merge fence
in `LiveStructuralKeyHandler::tryHandle()` — Backspace at qtPos=0 adjacent to a
block-only block now selects the block-only instead of running backspaceMerge.

New behaviours to verify in the next dogfood pass:
- Arrow Down from paragraph above HR/Image: first press → HR/Image selected (BlockSelected); second press → paragraph below.
- Backspace at start of paragraph after HR/Image → HR/Image becomes selected. Second Backspace → HR/Image deleted.
- Delete at end of paragraph before HR/Image → HR/Image becomes selected.
- Enter on selected HR/Image → new empty paragraph inserted after, cursor in it.
- Typing on selected HR/Image → no-op (block stays selected, keystroke discarded).
- Triple-click on HR/Image → BlockSelected (same as single click; no surrounding text selected).
- Repeat all of the above for Image blocks using a `![alt](url)` reference.

### D-fc-5 — Typed HR/Image unreachable by arrow nav and click — resolved 2026-05-15

**Symptom (user, dogfood 2026-05-15):** After landing the block-only
generalisation, selection of HR/Image blocks **loaded from the
document** worked, but when the user typed `---` (or `![alt](url)`)
in a blank line to create the block at runtime, arrow keys couldn't
enter/select it and clicking on it did nothing. Visually the HR
rendered and a fresh paragraph appeared below it (the D-fc-1 fix)
with the caret in the new paragraph — only the *navigability* was
broken.

**Root cause:** Delegate-swap registration race in
`LiveCursorState::delegateGoingAway`. When `DelegateChooser`
swapped `ParagraphDelegate` → `HorizontalRuleDelegate` at the same
`BlockAnchor`, the actual lifecycle ordering was:

1. NEW `HorizontalRuleDelegate.Component.onCompleted` →
   `delegateAvailable(anchor, "hr", R_hr)` — registers HR.
2. OLD `ParagraphDelegate.Component.onDestruction` fires *after* →
   `delegateGoingAway(anchor)` unconditionally removed the entry,
   **clobbering the just-registered HR**.

`tryResolvePending` then found no delegate at the anchor (or a
stale `"paragraph"` entry from the kind-transition-replacement
branch's wasRegistered=true path) and silently dropped every
focus request. Loaded HRs worked because no swap occurs: the HR
delegate registers once at load time with no preceding
`delegateGoingAway` to race with. The merge-fence backspace path
worked accidentally because it ran `establishFocus` early enough
that the new HR delegate's onCompleted hadn't completed the race
yet.

**Fix:** `delegateGoingAway` now takes the dying delegate's
`QQuickItem *root` and only removes the entry if `m_delegates[anchor].root`
still equals that root. Stale destruction notifications after a
kind-swap are skipped. All seven QML delegates updated to pass
`root`. Signature change is backwards-compatible (default `nullptr`
preserves the legacy unconditional-remove behaviour for any
caller that doesn't have a root handle).

**Verification:** Probe trace at `LiveCursorState::delegateAvailable`/
`delegateGoingAway` showed the exact race in the typed-HR path.
Two new reproducer tests in `tst_live_render_focus_chokepoint_invariant`:
`arrow_up_from_new_para_after_typed_hr_lands_blockselected` and
`click_on_typed_hr_lands_blockselected`. Interactive dogfood
confirmed by user.

**Files:** `libs/markoff-live/include/markoff/live/LiveCursorState.h`,
`libs/markoff-live/src/LiveCursorState.cpp`,
`libs/markoff-live/qml/delegates/{BlockOnlyDelegateBase,Paragraph,Heading,CodeBlock,Blockquote,ListItem,Math}Delegate.qml`,
`libs/markoff-live/tests/tst_live_render_focus_chokepoint_invariant.cpp`.

### D-fc-6 — Cross-kind paste inside code-block fences breaks the fence — open

**Symptom (user, dogfood 2026-05-15, interactive):** Copy a
cross-kind selection of blocks (e.g. paragraph + heading + list-item)
from elsewhere in the document. Place the caret inside an existing
fenced code block (between the opening and closing fences). Paste.

Observed: the code block is **bisected**. The text before the caret
remains a code block. Each pasted block retains its **original
kind** (heading is still a heading, list-item still a list-item).
The text after the caret — which was originally inside the fence —
takes on the **kind of the last pasted block** rather than staying
a code block.

Expected: pasting inside a fenced code block must coerce all
incoming content to **plaintext within the existing code block**.
The fence is a one-kind region — no kind transitions, no block
splits, no kind inheritance from the paste source. The result
should be a single code block with the pasted text inlined at the
caret position, fences unchanged.

**Why this matters:** Code blocks are the prototypical "no markup
applies here" region. Any path that lets foreign block kinds
leak inside a fence violates the user's mental model and produces
unrenderable markdown on save (because the surrounding fences no
longer enclose the bisected region).

**Likely seam:** The structured-paste path in
`LiveListModelBinding`/`LiveStructuralKeyHandler` — it currently
preserves the source block kinds via the clipboard's
block-structured payload (per the E2.5 D2 multi-block clipboard
fix). It needs a "destination is a code block" precondition that
collapses the paste to flat text and routes through
`applyFlatEdit` instead.

**To investigate:** which clipboard format the paste path is
consuming inside a code block (block-structured vs. plain-text),
where the kind-preservation lives, and what the cleanest
coerce-to-plaintext hook is. May share machinery with the
yet-to-be-written "typing inside a code block is plaintext"
invariant (which presumably already works — typing `# foo` in a
fence doesn't promote, so the equivalent paste-side guard should
exist somewhere to graft onto).

**Test fixture sketch:** doc = `prefix\n\n` + ``` ` ``` `\nfoo\nbar\n```\n` + `suffix`.
Clipboard preloaded with a heading + paragraph + list-item.
Place caret between `foo` and `bar` inside the fence. Paste.
Assert: model row count unchanged in count of code-block rows
(still 1); single fence still encloses all the content; pasted
text appears between `foo` and `bar` as plaintext.

## What's deferred (tier 2/3/4)

Per spec §10 — typing-cursor authority (queue #2 #1 full / #2 / #6
full / #9), API consolidation (#3 / #4 / #5 / #12), selection/cursor
unification (#10). Each tier dogfoods before the next is planned.
