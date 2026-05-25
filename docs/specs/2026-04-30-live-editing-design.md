# Live editing — design

**Date:** 2026-04-30
**Branch:** `exploration/new-foundation`
**Phase:** Phase-2 of `markoff-view-qml`, follow-on to the read-only Live Render walking skeleton (`docs/specs/2026-04-29-live-render-design.md`)
**Status:** approved (pending user spec review)
**Depends on:** `docs/specs/2026-04-30-block-anchor-foundation-design.md` — the foundation `BlockAnchor` + `TextAnchor` API ships first; the implementation plan for this spec only starts after that foundation work is in flight.

## 0. TL;DR

Turn the read-only `LiveView` into a structurally-complete editor: type into existing text-bearing delegates, cross-block structural edits (Backspace-joins, Enter-splits, multi-block selection-replace), structural promotion via parser truth (with two narrow speculative paths — open inline delimiters and code-block fences), markdown-source-on-the-wire clipboard, and shared cross-mode undo + selection through `Session`.

The editing spec also retires three pieces of walking-skeleton tech debt: `TextEdit.MarkdownText` is replaced by a markoff-parser-AST-driven `QTextCharFormat` highlighter; `AstBlockDiff`'s content-hash `BlockKey` is replaced by `(kind, BlockAnchor)`; `LiveSelectionModel` is replaced by `LiveSelectionView` that projects `Session::primarySelection`.

The architecture commits to **Approach 3 from day one for editing**: each delegate's text *is* the source bytes for its byte range, so qtPos translates to source offset directly. Inline rendering is provided by a custom highlighter, never by Qt's MD parser. The widget's job is to render markdown beautifully using markoff's own parser; Qt's built-in markdown rendering has no place.

**v0 scope:** typing, cross-block structural edits, clipboard (cut/copy/paste of markdown source), undo/redo, IME, cross-mode selection, app-layer save with version-derived dirty tracking. **Explicitly out:** Ctrl+B/Ctrl+I/Ctrl+K format toggles, cursor-aware delimiter hiding, edits to image/HR delegates, HTML→MD conversion on paste, image paste, drag-and-drop, spell-check, math/mermaid/table/list/blockquote/callout/frontmatter delegates.

---

## 1. Architecture overview

**Shape.** No new top-level QML component. `LiveView.qml` from the walking skeleton stays; its delegates flip from `readOnly: true` to `readOnly: false` and pick up a per-delegate cycle-guarded edit binding mirroring `SourceTextDocumentBinding`'s pattern. New C++ pieces sit alongside the existing `LiveBlockModel` / `LiveListModelBinding`: `LiveEditBinding` (per-delegate write-back, top-level structural-key intercept, speculative styling controller) and `LiveSelectionView` (replaces `LiveSelectionModel`, projects `Session::primarySelection`).

**Render pipeline (no Qt MarkdownText).** The walking skeleton's `TextEdit.MarkdownText` is retired by this spec. Every text-bearing delegate (`ParagraphDelegate`, `HeadingDelegate`, `CodeBlockDelegate`) uses `textFormat: TextEdit.PlainText`, with inline formatting painted via `QTextCharFormat` ranges by a markoff-parser-AST-driven highlighter (`InlineFormatHighlighter`). The highlighter mirrors `SourceEditor.qml`'s `Kf6SyntaxHighlightService` wiring but is driven by markoff-parser's inline tree instead of KSyntaxHighlighting's regex grammar. Source markers (`**`, `_`, `` ` ``, `~~`, `==`, link brackets) stay visible at all times in v0.

**Edit translation (Approach 3).** Each delegate's text role is the **source bytes** for its block's byte range `[block_start, next_block_start)`. qtPos in the delegate's TextEdit translates to a canonical source offset via `block_start + qtPosUtf8`, where `qtPosUtf8` is qtPos converted from UTF-16 to UTF-8 (the existing helper in `EditorBackend` is reused). No reverse-mapping from rendered position to source position is ever needed — the delegate's QTextDocument and the canonical source agree byte-for-byte modulo encoding.

**Identity (γ-CRDT-anchor).** `AstBlockDiff`'s `BlockKey` becomes `(kind, BlockAnchor)`, where `BlockAnchor` is the foundation's stable per-block handle (see foundation spec). The diff is plain LCS over identity keys; rows whose `BlockAnchor` is unchanged through an edit cycle are emitted as `dataChanged`, never `delete+insert`. This eliminates the walking skeleton's content-hash churn under typing.

**Source-faithful gap model.** Each delegate's byte range is `[block_start, next_block_start)` — meaning each delegate **owns its trailing whitespace** including any blank-line newlines that separate it from the next block. The visible "gap" between rendered blocks is the literal blank-line region inside the preceding delegate's TextEdit. Markdown has no implicit paragraph breaks; the cursor lands on real source bytes. This design does not foreclose a future "WYSIWG paragraph-aware mode" toggle: the gap-routing rule lives in `LiveView`, not in delegates, so flipping a future toggle costs one rule change.

**Speculative styling.** Two narrow speculative paths sit on top of the parser-driven baseline:

1. **Inline open delimiters (renderer-side).** `InlineFormatHighlighter` recognises open `**`, `__`, `*`, `_`, `` ` ``, `~~`, `==` ahead of the parser and styles trailing content speculatively. When the parser confirms by emitting a real inline node on close, speculative styling is replaced by parser-confirmed styling — no visual snap when the rendering is identical.
2. **Code-block fence (editor-side kind change).** Typing ```` ``` ```` (or `~~~`) on its own line in a paragraph delegate flips the row's kind to code-block immediately via a `LiveBlockModel::speculativelyChangeKind(row, code)` call. ListView's `DelegateChooser` swaps `ParagraphDelegate` for `CodeBlockDelegate`; subsequent typing routes to the code-block delegate. The parser confirms on close with a real `fenced_code_block` node; speculative state aligns with parser truth on parse return.

All other block-kind changes (heading prefix `# `, HR `---`, etc.) go via the parser only. Sub-millisecond incremental parse makes the round-trip invisible.

**Selection (Session-canonical).** `LiveSelectionModel` retires. `LiveSelectionView` subscribes to `Session::primarySelectionChanged` and projects `(anchor, active)` to per-delegate ranges via foundation translation APIs. Mouse drag in `LiveView`'s top-level `MouseArea` writes to `Session::setPrimarySelection`. Mode toggle carries selection automatically because both modes read the same `Session`.

**Undo (foundation-driven).** Every `applyLocalEdit` is a candidate for `coalesceLastUndo` per a view-side policy: consecutive printables in same focus context coalesce; broken by non-printables, movement, mode switch, paste, structural change, idle threshold (1 second). The undo stack lives on `MarkoffDocument` and is shared cross-mode by construction. Cursor restoration on undo derives from the returned `CollabText::Crdt::Operation` to land at the edit site.

**IME.** `applyLocalEdit` defers until Qt commits the composed text — preedit lives in TextEdit transparently. While composition is active on a row, the cycle-guarded binding suppresses parse-driven `dataChanged` text-content updates for that row (highlight-format updates can still apply). On `compositionEnded`, deferred updates apply.

**Save / dirty (app-layer).** No save logic in the view library. Foundation already exposes `MarkoffDocument::version()` and `contentsChanged`. App composes dirty as `currentVersion != lastSavedVersion`. The `markoff-testapp` host gets a tiny Ctrl+S handler, window-title `[modified]` indicator, and close-prompt-on-dirty.

---

## 2. File structure & components

### C++ side, `markoff-view-qml`

| File | Responsibility |
|---|---|
| `src/LiveEditBinding.{h,cpp}` (new) | Per-delegate cycle-guarded write path. Owns one binding per text-bearing delegate Item; mirrors `SourceTextDocumentBinding`'s reentrancy guard. Translates `TextEdit::contentsChange(qtPos, charsRemoved, charsAdded)` to `MarkoffDocument::applyLocalEdit` against the delegate's current byte range. Calls `MarkoffDocument::coalesceLastUndo()` per the undo grouping policy. Owns the focus-context state machine for undo grouping. |
| `src/LiveStructuralKeyHandler.{h,cpp}` (new) | Top-level QML `Keys` handler attached to `LiveView`. Intercepts: Backspace at offset 0 of any delegate (route to delete prev byte); Delete at end-of-block (route to delete first byte of next block); Enter at offsets handled per Q4 semantics; Ctrl+Home/End/arrow crossings; Tab inside code block. Delegates that need to consume the key (intra-block typing) handle it natively before this handler sees it; this handler runs only for unhandled events. |
| `src/LiveSelectionView.{h,cpp}` (new, replaces `LiveSelectionModel`) | `QObject` exposed to QML. Subscribes to `Session::primarySelectionChanged`. For each block, translates the global `(anchor, active)` selection to a per-delegate range via `MarkoffDocument::blockAt` + `offsetInBlock`. Provides `Q_INVOKABLE rangeForBlock(BlockAnchor)` for delegate `Connections`. Mouse-drag writes (via the `MouseArea` in `LiveView`) call `setPrimarySelection`. The walking-skeleton `LiveSelectionModel` is deleted. |
| `src/LiveSpeculativeFenceController.{h,cpp}` (new) | Watches `LiveEditBinding`'s contentsChange events on rows of kind `paragraph`. Detects an open ```` ``` ```` (or `~~~`) on its own line with no prior unclosed fence in the doc. On detection, calls `LiveBlockModel::speculativelyChangeKind(row, code)`. On parse return that confirms or rejects the speculation, reconciles. |
| `src/InlineFormatHighlighter.{h,cpp}` (new) | A `QObject` per delegate's TextEdit. Reads the parser's inline tree for the delegate's block and applies `QTextCharFormat` ranges via the TextEdit's `QTextDocument`. Adds open-delimiter speculative styling: scans the delegate's text for unclosed `**`/`__`/`*`/`_`/`` ` ``/`~~`/`==` and applies bold/italic/code/strike/highlight formatting between the open delimiter and end-of-block (or until the next character that breaks the run). Speculative ranges are visually identical to confirmed ranges so transitions are seamless. |
| `src/LiveClipboardController.{h,cpp}` (new) | Handles cut/copy/paste. Copy serializes the current `Session::primarySelection` to markdown source via `MarkoffDocument::contents` + selection range. Cut copies + applies a delete edit. Paste reads the clipboard's `text/plain`, applies an insert/replace edit at current selection. No HTML→MD or image handling in v0. |
| `src/LiveBlockModel.{h,cpp}` (modify) | `BlockKey` field changes from `(kind, content-hash)` to `(kind, BlockAnchor)`. Add `speculativelyChangeKind(row, kind)` for the fence controller. The `kind` role's data source remains string-keyed. |
| `src/AstBlockDiff.{h,cpp}` (modify) | Operates on the new `(kind, BlockAnchor)` BlockKey. The diff algorithm is unchanged shape (LCS), but identity equality is now anchor-based. Move detection (`Move` op) becomes more accurate because anchors survive across position shifts. |
| `src/LiveListModelBinding.{h,cpp}` (modify) | When `parseUpdatedAt` arrives, the BlockKey list is rebuilt with `(kind, BlockAnchor)`. The binding tracks "last applied edit version" so it can compare incoming parse versions. Speculative-kind state from `LiveSpeculativeFenceController` is merged with parser truth on each parse return. |
| `src/LiveContextMenuHandler.{h,cpp}` (modify) | Add Cut + Paste actions. Both wired through `LiveClipboardController`. |
| `src/EditorBackend.{h,cpp}` (modify) | Add `Q_INVOKABLE` for `LiveSelectionView`'s consumption of foundation translation APIs (so QML can stay decoupled from the foundation header). |

### QML side

| File | Change |
|---|---|
| `qml/LiveView.qml` | Top-level `Keys` handler installs `LiveStructuralKeyHandler`. Right-click handler now passes Cut + Paste availability flags to `LiveContextMenuHandler`. The `MouseArea`'s drag writes go through `LiveSelectionView` instead of `LiveSelectionModel`. |
| `qml/delegates/ParagraphDelegate.qml` | `readOnly: false`. `textFormat: TextEdit.PlainText`. Replace `TextEdit.MarkdownText` with `InlineFormatHighlighter` attached. Per-delegate `LiveEditBinding` instance. `Connections` now read from `LiveSelectionView`. |
| `qml/delegates/HeadingDelegate.qml` | Same changes as paragraph. Heading font sizing remains theme-driven on the block level (kind-driven), independent of inline rendering. |
| `qml/delegates/HorizontalRuleDelegate.qml` | Unchanged in shape but: clicks on this delegate route focus to the preceding text delegate per Q6. No editing in v0. |
| `qml/delegates/ImageDelegate.qml` | Same: clicks route focus to preceding text delegate. No editing in v0. |
| `qml/delegates/CodeBlockDelegate.qml` | `readOnly: false`. Continue to use `org.kde.syntaxhighlighting` `SyntaxHighlighter` keyed by `codeLanguage` for code content (this isn't markdown rendering — it's source code highlighting, which is the right job for KSyntaxHighlighting). Tab key inside the code block inserts a literal tab. Enter inserts a literal newline (does not split the block). Per-delegate `LiveEditBinding`. |

### Foundation-side dependencies (consolidated)

These are owned by the foundation spec but the editing spec calls them out as preconditions. See `docs/specs/2026-04-30-block-anchor-foundation-design.md`.

- New opaque types: `Markoff::BlockAnchor`, `Markoff::TextAnchor` (the latter wrapping or typedef'ing `CollabText::Crdt::Anchor` so view headers don't include `<crdt/Anchor.h>`).
- New `MarkoffDocument` APIs: `blockAt(TextAnchor) → BlockAnchor`, `offsetInBlock(BlockAnchor, TextAnchor) → int`, `anchorAt(BlockAnchor, int offset) → TextAnchor`.
- New `parseUpdated` signal payload includes per-block `BlockAnchor` for each top-level block in the parsed AST.

Already-existing foundation APIs the editing spec consumes verbatim:

- `MarkoffDocument::applyLocalEdit`, `undo`, `redo`, `undoDepth`, `coalesceLastUndo`, `version()`, `toMarkdownUtf8()`, `anchorAt(byteOffset, bias)`, `resolveAnchor(Anchor)`.
- `MarkoffDocument::contentsChanged` and `parseUpdated` signals.
- `Session::primarySelection`, `setPrimarySelection`, `primarySelectionChanged`.
- `Selection` struct (`anchor`, `active`).
- `Origin::FirstOpen` etc.

### Test app side

| File | Change |
|---|---|
| `app/main.cpp` | Add Ctrl+S handler that writes `markoffDocument.toMarkdownUtf8()` to the file path (or a Save-As dialog if no path). Track `lastSavedVersion`; emit a `dirty` property change on `contentsChanged`. Window title shows `[modified]` when dirty. Close-prompt on dirty. ~30 lines. |

### Tests

| File | Coverage |
|---|---|
| `tests/tst_view_qml_live_edit_binding.cpp` (new) | Per-delegate cycle-guarded write path. Type a character → `applyLocalEdit` fires with the right edit. Reentrancy: applying an edit doesn't recursively trigger another. UTF-16 → UTF-8 offset translation. Multi-character paste in one go. |
| `tests/tst_view_qml_live_structural_keys.cpp` (new) | Backspace at offset 0 deletes prev byte; multiple presses eat through `\n\n` separator until merge. Enter at end inserts `\n` once, again inserts another `\n` and splits via parser. Enter mid-block splits with kind preservation rules from Q4. Inside code block: Enter inserts literal newline. |
| `tests/tst_view_qml_live_speculative_fence.cpp` (new) | Typing ```` ``` ```` on its own line in a paragraph row flips kind to code-block before parser returns. Parser confirms on close. Reconciliation when the speculation is wrong (e.g. user undoes the fence). |
| `tests/tst_view_qml_inline_format_highlighter.cpp` (new) | Open `**` styles trailing content as bold; closing `**` confirms. Nested `**bold *italic***`. `==highlight==`. `~~strike~~`. Inline code does not interpret nested formatting. |
| `tests/tst_view_qml_live_selection_view.cpp` (new, replaces existing `tst_view_qml_live_selection_model.cpp`) | Selection projection from `Session::primarySelection` to per-delegate ranges. Cross-block selection correctness. `setPrimarySelection` round-trip. Mode-toggle preserves selection. |
| `tests/tst_view_qml_live_clipboard.cpp` (new) | Copy a single-block selection: clipboard has source markdown bytes for that range. Copy a cross-block selection: clipboard has the full source markdown including structural separators. Cut applies the delete. Paste at empty doc; paste replacing a selection; paste of multi-line markdown that promotes a paragraph to heading after reparse. |
| `tests/tst_view_qml_live_undo.cpp` (new) | Coalesce policy: typing 5 characters then Ctrl+Z undoes all 5 as one entry. Idle threshold breaks the group. Movement breaks the group. Cross-mode: type in Live, switch to Source, Ctrl+Z undoes the Live edit. |
| `tests/tst_view_qml_live_ime.cpp` (new) | Compose Korean syllable; preedit visible in TextEdit; no `applyLocalEdit` until commit. During composition, parse-driven `dataChanged` for the same row is deferred. After `compositionEnded`, deferred update applies. |
| `tests/tst_view_qml_ast_block_diff.cpp` (modify) | BlockKey now `(kind, BlockAnchor)`. Identity-stable cases: typing in row N produces `dataChanged` only, not `delete+insert`. Splits/merges identified by anchor continuity. |
| `tests/tst_view_qml_live_view_qml.cpp` (modify) | Re-target the QML smoke test to exercise the editing path: type into a paragraph, observe the doc mutating; structural key intercept fires; Cut/Paste round-trip. |

**Total new files:** 6 C++ headers + 6 .cpp + 8 test files = 20 new source files. **Modified:** 6 existing C++ files + 5 existing QML files + 1 test file + 1 app source file = 13.

---

## 3. Data flow

### Local typing inside one delegate (the hot path)

```
user types 'h' in ParagraphDelegate of row N (BlockAnchor A_N)
  → TextEdit.contentsChange(qtPos, removed=0, added=1)
  → LiveEditBinding (per-delegate)
      • cycle-guard: was reentered? bail
      • compute canonical edit: byte_offset = A_N.byteRange.start + utf16ToUtf8(qtPos)
      • applyLocalEdit(MarkoffEdit{byte_offset, 0, "h"})
      • returns Operation; broadcast hook is foundation's concern
      • per undo grouping policy: coalesceLastUndo() if previous keystroke was a printable in same context
  → MarkoffDocument emits contentsChanged(edits)
  → ParsePool schedules an incremental parse (sub-ms typical)
  → ParsePool worker: incremental parse → parseUpdatedAt(parsed, atVersion)
  → LiveListModelBinding receives parse result:
      • walks AST → BlockKey list with (kind, BlockAnchor)
      • runs AstBlockDiff vs prior list
      • for row N: anchor unchanged, content updated → dataChanged(roles=[text])
      • model emits dataChanged
  → ParagraphDelegate's TextEdit receives new text (from role data)
      • LiveEditBinding's "ignore my own write-back" guard suppresses recursion
      • InlineFormatHighlighter re-runs over the new text + new inline tree, applies QTextCharFormat ranges
      • cursor position preserved (delegate Item is unchanged; only its text content + format spans change)
```

### Backspace at offset 0 of row N (cross-block structural)

```
user presses Backspace, cursor at qtPos=0 in delegate N
  → TextEdit's native Backspace would do nothing (no prev character in the delegate)
  → Keys.onPressed at LiveStructuralKeyHandler: detects Backspace + qtPos==0 + has prev block
      • compute target: last byte of block N-1's range == byte_offset
      • applyLocalEdit(MarkoffEdit{byte_offset - 1, 1, ""})
      • event accepted; TextEdit doesn't see it
  → contentsChanged → ParsePool → parseUpdatedAt
  → AstBlockDiff: A_N's block range now starts one byte earlier; if separator was reduced to single \n,
    parser merged A_(N-1) and A_N into one paragraph (BlockAnchor of one survives, the other is gone)
      • diff emits beginRemoveRows for the one anchor that's gone
      • diff emits dataChanged for the surviving anchor (its content is now joined)
  → ListView destroys delegate for the removed row, the surviving delegate updates
  → Cursor restoration: LiveStructuralKeyHandler's "intent" was "join", so cursor moves to the byte position
    that's now in the surviving delegate (computed as the original separator's location)
```

### Selection across multiple blocks, then typing replaces

```
user mouse-drags from mid-block 2 to mid-block 5 → LiveView's MouseArea
  → hit() calls compute (block, offset) pairs at press and at drag-current
  → Session::setPrimarySelection({TextAnchor anchor, TextAnchor active})
  → primarySelectionChanged emitted
  → LiveSelectionView projects to per-delegate rangeForBlock for blocks 2..5
  → each delegate's TextEdit applies the selection slice

user types 'X'
  → LiveEditBinding (the focused delegate, say block 2):
      • selection is non-empty and spans multiple blocks
      • compute the canonical byte range to replace: [resolveAnchor(anchor), resolveAnchor(active)]
        normalized to ascending
      • applyLocalEdit(MarkoffEdit{start, end-start, "X"})
  → contentsChanged → parse → diff sees blocks 3, 4, 5 collapse into block 2's content
  → AstBlockDiff: A_2 content updated, A_3..A_5 removed
  → ListView: surviving delegate updated, others removed
  → Cursor at position after the inserted 'X' (block 2's range, offset of insertion + 1)
```

### Cut / Copy / Paste

```
Copy:
  Ctrl+C OR right-click → "Copy"
    → LiveClipboardController.copy()
    → reads Session::primarySelection
    → byte range = [resolveAnchor(anchor), resolveAnchor(active)] normalized
    → bytes = MarkoffDocument::toMarkdownUtf8().mid(byteRange)
    → QGuiApplication::clipboard()->setText(QString::fromUtf8(bytes))

Cut:
  Ctrl+X OR right-click → "Cut"
    → copy() as above
    → applyLocalEdit(MarkoffEdit{start, length, ""})

Paste:
  Ctrl+V OR right-click → "Paste"
    → text = QGuiApplication::clipboard()->text() (text/plain only; HTML and images ignored)
    → bytes = text.toUtf8()
    → if selection non-empty: replace; else: insert at caret
    → applyLocalEdit(MarkoffEdit{caretOrSelStart, selLengthOrZero, bytes})
    → next parse decomposes the inserted markdown into structure (newlines split paragraphs etc.)
```

### Undo

```
Ctrl+Z (any mode):
  → CommandFacade::undo() → MarkoffDocument::undo()
  → returns optional<Operation>; if none, no-op
  → contentsChanged emitted
  → parse → parseUpdatedAt
  → diff applied
  → Cursor restoration: LiveEditBinding inspects the returned Operation, derives a TextAnchor at the
    edit site, calls Session::setPrimarySelection({siteAnchor, siteAnchor})
  → primarySelectionChanged → LiveSelectionView places cursor in the delegate
```

### IME composition

```
user begins composing (e.g. Korean input):
  → TextEdit native behaviour: preedit text appears in delegate's QTextDocument; QInputMethodEvent
    delivered, processed by Qt internally
  → LiveEditBinding suppresses applyLocalEdit while compositionActive==true (TextEdit::inputMethodComposing)
  → if a parse return arrives during composition with dataChanged for this row:
      LiveListModelBinding defers the text update for this row only; format-only updates apply
user commits composition:
  → TextEdit::contentsChange fires with the final composed text as one batch
  → LiveEditBinding's normal path runs, applyLocalEdit with the composed bytes
  → deferred dataChanged updates (if any) are flushed; usually no-op since the new applyLocalEdit
    will trigger a fresh parse and the deferred parse is stale
```

### Save / dirty (app layer)

```
On open:
  app reads file → MarkoffDocument::resetContent(bytes, FirstOpen)
  app: lastSavedVersion = doc.version()
  app: dirty = false; window title clean

On any contentsChanged:
  app: if (doc.version() != lastSavedVersion && !dirty) { dirty = true; updateWindowTitle(); }
  app: if (doc.version() == lastSavedVersion && dirty) { dirty = false; updateWindowTitle(); }

On Ctrl+S:
  app writes doc.toMarkdownUtf8() to file
  app: lastSavedVersion = doc.version()
  app: dirty = false; updateWindowTitle()

On close with dirty:
  app prompts; user picks save / discard / cancel
```

---

## 4. Invariants

These must hold for every commit touching the editing path:

1. **Source-faithful text representation.** Each text-bearing delegate's QTextDocument text equals the canonical UTF-8 source bytes for its block range (modulo UTF-16 conversion for QTextDocument's internal storage). qtPos in the delegate translates to canonical byte offset by simple arithmetic, never by reverse-mapping rendered text to source.

2. **Single source of truth: `MarkoffDocument` only.** No per-delegate independent state for content. The cycle-guarded `LiveEditBinding` writes go through `applyLocalEdit`; reads go through the model's `text` role which is computed from the parsed AST + canonical source. Speculative kind changes (`speculativelyChangeKind`) are model-layer state that's reconciled with parser truth on every parse return; speculation is never authoritative.

3. **`Session::primarySelection` is the single canonical selection across modes.** No `LiveSelectionModel` parallel state. View projections to per-delegate ranges are derived, never authoritative.

4. **No `TextEdit.MarkdownText`.** Every text-bearing delegate uses `TextEdit.PlainText`. Inline rendering goes through `InlineFormatHighlighter` driven by markoff-parser. Qt's MD parser is unused.

5. **Approach 3 byte ranges are stable up to the next parse.** Between parses, a delegate's `block_start` is whatever the most recent parse said it was. After a local edit but before the next parse return, the binding holds a "shadow" `block_start` that's adjusted by the local edit's delta, so qtPos translation continues to work during the parse round-trip. (This is the foundation's `BlockAnchor` doing its job; the view consumes it as opaque.)

6. **CRDT types are opaque to the view layer.** `markoff-view-qml` headers must not `#include <crdt/...>`. `Markoff::TextAnchor` (the foundation typedef/wrapper) is the only way the view holds CRDT-typed values, and it never inspects, constructs, or compares them — only passes them back to `MarkoffDocument` translation APIs.

7. **No focus-proxy, no `QApplication::sendEvent`, no QML focus chain workarounds.** The walking skeleton's "QML focus chain handles per-delegate input naturally" remains true. Structural keys are intercepted by `LiveStructuralKeyHandler` via QML `Keys.onPressed`, not by intercepting at the application level.

8. **Cycle-guard is per-delegate.** Each `LiveEditBinding` instance owns its own reentrancy guard. Writes from `MarkoffDocument` to a delegate's TextEdit (during model `dataChanged`) are flagged and ignored by the binding's `contentsChange` slot. Mirrors `SourceTextDocumentBinding`'s pattern.

9. **IME composition is held off.** No `applyLocalEdit` fires while a delegate's TextEdit is in active composition. Parse-driven text updates for that row are deferred until composition ends.

10. **Undo entries are coalesced per a deterministic policy.** The view-side policy is purely a function of recent input history (printable / non-printable / movement / structural / time gap); it doesn't depend on user-configurable settings in v0.

### Error / edge cases

| Scenario | Strategy |
|---|---|
| Empty doc / fresh file (no AST blocks) | `LiveView` itself accepts focus when the model has zero rows. First keystroke triggers `applyLocalEdit` at byte 0; reparse produces a paragraph row; focus transfers to the new delegate. |
| Paste of a 1MB blob | Single `applyLocalEdit` of the bytes; parse is incremental and shouldn't choke; ListView streams new delegates as the diff applies. No special-case in v0; if dogfood reveals bad UX, escalate. |
| User types while parse is in flight (older than current local version) | Common case. `LiveListModelBinding` checks `parsed.atVersion` against `MarkoffDocument::version()`; if stale, the diff still runs but the binding marks "stale" so subsequent `dataChanged` for rows the user has touched since `atVersion` use the canonical source for those rows (read via `BlockAnchor`'s shadow byte range), not the stale AST text. |
| Speculative code-block fence is wrong (user undoes the open ``` ```) | On parse return, parser sees no fenced code block; speculative kind reverts paragraph; `LiveSpeculativeFenceController` calls `LiveBlockModel::revertSpeculativeKind(row)`. |
| Speculative inline open delimiter that never closes | Highlighter styles to end-of-block; parser never produces a real inline node; speculative styling persists. Visually identical to "user is mid-typing a bold span" — that's the desired UX. |
| Backspace at offset 0 of the very first block | No-op (no prev block to delete from). |
| Click on HR or image delegate | Cursor routes to end of preceding text delegate (or start of following text delegate if HR is at top of doc). HR/image is never the focused element in v0. |
| Tab outside any code block | No-op in v0 (no list-indent semantics; lists are deferred). |
| Cross-mode undo where the originating mode's cursor model differs | Undo derives a `TextAnchor` at the edit site from the returned Operation; the current mode's selection projection takes that anchor and places its cursor accordingly. v0 lands cursor at edit site, not at exact pre-edit position. |
| Delegate kind changes mid-typing (parser-driven promotion: `# `→ heading) | Old delegate Item is destroyed by `DelegateChooser`; new Item is constructed for the new kind. `LiveListModelBinding` tracks "previously focused row + cursor position" and on the next QML scene update, applies focus + cursor to the new delegate via `Component.onCompleted` or programmatic focus restoration. Cursor offset is preserved as a byte position into the same row's source. |
| User types a `> ` prefix (blockquote — no delegate exists) | Parser produces a `block_quote` node. `LiveBlockModel`'s `kind` role exposes the raw AST kind string. `LiveView.qml`'s `DelegateChooser` has a fallback `DelegateChoice` with no `roleValue` (Qt.labs.qmlmodels' default-fallback semantics), pointing at `ParagraphDelegate`. So unsupported kinds render as paragraph showing the raw source markdown including any block-level prefix markers (`> `, `- `, `1. `, etc.). The BlockKey's `kind` field stores the raw AST kind unchanged, so identity diff distinguishes a block_quote row from a paragraph row even though both render via `ParagraphDelegate`. |
| Selection write while parse in flight | `Session::setPrimarySelection` is foundation-side and operates on `TextAnchor`s, not byte offsets, so it survives reparses naturally. |
| Two delegates simultaneously composing IME (impossible — only one focus, but defensive) | Composition is per-TextEdit; only the focused TextEdit can compose. The binding's "is composing?" check reads from the specific TextEdit, not a global flag. |

---

## 5. Testing strategy

**Unit tests (pure C++ where possible, otherwise QML test harness):**

| Test target | Coverage |
|---|---|
| `tst_view_qml_live_edit_binding` | Per-delegate cycle-guarded write path. UTF-16/8 translation. Reentrancy. Multi-edit batches. Undo coalesce policy state machine. |
| `tst_view_qml_live_structural_keys` | Each structural key + offset combination from Q4. Backspace eats through trailing whitespace. Enter mid-block splits with kind preservation. Tab in code block. |
| `tst_view_qml_live_speculative_fence` | Open fence flips kind; closing fence + parse confirmation; reconciliation when wrong; nested fence inside an existing code block (no double-promotion). |
| `tst_view_qml_inline_format_highlighter` | Speculative open `**`/`_`/`` ` ``/`~~`/`==`. Closing produces parser-confirmed range; visually identical (test verifies QTextCharFormat equality between speculative and confirmed ranges). Nested formatting. Inline code does not interpret nested. |
| `tst_view_qml_live_selection_view` | Projection from `Session::primarySelection` to per-delegate ranges. Cross-block correctness. Round-trip via `setPrimarySelection`. Mode-toggle. |
| `tst_view_qml_live_clipboard` | Copy / Cut / Paste round-trip on single-block, cross-block, empty selection. Markdown source on the wire. Paste of multi-line markdown that decomposes structurally. |
| `tst_view_qml_live_undo` | Coalesce policy: 5 chars, 1 char then 1s gap then 1 char (two groups), printable then Backspace then printable (two groups). Cross-mode: edit in Live, switch to Source, Ctrl+Z. Cursor restoration to edit site. |
| `tst_view_qml_live_ime` | Composition state during preedit; deferral of dataChanged; flush on commit; format updates apply during composition. Tested via synthetic `QInputMethodEvent` injection. |
| `tst_view_qml_ast_block_diff` (modified) | BlockAnchor identity. Typing in row N produces single `dataChanged` not `delete+insert`. Splits and merges produce correct `Insert`/`Remove` ops with anchors that match parser output. |
| `tst_view_qml_live_view_qml` (modified) | End-to-end: type, paste, undo, redo, mode-toggle. Smoke test for the integrated stack. |

**Manual / dogfood:**

- Type continuously in a 50KB document; verify no perceptible lag, no cursor jumps, no focus loss.
- Korean / Japanese / Chinese IME composition end-to-end.
- Paste a multi-paragraph markdown blob with mixed kinds (heading + paragraph + code block); verify structural decomposition is correct.
- Undo through ~50 keystrokes; verify reasonable group boundaries.
- Mode-toggle Source ↔ Live mid-edit; verify selection survives, undo stack survives, cursor lands somewhere reasonable.

---

## 6. Walking-skeleton retirement

The walking skeleton committed three things this spec retires explicitly. Each is a single deliberate change, not a side effect:

1. **`TextEdit.MarkdownText` → custom highlighter.** `ParagraphDelegate.qml` and `HeadingDelegate.qml` flip to `TextEdit.PlainText` and attach `InlineFormatHighlighter`. The highlighter is new; KSyntaxHighlighting in `CodeBlockDelegate` stays (different concern: syntax-highlight source code, not parse markdown).

2. **`(kind, content-hash)` BlockKey → `(kind, BlockAnchor)`.** `AstBlockDiff` and `LiveBlockModel` change in lockstep. The walking-skeleton tests for `AstBlockDiff` are mostly preserved (the diff algorithm is the same shape) but identity equality is now anchor-based.

3. **`LiveSelectionModel` → `LiveSelectionView`.** Class is renamed and refactored; the existing test file is replaced. `LiveView.qml` changes its import name and method calls.

These retirements are scheduled for **the first task slice of the implementation plan**, not deferred. Doing them first is what unlocks all subsequent editing work; doing them last would mean the editing code is built against a model it's about to discard.

---

## 7. Future evolution

These extension points are designed-in but not implemented in v0. The spec calls them out so they don't get accidentally papered over.

- **Cursor-aware delimiter visibility.** `InlineFormatHighlighter` already scans for delimiters; adding "hide markers when cursor is outside the span" is a cosmetic flag on the highlighter, additive. Future inline-formatting spec.
- **Format toggles (Ctrl+B/Ctrl+I/Ctrl+K).** Editor-side commands that wrap selection with delimiters. No model changes; pure editor command. Future inline-formatting spec.
- **WYSIWG paragraph-aware mode.** Toggle in `LiveView` that changes the gap-routing rule: Enter at end of last visible char inserts `\n\n` instead of `\n`; click in visual gap creates an empty paragraph. Single rule change, doesn't touch delegates or bindings. Future UX spec.
- **HTML→MD on paste.** `LiveClipboardController` checks `text/html` first; runs through a turndown-style converter; falls back to `text/plain`. Self-contained change in the controller. Future clipboard spec.
- **Image paste.** `LiveClipboardController` checks for image MIME types; writes file to doc directory; inserts `![](path)`. Requires app-level "doc directory" knowledge — likely a foundation `Session` or app-host concern. Future image spec.
- **Edits to non-text delegates.** Selecting an image as a unit, deleting it, dragging it, replacing its alt text via a side panel. Probably a side panel in v0 of that spec. Future non-text-delegate spec.
- **Multi-cursor.** `Session::secondarySelections` is already in foundation. Live would need to project each selection to delegate ranges and route edit commands across all of them. Future multi-cursor spec.
- **Drag-and-drop, spell-check.** Standard Qt mechanisms; future spec each.

---

## 8. Out of scope

| Deferred work | Future home |
|---|---|
| Format toggles (Ctrl+B/Ctrl+I/Ctrl+K) | inline-formatting-spec |
| Cursor-aware delimiter hiding | inline-formatting-spec |
| HTML→MD conversion on paste | clipboard-spec |
| Image paste | image-spec |
| Drag-and-drop | dnd-spec |
| Spell-check | spell-check-spec |
| Math / mermaid / table / list / blockquote / callout / frontmatter / link / wikilink delegates (rendering richly) | per-delegate specs |
| Edits to image / HR delegates as units | non-text-delegate-spec |
| WYSIWG paragraph-aware mode toggle | future UX spec (not foreclosed) |
| Multi-cursor | multi-cursor-spec |
| Performance budgets / acceptance criteria | future perf spec; existing bench infrastructure already tracks regressions |
| Save in foundation (vs app) | not planned; markoff-view-qml stays filesystem-agnostic by design |
| Restore exact pre-edit cursor position on undo | future polish; v0 lands at edit site |

---

## 9. References

- **Walking skeleton (read-only Live):** `docs/specs/2026-04-29-live-render-design.md`
- **Foundation `BlockAnchor` spec (precondition):** `docs/specs/2026-04-30-block-anchor-foundation-design.md`
- **Source widget cycle-guarded edit pattern:** `libs/markoff-core/src/SourceTextDocumentBinding.cpp`
- **Foundation `MarkoffDocument` API:** `libs/markoff-core/include/markoff-foundation/MarkoffDocument.h`
- **Foundation `Session` + `Selection`:** `libs/markoff-core/include/markoff-foundation/Session.h`, `Selection.h`
- **Cross-block selection spike findings:** `docs/specs/2026-04-29-cross-block-selection-spike-findings.md`
- **Source-mode editor (mirror its highlighter wiring pattern):** `libs/markoff-view-qml/qml/SourceEditor.qml`
- **KDAB Widget-window bridge:** https://www.kdab.com/display-widget-windows-in-qt-quick-applications/

---

## 10. The bet, restated

`TextEdit.PlainText` everywhere, always editable, with markoff-parser-driven inline rendering via `QTextCharFormat`, Approach 3 byte-range delegates, γ-CRDT-anchor identity, single-source-of-truth selection on `Session`, and shared cross-mode undo on `MarkoffDocument` — is fundamentally a different architectural shape from the walking skeleton's read-only-with-`MarkdownText` rendering. This spec retires the three walking-skeleton choices that were tactically right for read-only but architecturally wrong for editing, and replaces them with primitives the foundation will own. The per-delegate cycle-guarded write path mirrors the proven `SourceTextDocumentBinding` pattern; the speculative paths (open inline delimiters, code-block fence) are narrow and bounded; everything else flows through parser truth on a sub-millisecond round-trip. The implementation cost is largely structural — there's not much new to invent, just discipline in retiring the walking-skeleton tech debt and consuming the foundation's `BlockAnchor` API faithfully.
