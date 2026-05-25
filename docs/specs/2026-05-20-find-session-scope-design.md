# Find at session scope — design

**Date:** 2026-05-20
**Branch:** `exploration/new-foundation`
**Status:** design draft.
**Supersedes:**
- `docs/specs/2026-05-18-markoff-source-freeze-shape-design.md` §D2 (host-instantiable + Editor self-host FindBar)
- `docs/specs/2026-05-18-markoff-source-freeze-shape-design.md` §D5 (forwarders deletion + showFindBar implementation order, find-bar portion only)
- `docs/specs/2026-05-19-markoff-live-freeze-shape-design.md` §D9 (QML FindBar + `LiveFindController`)

The remainder of both freeze specs stands.

## Purpose

The find UI that landed in the two freeze specs put the search loop and the
visible UI *inside* each view leaf — `Markoff::Source::FindBar` as a child
QWidget of `Editor`, `Markoff::Live::FindBar.qml` embedded above the
`ListView` inside `LiveView.qml`. Two independent implementations, two
independent stores of (needle, matches, currentIndex), one virtual on
`MarkdownView` to start each of them, no shared state.

Two consequences fall out of that shape:

1. **Find state cannot survive a view swap.** Markoff is designed for the
   host to hot-swap views (source ↔ live ↔ future leaves) on the same
   open document inside a session. With find state owned per-leaf,
   switching from source to live drops the needle, the match list, and
   the current-match index — there is no shared store to carry across.
2. **Find-bar focus is hijacked by the first match.** Concrete bug
   reproduced 2026-05-20: typing `Hello` into the live FindBar with
   document `# My Header` produces search-box `H` + document
   `# My elloHeader`. Chain at `LiveFindController.cpp:83` —
   `setNeedle` calls `recomputeMatches` which unconditionally calls
   `seekToCurrent` → `LiveCursorState::requestTextCaretAtRow` →
   `establishFocus` → delegate `takeFocus` → `edit.forceActiveFocus()`.
   Because the find-bar input is a QML child of the same scene as the
   delegates, there is nothing structural keeping its focus.

The first is an architectural error (wrong layer). The second is a focus-
seam violation (invariant 1, 2, 3 of `docs/INVARIANTS.md`). They share a
root cause: the find search loop and the find UI both live inside the
view leaf.

This spec relocates the search loop to `markoff-core` at document/session
scope, retires the view-owned find UI on both leaves, and leaves the
visible find UI to the consumer. The optional shipped affordance is
deferred (§ "Open questions").

## Scope

In scope:

- Revert the find UI added in `4d0e7c3` (live) and `0ec907d` (source).
- Remove `showFindBar` / `showReplaceBar` / `hideFindBar` from the
  `Markoff::MarkdownView` base contract.
- Add `Markoff::FindController` in `markoff-core` operating on
  `MarkoffDocument`.
- Add narrow attach/detach hooks on each view leaf so the active leaf
  can render highlights + respond to navigation; these are not on the
  `MarkdownView` polymorphic surface.
- Lock the no-focus-steal-on-typing behaviour with a falsifiable
  invariant test (per `docs/INVARIANTS.md` invariant 4).
- The `LiveView.qml` `ListView`-→-`Item` refactor done to host the
  QML FindBar is reverted as part of dropping the QML find UI.

Out of scope (tracked separately):

- Optional `Markoff::FindBar` QWidget affordance shipped in `markoff-core`.
  Decision deferred until the consumer (Corbomite) has built its own and
  we can judge whether to promote it. See § Open questions.
- ReplaceBar / replace flow on either leaf.
- Regex search, whole-word toggle, case-sensitivity toggle (the leaf
  implementations were case-insensitive only; the new controller keeps
  that and exposes match-case as a future flag).
- Cross-block-spanning matches.
- Session object as a distinct type. The controller is owned by the
  consumer for now (see D5); a `Markoff::Session` may later assume
  ownership without changing the controller's shape.

## Decisions

### D1. Search loop relocates to `markoff-core` as `Markoff::FindController`

**Decision:** A single `Markoff::FindController` (Q_OBJECT) in
`markoff-core` owns the needle, the match list, the current-match index,
and the active flag. It operates on a `MarkoffDocument *` directly and
emits view-agnostic match descriptors. Each view leaf gains an internal
adapter that subscribes to the controller and translates matches into
that leaf's idiom (scroll + highlight + caret placement).

**Why:** `MarkoffDocument` is the single object already shared across
views through the session model — it is the only entity in the architecture
naturally positioned to outlive any one view. Putting the search loop
anywhere else recreates the per-leaf-state problem we are retiring.
Promoting also collapses the two duplicate search loops
(`LiveFindController::recomputeMatches` + `Source::FindBar::recomputeMatches`)
into one, which is the duplication that pointed at the abstraction error
in the first place.

The controller MAY later become owned by a `Markoff::Session` value type
if one is introduced; the API shape does not change.

**How to apply:**

- New header `libs/markoff-core/include/markoff/core/FindController.h`,
  source `libs/markoff-core/src/FindController.cpp`.
- Operates on `MarkoffDocument *`, subscribes to `d2DocumentChanged` to
  recompute on edit while active.
- Search scans top-level blocks in document order; per-block scan uses
  the block's current text via `MarkoffDocument::iterateBlocks()` /
  `inlineSpansFor` (existing core APIs).
- No `<crdt/...>` dependency; uses public `Markoff::BlockAnchor` to
  identify a block in a match.

### D2. `Match` is `(BlockAnchor block, int charOffsetInBlock, int charLength)`

**Decision:** Match descriptors are expressed in block-anchored character
offsets. The `BlockAnchor` identifies the block; the offset is a
`QChar`-position into that block's text; the length is in characters.

**Why:** The CRDT D2 model is per-block by construction; `BlockAnchor`
is already the public, stable, view-agnostic block identifier. QChar
positions match what QML `TextEdit` and `QPlainTextEdit` use natively
on both leaves, eliminating a conversion at the adapter boundary.
Byte offsets would force every adapter to convert to QChar before
hitting QML or Qt widgets.

The live adapter resolves `BlockAnchor → row` via the existing
`LiveBlockModel::rowForBlock` (already used by `LiveCursorState`).
The source adapter walks the block list once per match emission to
accumulate a flat character offset into the `QPlainTextEdit`'s text
(cheap; block list is short).

**How to apply:** `struct Match { Markoff::BlockAnchor block; int charOffset; int charLength; };` published from `FindController.h` in the `Markoff::` namespace. `Q_DECLARE_METATYPE` for queued-connection safety.

### D3. The controller does NOT touch focus, cursors, or scroll. Adapters do.

**Decision:** `FindController` is pure search state. Setting the needle
recomputes matches; navigating (`findNext` / `findPrevious`) updates
`currentMatchIndex` and emits `currentMatchChanged`. No method on the
controller calls into a view, a delegate, or a cursor.

The active view's adapter listens for `currentMatchChanged` and decides
what to do — scroll the match into view, render highlight overlays,
optionally place a caret. The adapter MUST NOT take focus on its own;
focus belongs to whoever is currently driving the controller (the
consumer's find-bar widget). A caret placed by the adapter on explicit
navigation is allowed only if the adapter has a non-focus-stealing path
to do so (see D6 below).

**Why:** This is the discipline the live-side bug violated. By writing
the no-focus-touch rule into the controller's contract, it cannot be
violated implicitly by typing — there is no code on the typing path
that *could* touch focus. The adapter's two callers (`needleChanged` and
`currentMatchChanged`) are distinguishable and can have different
policies: `needleChanged` → render only; `currentMatchChanged` →
render + scroll + (optionally, on a separate explicit-nav signal)
place caret.

**How to apply:**

- `FindController::setNeedle` recomputes matches and resets
  `currentMatchIndex` to 0 (or -1 if empty). It emits `matchesChanged`
  and, separately, `currentMatchChanged`.
- `FindController::findNext` / `findPrevious` advance `currentMatchIndex`
  and emit `currentMatchChanged` AND a distinct `navigationRequested(MatchRef)`
  signal that adapters interpret as "user is asking to actually jump
  to this match" — adapters MAY scroll and place a non-focusing caret
  in response. `matchesChanged` and `currentMatchChanged` MUST NOT
  imply navigation.
- Adapters connect to all three signals separately; their policy is in
  the adapter, not in the controller.

### D4. Per-leaf adapter is internal and attaches via narrow hook

**Decision:** Each view leaf gains a small internal adapter that wires
the controller to that leaf's rendering surfaces:

- `Markoff::Live::Detail::LiveFindAdapter` (renamed and demoted from
  the public `Markoff::Live::LiveFindController`).
- `Markoff::Source::Detail::SourceFindAdapter` (new; absorbs the
  highlight-painting logic from the deleted `Source::FindBar.cpp`).

The view leaves expose attach/detach hooks for the controller, NOT on the
`MarkdownView` polymorphic surface:

- `Markoff::Live::LiveListModelBinding::attachFindController(Markoff::FindController *)` /
  `detachFindController()`.
- `Markoff::Source::Editor::attachFindController(Markoff::FindController *)` /
  `detachFindController()`.

Attaching is the consumer's responsibility. The consumer holds one
controller per document and re-attaches it when the active view changes.

**Why:** The polymorphic surface should describe what a view IS, not what
search policy it implements. `attachFindController` is a per-leaf wiring
hook with a concrete type on each side; making it virtual would force
the base contract to know about `FindController`, which is fine, but
the asymmetry between leaves (a source view scrolls a QPlainTextEdit
viewport; a live view scrolls a ListView) means there is no behaviour
worth abstracting through the virtual.

Adapters are internal because the consumer never names them. They are
implementation details of how a leaf paints the matches a controller
emits.

**How to apply:**

- Demote `LiveFindController` → `Markoff::Live::Detail::LiveFindAdapter`.
  Move out of `include/markoff/live/` into `src/`. Strip Q_PROPERTYs;
  reduce to a non-Q_OBJECT helper or keep Q_OBJECT only for the slots
  that connect to `FindController`.
- Add `Markoff::Source::Detail::SourceFindAdapter` under
  `libs/markoff-source/src/`. Owns the `QList<QTextEdit::ExtraSelection>`
  highlight set, calls `Editor::plainTextEdit()->setExtraSelections()`.
- Both adapters listen for `matchesChanged` to render highlight overlays,
  and for `navigationRequested(MatchRef)` to scroll and place a caret
  per D6.

### D5. `MarkdownView::showFindBar` / `showReplaceBar` / `hideFindBar` are removed

**Decision:** The three virtuals on `Markoff::MarkdownView` are deleted.
Their overrides on `Markoff::Source::Editor` are deleted. No equivalent
appears on `Markoff::Live::LiveListModelBinding`.

**Why:** These virtuals encode "the view owns its find UI". That is the
opinion we are retiring. The consumer owns the find UI; the view leaves
own only the attach hook and the adapter that draws matches.

**How to apply:** Edit `libs/markoff-core/include/markoff/core/MarkdownView.h`
to remove the three declarations and their `MarkdownView.cpp` default
implementations. Remove the overrides + implementations in
`libs/markoff-source/include/markoff/source/Editor.h` and `Editor.cpp`.

### D6. Caret placement on navigation uses a non-focusing path

**Decision:** When the active adapter responds to
`navigationRequested(MatchRef)`, it scrolls the match into view and
places a caret at the match's start. On the live leaf, this MUST go
through a new `LiveCursorState::setCaretWithoutFocus(BlockAnchor, qtPos)`
chokepoint that updates the canonical cursor state and emits
`cursorChanged` but does NOT call into delegates' `takeFocus()` /
`forceActiveFocus()`. On the source leaf, the adapter sets the
`QTextCursor` on the underlying `QPlainTextEdit` via
`Editor::plainTextEdit()->setTextCursor` (does not steal focus from
elsewhere in the application unless the editor is already focused).

The existing `LiveCursorState::establishFocus` / `requestTextCaretAtRow`
path stays — it is used by typing, arrow nav, click, and other in-view
actions. Find adapters do not use it.

**Why:** The branch's focus-seam invariants name `forceActiveFocus`
inside delegates as the gravity well. The right shape is a new public
method on `LiveCursorState` that *describes* the operation it does
(move the caret) without doing the operation it shouldn't (steal focus).
This makes the focus-vs-cursor authority decision explicit at the API,
which is what invariant 2 (L4 in writing) demands for any new path
that asserts authority over cursor state.

**How to apply:**

- Add `LiveCursorState::setCaretWithoutFocus(Markoff::BlockAnchor, int qtPos)`
  in `libs/markoff-live/include/markoff/live/LiveCursorState.h`. Implementation
  mirrors `establishFocus`'s canonical-state update (sets `m_cursor` to
  a `TextCaret`, emits `cursorChanged`) but does NOT enqueue a pending
  focus request and does NOT call any delegate method.
- The adapter calls `setCaretWithoutFocus` from its
  `onNavigationRequested` slot, then triggers scroll-into-view via
  the model index. (Scroll-into-view on `ListView` is a QML method;
  the adapter exposes `Q_INVOKABLE scrollToCurrentMatchRow()` or
  signals the `LiveView` to call `positionViewAtIndex` — to be
  pinned in the implementation plan.)
- Docstring on `setCaretWithoutFocus` states the L4 decision:
  *"Sets the canonical cursor state to a TextCaret at the given
  block + qtPos. Does NOT take focus and does NOT notify the matched
  delegate. For callers (find adapter, future programmatic seek) that
  need to move the caret without disturbing whichever widget currently
  holds focus."*

### D7. Falsifiable invariant test pins the no-focus-steal rule

**Decision:** Add a QML integration test that exercises the production
path (find input → controller → adapter) and asserts:

1. After typing N characters into a hosting `TextField` wired to a
   `Markoff::FindController`, that `TextField` is still the active focus
   item, and
2. The document content is unchanged from before typing.

The test MUST be proven falsifiable by temporarily reintroducing a
seek-on-needle-change call into the adapter and confirming the test
fails before the implementation lands.

**Why:** `docs/INVARIANTS.md` invariant 4. The bug we just fixed was
catchable by a test of this shape and was not caught because no test
exercised the QML-reached production path with the find input as a
peer widget.

**How to apply:** New slot `find_typing_does_not_steal_focus` in
`tst_live_render_qml_integration.cpp` using `LiveRealisticInputHarness`.
The test hosts a peer `TextField` (or `QLineEdit` via a custom
test harness) bound to a controller wired to the `LiveListModelBinding`,
types via the harness's key-event API, and reads
`Window.activeFocusItem` / `qApp->focusWidget()`.

### D8. `LiveView.qml` returns to being a `ListView`; binding's find Q_PROPERTYs deleted

**Decision:** Revert the `ListView` → `Item` refactor done in `4d0e7c3`
to host the QML FindBar. Restore `LiveView.qml` root to `ListView`.
Delete the `findController` Q_PROPERTY, `showFindBar` / `hideFindBar`
Q_INVOKABLEs, and the `LiveFindController *` member from
`LiveListModelBinding`. Delete `FindBar.qml`.

**Why:** With no QML FindBar to host, the wrapper `Item` and the
listView alias are pure cost. Reverting restores the documented
`LiveView is-a ListView` shape and unbreaks any consumer setting
`spacing` / `cacheBuffer` / `ScrollBar.vertical` / etc. at the root.

**How to apply:**

- `git revert 4d0e7c3` cleanly removes most of this. The new
  `attachFindController` hook on `LiveListModelBinding` (D4) is
  added on top, not as part of the revert.
- Verify the revert restores the pre-`4d0e7c3` `LiveView.qml` exactly;
  rebase any post-`4d0e7c3` changes to that file on top.

## New public surface (post-spec)

`markoff-core`:
```
include/markoff/core/
  FindController.h            # NEW
    namespace Markoff
      class FindController : QObject
        struct Match { BlockAnchor block; int charOffset; int charLength; }
        Q_PROPERTY needle (QString, read/write/notify)
        Q_PROPERTY matchCount (int, notify)
        Q_PROPERTY currentMatchIndex (int, notify)
        Q_PROPERTY isActive (bool, notify)
        ctor(MarkoffDocument *, QObject *parent = nullptr)
        QList<Match> matches() const
        Q_INVOKABLE activate() / deactivate()
        Q_INVOKABLE findNext() / findPrevious()
        signals: needleChanged, matchesChanged, currentMatchChanged,
                 activeChanged, navigationRequested(Match)
  MarkdownView.h              # MODIFIED
    Remove showFindBar / showReplaceBar / hideFindBar
```

`markoff-live`:
```
include/markoff/live/
  LiveListModelBinding.h      # MODIFIED
    Add: void attachFindController(Markoff::FindController *);
         void detachFindController();
    Remove: findController Q_PROPERTY, showFindBar / hideFindBar Q_INVOKABLE
  LiveCursorState.h           # MODIFIED
    Add: void setCaretWithoutFocus(Markoff::BlockAnchor, int qtPos);
  LiveFindController.h        # DELETED (promoted to core; demoted to internal adapter)
qml/
  FindBar.qml                 # DELETED
  LiveView.qml                # REVERTED to ListView root
src/
  Detail/LiveFindAdapter.{h,cpp}  # NEW (internal)
```

`markoff-source`:
```
include/markoff/source/
  Editor.h                    # MODIFIED
    Add: void attachFindController(Markoff::FindController *);
         void detachFindController();
    Remove: showFindBar / showReplaceBar / hideFindBar overrides
  FindBar.h                   # DELETED
src/
  FindBar.cpp                 # DELETED
  Detail/SourceFindAdapter.{h,cpp}  # NEW (internal)
  Editor.cpp                  # MODIFIED — drop find-bar lazy ctor, add attach/detach
```

## Testing strategy

Three layers, in order:

1. **Unit — controller in isolation.** `tst_markoff_find_controller` in
   `libs/markoff-core/tests/`. Construct a `MarkoffDocument` with known
   content; drive needle changes; assert match list shape and
   `currentMatchIndex` advancement; assert no signals or methods on the
   controller require a view or cursor state. Replaces
   `tst_live_find_controller` (which goes away with the demotion).
2. **Per-leaf adapter — wiring.** `tst_live_find_adapter` and
   `tst_source_find_adapter` in each leaf's `tests/`. Construct
   controller + view; attach; assert that `matchesChanged` produces the
   expected highlight set; assert that `navigationRequested`
   scrolls + places a non-focusing caret. The adapter's `BlockAnchor →
   row` resolution and highlight rendering live here.
3. **Integration — focus invariant.** `find_typing_does_not_steal_focus`
   slot in `tst_live_render_qml_integration` (per D7). Mandatory
   falsifiability check before the spec is allowed to land.

The existing source-side `show_findbar_creates_visible_bar`,
`hide_findbar_clears_highlights`, `showFindBar_is_idempotent`,
`findbar_close_signal_hides` slots in
`tst_source_widget_editor.cpp` go away with the `Source::FindBar`
deletion.

## Migration / commit plan

The work is small enough to land in three commits behind the existing
v0.7.0-e3a tag:

1. **Revert.** `git revert 4d0e7c3 0ec907d`. Net effect: removes the
   per-leaf find UIs, the QML FindBar, the `MarkdownView` virtuals,
   the `LiveView.qml` `Item` wrapper, and the duplicated search loops.
   Tests for the removed code go too. Fast suite expected: 217 - 4
   (source FindBar slots) - 1 (live find controller) - 1 (live
   integration find slot) = 211.
2. **Add `Markoff::FindController` + tests.** Pure addition in
   `markoff-core`. Includes the new header, source, and
   `tst_markoff_find_controller`. No leaf wiring yet. Fast suite
   expected: 211 + 1 = 212.
3. **Add per-leaf adapters + attach hooks + invariant test.**
   `Markoff::Live::Detail::LiveFindAdapter`,
   `Markoff::Source::Detail::SourceFindAdapter`, the
   `LiveCursorState::setCaretWithoutFocus` chokepoint, the
   `LiveListModelBinding::attach/detachFindController` hook, the
   `Editor::attach/detachFindController` hook, and the falsifiable
   integration test from D7. Fast suite expected: 212 + 3 = 215.

The implementation plan dating from this spec goes in
`docs/plans/2026-05-20-find-session-scope.md`; concrete file edits and
test bodies belong there.

## Risks

- **`LiveCursorState::setCaretWithoutFocus` is a new authority path** on
  the canonical cursor store. By invariant 3 ("A new authority retires
  the old one in the same plan") we need to confirm it does not create
  a third source of truth alongside `establishFocus`. Mitigation: the
  new method writes to the same canonical fields (`m_cursor`) as
  `establishFocus`; it omits only the focus-handover step. Document
  this in the method's contract and in `libs/markoff-live/CLAUDE.md`'s
  cursor section. Falsifiable: `tst_live_render_cursor` can pin that
  `setCaretWithoutFocus` followed by a typed key produces the same
  canonical-state outcome as `establishFocus` would, modulo focus.
- **Consumer not yet present.** Corbomite is the planned first consumer
  but has not consumed the foundation-exploration API yet. We are
  designing the find boundary without a real driver. Mitigation: the
  attach/detach shape is minimal and easy to evolve; if Corbomite's
  needs surface a missing hook (e.g. "highlight matches but do not
  scroll on next"), we add a flag at that point.
- **`BlockAnchor`-based matches go stale on document edit.** The
  controller subscribes to `d2DocumentChanged` and recomputes, so
  match descriptors emitted to the adapter are always fresh as of
  the most recent edit. Mitigation: adapter MUST treat the match list
  as ephemeral — clear and re-render on every `matchesChanged`.

## Open questions

1. **Ship a `Markoff::FindBar` QWidget affordance in `markoff-core`?**
   The middle ground the user identified is "API as the floor, widget
   as the convenience". This spec defers the widget. Recommendation:
   let Corbomite build its own QWidget find bar against the controller
   first. If the result is reusable, promote it to `markoff-core` in
   a follow-up freeze. If it ends up Corbomite-specific (e.g. matches
   the host's command-bar chrome), it stays in Corbomite. The
   controller does not depend on this decision.
2. **Replace flow.** Deliberately out of scope here; will need
   `Markoff::ReplaceController` (likely composing `FindController`)
   and a Cmd-level edit path. Decide when the find flow has shipped
   and has feedback.
3. **Find scope = top-level blocks only?** The current implementations
   scan only top-level block text. Inside a list, do we want to find
   inside nested ListItem blocks? Current answer: yes — D3-corrective
   makes ListItems peer blocks, so the controller's
   `iterateBlocks()` scan naturally hits them. Math LaTeX text and
   code-block content: in-scope (they are block text). Pin in the
   implementation plan.
4. **Controller ownership.** This spec puts it on the consumer. If a
   `Markoff::Session` value type later assumes ownership, the API
   does not change. We mention this in §Scope; no action required now.
