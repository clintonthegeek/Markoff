# Structural-key authority for `markoff-styled`

- **Date:** 2026-05-29
- **Status:** Design — approved for plan
- **Queue item:** #8.8 (Enter at end of bullet under heading merges bullet
  into preceding heading)
- **Seam:** focus / caret / block-change. INVARIANTS §2/§3/§4 apply
  (see [`docs/INVARIANTS.md`](../INVARIANTS.md)).

---

## 0. TL;DR

The styled leaf renders list items with a real `QTextList`. When the caret
is inside a `QTextList` block and a structural key (Enter, Backspace, Tab,
Delete) is pressed, Qt's native `QWidgetTextControl` performs *list-aware
block restructuring* — emitting large, content-rewriting `contentsChange`
events that `SourceTextDocumentBinding` (an observe-and-infer binding)
cannot reverse-engineer. It misreads them as structural flat-text edits and
mangles the model.

The fix adopts `markoff-live`'s proven pattern, adapted to a `QTextEdit`:
**own the structural keys.** Intercept them in a `QTextEdit::keyPressEvent`
override *before* native editing runs, and route them to a new pure
decision handler in `markoff-core` that mutates the model via the existing
`Cmd::*` primitives and declares the caret. Native editing is left to do
only what it is unambiguous at: within-block typing, navigation, and
selection it does not restructure.

---

## 1. Root cause (verified)

### 1.1 Symptom

Repro (queue #8.8): open a document with a heading followed by a bulleted
list; place the caret at the end of the first bullet; press Enter. The
entire bullet body is sucked into the preceding heading line, the bullet's
first character is duplicated, blocks collapse, and the caret jumps to the
end of the last bullet.

### 1.2 Mechanism

`markoff-styled`'s `StyleApplier` attaches a `QTextList` to every list-item
block (`StyleApplier.cpp:330` `manageListMembership` → `createList` /
`list->add`). The styled leaf has **no structural-key handler**: it relies
entirely on Qt's native `QTextEdit` editing plus `SourceTextDocumentBinding`
observing `QTextDocument::contentsChange` and inferring the intended model
edit (`SourceTextDocumentBinding.cpp:307` `onQtContentsChange`).

That observe-and-infer contract holds for *plain* blocks: pressing Enter at
the end of a paragraph emits exactly `contentsChange(pos, 0, 1)` — a clean
single `\n` at the caret, matching the binding's bare-Enter branch
(`SourceTextDocumentBinding.cpp:335`) which calls
`MarkoffDocument::applyInteractiveNewline`.

It does **not** hold for blocks in a `QTextList`. Qt's native Return
handling for a list item restructures the list: it removes and re-inserts
the item block(s), emitting multiple large `contentsChange(pos,
removed≈added, …)` events anchored at the *start of the list item* (not the
caret), spanning the whole item and beyond, re-inserting the item's existing
text, and carrying embedded newlines.

The binding sees `charsRemoved != 0` (so the bare-Enter branch is skipped)
and `insertedHasNewline == true`, and routes to the structural path
`applyFlatEdit(noSepStart, noSepEnd, insertedUtf8)`
(`SourceTextDocumentBinding.cpp:424`). `applyFlatEdit` then replays Qt's
internal list-restructuring as a global byte-range flat-text edit, at the
wrong coordinates (starting at the bullet, overrunning the heading
boundary) — merging the bullet into the heading, duplicating the first
character (the tell-tale off-by-one "R"), and collapsing blocks. The
corruption is fully formed on the **forward path**; the reverse-path
incremental diff faithfully mirrors the already-damaged model.

### 1.3 Evidence

A Markoff-free control (bare `QTextEdit` + `QTextList`, no Markoff code)
reproduces the event pattern exactly:

| Caret at end of… | `contentsChange` from one Enter |
|---|---|
| **list item** (in `QTextList`) | `(pos, rem≈11, add≈11)` then `(pos, rem≈23, add≈24)` — block rewrites at the item start |
| **plain paragraph** (no list) | `(pos, 0, 1)` — one clean `\n` at the caret |

The corruption reproduces headlessly in pure-ASCII, no-blockquote,
single-line-bullet fixtures, which **rules out** the three hypotheses filed
in queue #8.8:

- **H1** (sep-view off-by-one from #8.1's block-count shift): wrong trigger.
  The byte-range mishandling is real, but driven by Qt's list events, not by
  any index shift from the blockquote split.
- **H2** (reverse-path incremental diff): not the cause — the model is
  already corrupt on the forward path.
- **H3** (pre-existing, blockquote-dependent): half right — it *is*
  pre-existing, but the blockquote is irrelevant.

### 1.4 Regression provenance — NOT a #8.1 regression

The bug window opened at `845fc0f` (2026-05-29 09:58, "render
bullets/decimals via per-item `QTextList`") and widened at `511ab81`
(shared lists for continuous numbering). The queue #8.8 bisect point
`46643e7` already contains `QTextList` bullets, so the filed bisect would
have fired and (correctly) indicated "pre-existing." #8.1 (blockquote,
`fb7c79c`+, 14:42) is innocent; the user happened to dogfood right after
that push.

The deeper architectural fact: **styled is the only leaf that both uses
`QTextList` and relies on observe-and-infer.** `markoff-live` avoids
`QTextList` entirely (markers are QML elements) *and* intercepts structural
keys. `markoff-source` is a `QPlainTextEdit` showing raw markdown, so no
`QTextList` ever exists. Styled took the rich rendering without the
interception it requires.

---

## 2. Prior art in this codebase

### 2.1 `markoff-live` — the reference

`LiveStructuralKeyHandler` (`libs/markoff-live/src/LiveStructuralKeyHandler.cpp`)
intercepts every structural key at the QML delegate layer
(`UnifiedInlineTextDelegate.qml`, `Keys.priority: Keys.BeforeItem`) *before*
the native `TextEdit` runs, and routes to `Cmd::*` model mutations. If the
handler returns `Handled`, the delegate sets `event.accepted = true` and the
native editing never runs. Markers/indent/checkboxes are model attributes
rendered as separate QML `Text` elements — live never instantiates a
`QTextList`. This is the design styled will mirror.

The view-agnostic kernel of that handler — the (kind × key) → `Cmd::*`
decision rules — is what we lift into core. The `Cmd::*` primitives it calls
already live in `markoff-core` (`Cmd/D2.h`) and are reusable:
`insertSoftBreak`, `enterAtEnd`, `backspaceMerge` (returns its caret byte
offset), `deleteMerge`, `insertListItemAfter`/`Before`,
`renumberRunStartingAt`.

### 2.2 `markoff-source` — immune by construction

`QPlainTextEdit` showing literal markdown (`- ` markers as text); no
`QTextList`; structural keys go to native editing + the binding's
bare-Enter / single-block paths, which are unambiguous for plain text.
Source already intercepts only `Ctrl+Z`/`Ctrl+Y` → `undoD2`/`redoD2` via an
event filter (`libs/markoff-source/src/Editor.cpp:193`). Styled will adopt
the same undo/redo routing (§7).

### 2.3 General principle (predicts the whole bug class)

`SourceTextDocumentBinding` is an **observer**: it infers intent from
`contentsChange(pos, removed, added)`. Observation is lossy — Qt's native
*structural* operations are not uniquely recoverable from that tuple. So the
hazard is general: **any time the QTextDocument carries structure Qt
manipulates natively (`QTextList`, and in the future `QTextTable`), a
structural keystroke triggers block-restructuring the binding cannot
reverse-engineer.** Plain within-block typing, navigation, and same-block
selection edits are unambiguous and remain safely observed.

The fix therefore intercepts the bounded set of *structural* keys and leaves
everything else native. The decision table in §4 is the complete enumeration
of that set for the block kinds styled renders.

---

## 3. Architecture

Three components, with the smallest possible view-side surface.

```
markoff-core (new)
  Markoff::StructuralKeyHandler
    StructuralResult decide(MarkoffDocument& doc,
                            BlockId block, BlockKind kind,
                            uint32_t caretByteInBlock,
                            int key, Qt::KeyboardModifiers mods);
    // Pure (kind × key) decision rules. Issues Cmd::* mutations on `doc`.
    // Returns { handled, caretBlock, caretByteOffset }.
    // No QML, no widget, no LiveCursorState dependency.

markoff-core
  SourceTextDocumentBinding::handleStructuralKey(
      int key, Qt::KeyboardModifiers mods, int qtPos, int qtAnchor) -> bool;
    // 1. If qtPos != qtAnchor (non-empty selection): collapse via the
    //    extracted cross-block-delete method first (§6).
    // 2. Resolve (block, byteInBlock) from the collapsed caret via
    //    findBlockAtSepByte (already present).
    // 3. handler.decide(...); on handled, set m_pendingCaret and return true.

markoff-styled (new)
  Markoff::Styled::StructuralTextEdit : public QTextEdit
    void keyPressEvent(QKeyEvent* e) override;
    // Structural keys + Ctrl+Z/Y/Shift+Z forwarded to the binding / document.
    // Everything else -> QTextEdit::keyPressEvent(e).
```

`Markoff::Styled::Editor` swaps `new QTextEdit(this)` →
`new StructuralTextEdit(this)` (`Editor.cpp:23`) and gives the subclass a
pointer to the binding (constructor arg or setter wired in
`Editor::setDocument`).

### 3.1 Data flow (Enter at end of a bullet)

1. `StructuralTextEdit::keyPressEvent(Return)` → `binding->handleStructuralKey(Key_Return, mods, qtPos, qtAnchor)`.
2. Binding resolves `(bulletBlock, endByte)` via `findBlockAtSepByte`.
3. `StructuralKeyHandler::decide` (ListItem, Enter, caret==end) → `Cmd::insertListItemAfter` + `Cmd::renumberRunStartingAt`; returns `{handled, newItemBlock, 0}`.
4. Binding sets `m_pendingCaret = {newItemBlock, 0}`; returns `true`.
5. `keyPressEvent` calls `e->accept()` and returns — **Qt's native list machinery never runs; the corrupting `contentsChange` storm never happens.**
6. The model mutation queued a debounced `d2DocumentChanged`. The existing reverse path (`onD2DocumentChanged`) re-renders the QTextDocument incrementally and emits `caretResolved`, landing the caret in the new empty item.

No `QTimer::singleShot` / `Qt.callLater`, no new re-entrance guard. Caret
resolution reuses the exact `m_pendingCaret` mechanism that already serves
the bare-Enter path (INVARIANTS §6/§7 clean).

---

## 4. Decision table

Dispatched on the model `BlockKind` (not a text-prefix heuristic — styled's
D2 model has clean per-item ListItem blocks with post-marker buffers, kind =
`ListItem`, marker/indent in attrs). `byte` offsets are within-block UTF-8
(the binding's coordinate space); the binding converts to/from qt UTF-16.

| Kind | Enter | Shift+Enter | Backspace @start | Delete @end | Tab / Shift+Tab |
|---|---|---|---|---|---|
| **Paragraph / Heading** | @end → `enterAtEnd` (new para after, caret 0); @start → empty para *before*, caret in the **new empty para** (matches live `LiveStructuralKeyHandler.cpp:292`); mid → split (truncate + `d2InsertBlock` + suffix, caret 0 of new suffix block) | `insertSoftBreak`, caret +1 | `backspaceMerge` into prev, caret at join | `deleteMerge` next in, caret unchanged | native |
| **ListItem** | empty & indent>0 → outdent (`IndentLevel-1` + renumber); empty & indent==0 → exit to Paragraph (`d2SetBlockKind` + clear `MarkerStyle`); @start → `insertListItemBefore` + renumber; @end → `insertListItemAfter` + renumber; mid → split + `insertListItemAfter` + renumber | `insertSoftBreak`, caret +1 | indent>0 → outdent; else `backspaceMerge` into prev + renumber | `deleteMerge` + renumber | indent (with "parent item at this level exists" guard) / outdent; caret unchanged |
| **CodeBlock** | **`insertSoftBreak`** (literal `\n`, NO split — code blocks legitimately contain newlines) | `insertSoftBreak` | `backspaceMerge` into prev | `deleteMerge` next in | insert 4 spaces at caret |
| **BlockQuote** | empty → exit to Paragraph; else split keeping quote kind + depth attrs | `insertSoftBreak` | `backspaceMerge` into prev | `deleteMerge` next in | native |
| **HorizontalRule** (block-only) | empty Paragraph after | — (n/a) | delete block, caret to neighbor | delete block | — |

"Not at @start" Backspace, "not at @end" Delete, and all mid-block typing
return **NotHandled** → native within-block editing (unambiguous). Arrow-key
navigation is never intercepted (native).

Rules mirror `LiveStructuralKeyHandler.cpp:232-621`. Differences from live:
styled dispatches on `BlockKind` directly (no `rowIsListOrQuoteContent`
text-prefix guard, which exists only for live's legacy collapsed-row
regime); BlockQuote buffers are marker-stripped + depth-tagged (queue #8.1),
so the "empty quote" test and the split preserve `BlockQuoteDepth` /
`BlockQuoteRunId` attrs rather than live's `"> "` text matching.

---

## 5. Caret & authority decision (INVARIANTS §2 / §3)

- **L4, written:** for the structural keys this handler consumes, the
  **model is the sole authority** over block content and structure. The
  styled `QTextEdit` is forbidden from performing native structural edits
  for those keys; it forwards intent to the binding, the binding mutates the
  model and declares the caret (`m_pendingCaret`), and the reverse path
  re-renders. This matches live's authority model.
- **Old authority retired in the same plan:** the prior authority for these
  keys — "Qt native editing + `onQtContentsChange` observe-and-infer" — is
  retired *for the intercepted keys* by consuming them in `keyPressEvent`
  before native editing runs (so `onQtContentsChange` never receives them).
  The observe-and-infer path remains live for `markoff-source` (no
  interception) and for paste (§9). The plan includes a verification step
  asserting the binding's `applyFlatEdit` structural branch is not reached
  by styled structural keystrokes.
- **Caret authority unchanged:** reuse `m_pendingCaret` + `caretResolved`.
  No new caret store is introduced, so there is no second source of truth to
  reconcile (the failure mode INVARIANTS §3 guards against).
- **Developmental record cited:**
  [`2026-05-27-flat-view-enter-and-caret-authority-design.md`](2026-05-27-flat-view-enter-and-caret-authority-design.md),
  [`2026-05-06-per-item-listitem-blocks-design.md`](2026-05-06-per-item-listitem-blocks-design.md),
  and live's `LiveStructuralKeyHandler`.

---

## 6. Non-empty selection — collapse-then-apply

Live defers selection+structural-key to its edit-binding's `contentsChange`
path. Styled cannot: deferring to native editing across a `QTextList` is
exactly the corrupting path. So styled handles it:

1. On `handleStructuralKey` with `qtPos != qtAnchor`, first **delete the
   selected range through the model**, reusing the binding's existing
   cross-block delete logic.
2. Collapse the caret to the join point returned by that delete.
3. Dispatch the structural op (§4) at the collapsed caret.

**Refactor required:** the cross-block delete currently lives inline in
`onQtContentsChange` (`SourceTextDocumentBinding.cpp:381-418`). Extract it
into a private method, e.g.
`PendingCaret deleteSepRange(quint32 sepLo, quint32 sepHi)` (or
`(BlockId, byteOffset)` result), called by both the observer path and
`handleStructuralKey`. This consolidates the tested delete logic in one
place rather than duplicating it.

**Transaction note:** where the chosen `Cmd::*` primitive opens its own
transaction (`backspaceMerge` does — see live's documented two-undo-entry
caveat at `LiveStructuralKeyHandler.cpp:537-543`), the collapse + structural
op may produce more than one undo entry. We aim for a single
`UndoLog::Transaction` where the primitives accept one, and accept and
document the multi-entry case where they do not (matching live; a future
`backspaceMerge(…, Transaction&)` overload closes it for both leaves).

---

## 7. Undo / redo (included)

Styled currently has **no** `Ctrl+Z` handling, and `QTextDocument`'s own
undo stack is disabled (`SourceTextDocumentBinding.cpp:216`
`setUndoRedoEnabled(false)`), so undo is silently dead in styled today. The
`StructuralTextEdit::keyPressEvent` override routes:

- `Ctrl+Z` → `document()->undoD2()`
- `Ctrl+Y` or `Ctrl+Shift+Z` → `document()->redoD2()`

matching `markoff-source`'s event filter. After undo/redo the model emits
`d2DocumentChanged` and the reverse path re-renders; caret restoration on
undo/redo is the known §B.2/§B.4 partial in the View Implementor's Guide and
is **not** expanded here.

---

## 8. Testing (falsifiable-first — INVARIANTS §4)

1. **Promote the repro.** The headless `tst_styled_c1_repro` fixture built
   during diagnosis becomes a permanent slot in
   `tst_styled_dogfood_invariants`: load a heading + multi-line bulleted
   list (inline markdown, no filesystem dependency), place the caret at the
   end of the first bullet, press Enter; assert (a) the heading block's text
   is unchanged, (b) a new empty `ListItem` block exists between the two
   bullets, (c) the caret is at its start, (d) `widgetFlatView() ==
   toPlainText()`. Confirmed to FAIL on HEAD before the fix.
2. **Per-row structural tests**, mirroring live's `tst_live_render_structural`
   coverage, one per cell of §4 that styled implements: list outdent,
   exit-list, item-before, item-after, mid-split + renumber, Tab indent (and
   the parent-exists guard), Shift+Tab outdent, Backspace-merge, Delete-merge,
   CodeBlock-Enter-soft-break-not-split, BlockQuote exit/split.
3. **Selection cases** (§6): selection within one block + Enter; selection
   spanning two list items + Enter; selection + Backspace — each asserts
   collapse-then-apply lands the right structure and caret.
4. **Undo/redo**: a structural edit followed by `Ctrl+Z` restores the prior
   structure; `Ctrl+Y` reapplies.
5. **Falsifiability proof:** before wiring production, break the target seam
   in a throwaway stub and confirm the new tests fail (INVARIANTS §4); fix
   the test if it does not.

All under `QT_QPA_PLATFORM=offscreen` via `scripts/run-tests.sh`.

---

## 9. Scope boundaries / non-goals

- **Paste & drag-drop** are *not* keystrokes; they flow through
  `insertFromMimeData` → `contentsChange` → the binding's `applyFlatEdit`
  path, and through `Cmd::pasteMarkdown` where wired. They are a separate
  surface (clipboard `QAction`s + the observer path) and are out of scope
  here. If rich-text/multi-block paste shows analogous corruption, it gets
  its own queue item.
- **Formatting chords** (Bold/Italic/Heading/etc.) are `QAction` +
  `setShortcut` work (styled has none today); not part of this structural-key
  spec. Listed as a future leaf-parity item.
- **`QTextTable`:** tables are QML-only in `markoff-live` (separate
  `TableEditBinding`, never touches this binding) and are unrendered in
  styled (v0.2+, and even then planned as styled pipe-text, not native
  `QTextTable`). This fix is orthogonal to current tables. **Prediction:**
  if styled ever wants *editable* tables, native `QTextTable` would reproduce
  this exact bug class far more aggressively (Tab adds rows, Enter splits
  cells). The mandated principle, recorded here: extend the
  intercept-structural-keys approach and render/manage tables explicitly
  (mirror live's cell-delegate model) — **do not** rely on native
  `QTextTable` + observer inference.
- **KActions** are out — `markoff-styled` has a hard "no KF6" rule. Plain
  `QAction` only, where actions are eventually needed.

---

## 10. Files touched (anticipated)

- `libs/markoff-core/include/markoff/core/StructuralKeyHandler.h` (new)
- `libs/markoff-core/src/StructuralKeyHandler.cpp` (new)
- `libs/markoff-core/include/markoff/core/SourceTextDocumentBinding.h`
  (`handleStructuralKey`, extracted `deleteSepRange`)
- `libs/markoff-core/src/SourceTextDocumentBinding.cpp` (same)
- `libs/markoff-styled/src/StructuralTextEdit.{h,cpp}` (new)
- `libs/markoff-styled/src/Editor.cpp` (swap the `QTextEdit` instance; wire
  the binding pointer)
- `libs/markoff-styled/tests/tst_styled_dogfood_invariants.cpp` (+ new
  structural test binary if the slot count warrants its own file)
- `libs/markoff-core/tests/…` for the core handler unit tests
- CMakeLists updates for the new sources/tests

---

## 11. Open risks

- **`StructuralTextEdit` ↔ binding wiring order.** The subclass needs a
  valid binding pointer before the first keystroke; wire it in
  `Editor::setDocument` alongside the existing binding setup, and null-guard
  `handleStructuralKey` forwarding.
- **`inputMethodEvent` / IME.** Composition commits arrive via
  `inputMethodEvent`, not `keyPressEvent`; they are plain insertions and stay
  on the observer path. Confirm Enter during preedit is not double-handled
  (preedit Enter typically commits composition, then a separate Return — the
  test suite's emoji/IME mirrors in live are the reference).
- **AutoKeyRepeat for Tab/Backspace** held down — each repeat is its own
  `keyPressEvent`; the per-press model mutations + debounced re-render are
  expected to coalesce acceptably, but watch for caret lag under fast repeat.
