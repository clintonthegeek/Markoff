# Markoff::Editor key-dispatch architecture — soak-week-surfaced flaw

**Status:** BLOCKING C7. Must be resolved before Phase C7 starts.
**Priority:** High. Bandage shipped in `v0.6.0-alpha.8` as `Editor::m_inKeyPressEvent` re-entrance guard; real fix still owed.
**Origin:** Surfaced 2026-04-21 during `v0.6.0` soak-week dogfooding. Sixth of six consecutive typing-triggered SEGVs. The first five were C3-integration-specific; this one is pre-existing but was masked by pre-C3 tests that never exercised the bubble path.

---

## TL;DR

`Markoff::Editor` has a long-standing architectural contradiction: it sets itself as the **focus proxy target**'s parent (so key events naturally propagate back to Editor on bubble) AND its own `keyPressEvent` **manually forwards** events to the focus proxy target via `QApplication::sendEvent`. Unaccepted keys (bare modifiers, unbound shortcut keys, etc.) therefore loop forever between Editor and its focus proxy. A re-entrance guard is in place as a bandage; the proper fix requires re-organising how Editor dispatches keys, which touches every key-driven feature of Live mode.

---

## The flaw

Editor's constructor sets up keyboard routing this way:

```cpp
// libs/markoff-live/src/Editor.cpp (ctor)
m_view = new EditorGraphicsView(this);      // m_view's QWidget parent = Editor
m_view->installEventFilter(this);
m_view->viewport()->installEventFilter(this);
m_view->setFocusPolicy(Qt::StrongFocus);
setFocusProxy(m_view);                      // focus requests to Editor → m_view
```

And then `Editor::keyPressEvent` does:

```cpp
// libs/markoff-live/src/Editor.cpp line ~836
QApplication::sendEvent(m_view, e);          // manually forward to m_view
```

These two statements contradict each other:

1. **Focus proxy** instructs Qt: "when anything wants to focus Editor, focus m_view instead, and deliver keys directly to m_view." Under this contract, `Editor::keyPressEvent` should never be called — keys land on `m_view` via Qt's focus delivery.

2. **Manual `sendEvent`** sits inside `Editor::keyPressEvent` explicitly to forward the key to m_view. Under this contract, `Editor::keyPressEvent` IS called, and forwards down.

Only one of these should be true. Both being true is the flaw.

## The loop

When both are true, any key event that `m_view`'s focused `MarkdownTextItem`+`TextControl` does NOT accept (e.g. bare `Qt::Key_Shift`, unbound shortcut keys, `Qt::Key_AltGr`) propagates like this:

```
user presses bare Shift
  → Qt delivers KeyPress to m_view (focus is there via proxy)
  → m_view.keyPressEvent sends to scene → focused item → TextControl
  → TextControl ignores bare modifier (leaves e->isAccepted() = false)
  → m_view.keyPressEvent returns without accepting
  → Qt's default widget event propagation: unaccepted key bubbles to
    m_view's QWidget parent, which IS Editor
  → Editor::event receives KeyPress → dispatches to Editor::keyPressEvent
  → Editor::keyPressEvent calls QApplication::sendEvent(m_view, e)
  → ... same chain repeats infinitely ...
  → stack overflow → SIGSEGV inside whatever Qt layout call happens
    to be on top of the stack when the guard page trips
```

Soak-week dogfood reproduced this ~once per session, always on a bare modifier press or an otherwise-unclaimed key that came after several seconds of legitimate typing.

## Stakeholder breadth

`Markoff::Editor` is the Live mode widget. This key-dispatch path is load-bearing for every key-driven feature:

- **Typing** — characters flow to `TextControl` inside focused `MarkdownTextItem`
- **Tab smart-indent** — Editor catches Tab/Backtab at `Editor::event` line 427, handled specially before reaching m_view
- **Ctrl+Home / Ctrl+End** — Editor-level `jumpToDocumentEdge` before sendEvent
- **PageUp / PageDown** — Editor-level `pageUpDown` before sendEvent
- **Selection extension** (Shift+arrow etc.) — routed via SelectionScene/SelectionManager after reaching scene
- **IME composition** — dispatched through TextControl via m_view; composition commits route back through outbound MarkdownDelta path
- **QShortcut / QAction** app-level shortcuts — dispatched by Qt's shortcut-override chain, which itself WALKS up the widget parent chain and interacts with both Editor and m_view
- **CJK autocorrect** — `MarkdownTextItem::keyPressEvent` calls back after `m_control->processEvent`
- **Cursor-at-boundary detection** — same
- **Context menu** — `Editor::contextMenuEvent` via scene, but key-triggered menu (Menu key) goes through this path too
- **Read-only state** — `Editor::setReadOnly` affects interaction flags, but the key dispatch structure above still runs regardless
- **Event filtering** — Editor installs itself as event filter on m_view AND m_view->viewport() for context menus, mouse moves, wheel events

Any change to the key-dispatch structure affects all of these. That's why this is "soak-week scope but requires code review" — not something to hack in overnight.

## Three fix options

### Option A: remove the manual `sendEvent`, rely on focus proxy

**Change:** Delete `Editor::keyPressEvent`'s `QApplication::sendEvent(m_view, e)` line. Keep Editor's own handling for Tab/Backtab/Ctrl+Home/Ctrl+End/PageUp/Down, but when Editor doesn't handle the key, call `QWidget::keyPressEvent(e)` (default) which leaves `e` unaccepted for natural Qt propagation UP (to NoteEditorWidget etc., not back to m_view).

**Why it works:** With focus proxy set, Qt delivers keys to m_view directly. Editor's keyPressEvent is only called for bubble-up paths, which we don't want to re-deliver. The default `QWidget::keyPressEvent` path propagates correctly up the widget parent chain (past m_view, since we're past it in the bubble).

**Tradeoff:** Editor's handling of Tab/Ctrl+Home/PageUp/Down is via `Editor::event` override (Tab path at line 427) + inline ifs in `keyPressEvent`. If we remove the sendEvent, keyPressEvent's inline ifs can still fire when a key bubbles UP from m_view's default processing. That's new behavior — Tab/Ctrl+Home/PageUp/Down currently fire BEFORE sendEvent. If m_view's scene processes them first and accepts, Editor's handler never runs. Changing that order needs review.

**Scope:** ~20 lines in Editor.cpp + test that Tab/Home/End/PageUp/Down still work + manual test of IME / bare modifiers / unbound shortcuts.

### Option B: reparent `m_view` away from Editor

**Change:** Construct `m_view` with `nullptr` parent (or some other parent), then embed it in Editor via `QVBoxLayout::addWidget` without Qt-parent relationship.

**Why it works:** Unaccepted events bubble up the widget parent chain. If m_view's parent isn't Editor, bubbles never reach Editor::keyPressEvent, so no recursion.

**Tradeoff:** QGraphicsView embedded in a layout without Qt parent is unusual. Affects lifetime (must manage manually or use unique_ptr). Affects window-related Qt machinery (transient parent for popups, etc.). More invasive than it looks.

**Scope:** Larger. Ownership + lifetime + resize + layout integration to audit.

### Option C: drop the focus proxy

**Change:** Remove `setFocusProxy(m_view)`. Editor owns focus itself. Keys naturally arrive at Editor::keyPressEvent. Forward to m_view via sendEvent OR via an explicit scene dispatch. Careful handling needed for QGraphicsView focus semantics — without focus proxy, m_view doesn't get focus, which means scene items don't get focus either, which means TextControl doesn't know it has keyboard focus for cursor blink etc.

**Why it works:** Avoids bubble-back-to-Editor because m_view isn't the focused widget; it's a display-only child.

**Tradeoff:** Breaks scene focus handling — need to route focus-in/out events manually from Editor to scene's focus item. Breaks cursor blink, selection paint hints, etc. Worst of the three.

**Scope:** Medium+. Scene focus plumbing needs rewrite.

## Recommendation

**Option A.** Smallest footprint, cleanest semantic, matches Qt's native convention for focus-proxy widgets. Full preparation:

1. Spec: this doc, plus decide Tab/Home/End/PageUp/Down ordering (does Editor handle before or after m_view gets the chance?).
2. Migration test: write a test that sends bare `Qt::Key_Shift` to Editor, asserts no infinite loop (would have caught this bug).
3. Test pass: verify Tab, Ctrl+Home, Ctrl+End, PageUp, PageDown, character input, IME commit, Ctrl+Z/Y, bare-modifier-no-op, unbound-shortcut-no-op, bound-QAction-shortcut (Ctrl+A, Ctrl+C, etc.), context-menu-key, arrow keys, selection extension.
4. Implement.
5. Remove the bandage (`m_inKeyPressEvent` + the guard block in `Editor::keyPressEvent`).
6. Re-tag as `v0.6.1` once the dogfood week closes.

## The bandage (`v0.6.0-alpha.8`)

Committed as-is at `b58b96d` on the Markoff submodule master. The guard:

```cpp
// At the top of Editor::keyPressEvent:
if (m_inKeyPressEvent) {
    e->accept();
    return;
}
struct GuardRAII { bool &g; GuardRAII(bool &b) : g(b) { g = true; } ~GuardRAII() { g = false; } } guard(m_inKeyPressEvent);
```

**What the bandage preserves:** Every intentional Editor key-handling path — Tab, Ctrl+Home, Ctrl+End, PageUp, PageDown, character input, selection, IME.

**What the bandage breaks:** Bare-modifier-based app shortcuts that depend on the KeyPress bubbling past Editor to a higher handler. Unbound shortcut keys that were relying on bubble-up (observable in theory; not observed in practice since app-level shortcuts use Qt's shortcut-override dispatch path which runs BEFORE keyPressEvent).

**What the bandage costs architecturally:** The flaw is still there. The re-entrance guard makes it observable only as "nothing happens when you press an unhandled key" instead of "SEGV." The code is still structurally wrong. Every future touch to `Editor::keyPressEvent` has to remember the guard exists and not work around it; every future reader has to understand why the guard is there before modifying.

## Why this blocks C7

C7 is **Source feature completion (find/replace API + fold-gutter)**. Its implementation adds:

- New key bindings in Live (Ctrl+F for search, F3 for find-next, etc.)
- Find-bar focus management (keys route between find bar and editor)
- Fold-gutter key interactions (Alt+Arrow, maybe)

Every one of these adds new key-dispatch paths through `Editor::keyPressEvent`. Adding them on top of the bandage means:

1. Each new binding has to be checked against the re-entrance guard (does it fire before or after? does the guard still let it through on bubble?)
2. Every new binding that inadvertently becomes "unaccepted" triggers the guard silently — a regression that's hard to notice (the bandage makes failures quiet, not loud).
3. Find/replace uses modifier keys heavily (Ctrl+F, Ctrl+H, Esc, Shift+F3) — exactly the class of keys the bandage masks.

If we ship C7 atop the bandage, we bake the flaw into another layer of surface. Cutting through it later means fixing Editor's key dispatch AND updating C7's bindings AND re-auditing them — same work but more of it.

Fix the flaw before C7.

## Tracking

- This spec at `libs/markoff-family/docs/specs/2026-04-21-editor-key-dispatch-architecture.md`.
- Phase C status board entry (to be added) marks C7 as BLOCKED on this spec's resolution.
- Re-tag from `v0.6.0-alpha.8` to `v0.6.1` only after the proper fix (Option A) lands and the bandage is removed. Do NOT cut `v0.6.1` with the bandage in place; that cements the architectural debt into a release milestone.
